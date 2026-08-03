// QuantXCore - PositionTracker.cpp
//
// See PositionTracker.hpp for interface documentation and sign conventions.

#include "QuantXCore/execution/PositionTracker.hpp"

#include <cmath>

namespace QuantXCore::execution {

namespace {

// Small epsilon for treating a flipped/closed position as exactly flat.
// Guards against floating point residue (e.g. 1e-16) after offsetting fills.
constexpr double kFlatEpsilon = 1e-9;

[[nodiscard]] bool is_flat(double quantity) {
    return std::abs(quantity) < kFlatEpsilon;
}

// True if `a` and `b` are on the same side (both long-positive or both
// short-negative). A zero quantity has no side, so this is only meaningful
// when both are non-zero.
[[nodiscard]] bool same_side(double a, double b) {
    return (a > 0.0 && b > 0.0) || (a < 0.0 && b < 0.0);
}

}  // namespace

// -----------------------------------------------------------------------
// PositionTracker::apply_fill_locked
// -----------------------------------------------------------------------
// Core position-update math. `signed_fill_qty` is positive for a BUY fill
// and negative for a SELL fill. Caller must hold mutex_.
void PositionTracker::apply_fill_locked(Position& pos, double signed_fill_qty, double fill_price) {
    // Case 1: Flat before this fill -> simply open a new position.
    if (is_flat(pos.quantity)) {
        pos.quantity = signed_fill_qty;
        pos.average_entry_price = fill_price;
        return;
    }

    // Case 2: Same side as existing position -> grow position, update
    // volume-weighted average entry price.
    if (same_side(pos.quantity, signed_fill_qty)) {
        const double new_quantity = pos.quantity + signed_fill_qty;
        pos.average_entry_price =
            (pos.quantity * pos.average_entry_price + signed_fill_qty * fill_price) / new_quantity;
        pos.quantity = new_quantity;
        return;
    }

    // Case 3/4: Opposite side -> this fill reduces, closes, or flips the
    // existing position. Determine how much of the *old* position is
    // actually offset by this fill.
    const bool is_flip = std::abs(signed_fill_qty) > std::abs(pos.quantity);

    if (!is_flip) {
        // Reduce or exactly close: only part (or all) of the old position
        // is offset. Realized PnL is booked on the closed portion only,
        // using the position's existing average entry price.
        // Formula (valid for both long and short):
        //   realized_delta = -signed_fill_qty * (fill_price - avg_entry_price)
        pos.realized_pnl += -signed_fill_qty * (fill_price - pos.average_entry_price);

        pos.quantity += signed_fill_qty;
        if (is_flat(pos.quantity)) {
            pos.quantity = 0.0;
            pos.average_entry_price = 0.0;
        }
        // average_entry_price is unchanged when merely reducing (cost basis
        // of the remaining shares/contracts does not move).
    } else {
        // Flip: the fill fully closes the old position and opens a new one
        // on the opposite side at the fill price. Realize PnL on the entire
        // old position first.
        pos.realized_pnl += pos.quantity * (fill_price - pos.average_entry_price);

        const double remaining_quantity = pos.quantity + signed_fill_qty;
        pos.quantity = remaining_quantity;
        pos.average_entry_price = fill_price;
    }
}

void PositionTracker::on_fill(const execution::Order& fill_order) {
    // Only apply fills that actually represent executed quantity.
    if (fill_order.status != OrderStatus::FILLED &&
        fill_order.status != OrderStatus::PARTIALLY_FILLED) {
        return;
    }
    if (fill_order.filled_quantity <= 0.0) {
        return;
    }

    const double signed_fill_qty = (fill_order.side == alpha::SignalType::BUY)
                                        ? fill_order.filled_quantity
                                        : -fill_order.filled_quantity;

    std::scoped_lock lock(mutex_);

    // operator[] default-constructs a Position on first sight of a symbol;
    // this is the one allocation point in the tracker, and it is on the
    // (comparatively low-frequency) fill path, not the tick path.
    Position& pos = positions_[fill_order.symbol];
    if (pos.symbol.empty()) {
        pos.symbol = fill_order.symbol;
    }

    apply_fill_locked(pos, signed_fill_qty, fill_order.avg_fill_price);

    // Mark-to-market against the fill price immediately so unrealized_pnl
    // is consistent even before the next tick arrives.
    pos.current_price = fill_order.avg_fill_price;
    pos.unrealized_pnl = (pos.current_price - pos.average_entry_price) * pos.quantity;
}

void PositionTracker::on_tick(const data::MarketTick& tick) {
    std::scoped_lock lock(mutex_);

    // Hot path: no insertion, no allocation. If the symbol isn't tracked
    // (no position ever opened), the tick has no PnL relevance and is
    // simply ignored.
    const auto it = positions_.find(tick.symbol);
    if (it == positions_.end()) {
        return;
    }

    Position& pos = it->second;
    pos.current_price = tick.price;
    pos.unrealized_pnl = (pos.current_price - pos.average_entry_price) * pos.quantity;
}

Position PositionTracker::get_position(const std::string& symbol) const {
    std::scoped_lock lock(mutex_);

    const auto it = positions_.find(symbol);
    if (it == positions_.end()) {
        Position empty{};
        empty.symbol = symbol;
        return empty;
    }
    return it->second;
}

double PositionTracker::get_total_realized_pnl() const {
    std::scoped_lock lock(mutex_);
    double total = 0.0;
    for (const auto& [symbol, pos] : positions_) {
        total += pos.realized_pnl;
    }
    return total;
}

double PositionTracker::get_total_unrealized_pnl() const {
    std::scoped_lock lock(mutex_);
    double total = 0.0;
    for (const auto& [symbol, pos] : positions_) {
        total += pos.unrealized_pnl;
    }
    return total;
}

double PositionTracker::get_total_pnl() const {
    // Reuse the two aggregate accessors; each takes its own lock, so the
    // combined result is a composition of two consistent (if not perfectly
    // atomic-together) snapshots. For a strictly atomic combined read under
    // heavy concurrent mutation, iterate positions_ once under a single
    // lock instead.
    return get_total_realized_pnl() + get_total_unrealized_pnl();
}

void PositionTracker::reset() {
    std::scoped_lock lock(mutex_);
    positions_.clear();
}

}  // namespace QuantXCore::execution

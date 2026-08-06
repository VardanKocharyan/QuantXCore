#include "QuantXCore/execution/PositionTracker.hpp"

#include <cmath>

namespace QuantXCore::execution {

namespace {

constexpr double kFlatEpsilon = 1e-9;

[[nodiscard]] bool is_flat(double quantity) {
    return std::abs(quantity) < kFlatEpsilon;
}

[[nodiscard]] bool same_side(double a, double b) {
    return (a > 0.0 && b > 0.0) || (a < 0.0 && b < 0.0);
}

}  // namespace

void PositionTracker::apply_fill_locked(Position& pos, double signed_fill_qty, double fill_price) {
    // Case 1: Flat before this fill -> open new position and reset trailing state
    if (is_flat(pos.quantity)) {
        pos.quantity = signed_fill_qty;
        pos.average_entry_price = fill_price;
        pos.peak_price = fill_price;
        pos.trailing_stop_price = 0.0;
        pos.trailing_active = false;
        return;
    }

    // Case 2: Same side -> grow position, update VWAP and peak price
    if (same_side(pos.quantity, signed_fill_qty)) {
        const double new_quantity = pos.quantity + signed_fill_qty;
        pos.average_entry_price =
            (pos.quantity * pos.average_entry_price + signed_fill_qty * fill_price) / new_quantity;
        pos.quantity = new_quantity;
        if (fill_price > pos.peak_price) {
            pos.peak_price = fill_price;
        }
        return;
    }

    // Case 3/4: Opposite side -> reduce, close, or flip position
    const bool is_flip = std::abs(signed_fill_qty) > std::abs(pos.quantity);

    if (!is_flip) {
        pos.realized_pnl += -signed_fill_qty * (fill_price - pos.average_entry_price);
        pos.quantity += signed_fill_qty;
        if (is_flat(pos.quantity)) {
            pos.quantity = 0.0;
            pos.average_entry_price = 0.0;
            pos.peak_price = 0.0;
            pos.trailing_stop_price = 0.0;
            pos.trailing_active = false;
        }
    } else {
        // Flip: close old position and open new one
        pos.realized_pnl += pos.quantity * (fill_price - pos.average_entry_price);

        const double remaining_quantity = pos.quantity + signed_fill_qty;
        pos.quantity = remaining_quantity;
        pos.average_entry_price = fill_price;
        pos.peak_price = fill_price;
        pos.trailing_stop_price = 0.0;
        pos.trailing_active = false;
    }
}

void PositionTracker::on_fill(const execution::Order& fill_order) {
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

    Position& pos = positions_[fill_order.symbol];
    if (pos.symbol.empty()) {
        pos.symbol = fill_order.symbol;
    }

    apply_fill_locked(pos, signed_fill_qty, fill_order.avg_fill_price);

    pos.current_price = fill_order.avg_fill_price;
    pos.unrealized_pnl = (pos.current_price - pos.average_entry_price) * pos.quantity;
}

void PositionTracker::on_tick(const data::MarketTick& tick, 
                             double trailing_dist_pct, 
                             double trailing_activation_pct) {
    std::scoped_lock lock(mutex_);

    const auto it = positions_.find(tick.symbol);
    if (it == positions_.end()) {
        return;
    }

    Position& pos = it->second;
    pos.current_price = tick.price;
    pos.unrealized_pnl = (pos.current_price - pos.average_entry_price) * pos.quantity;

    // Trailing Stop Loss logic for active LONG position
    if (pos.quantity > 0.0 && pos.average_entry_price > 0.0) {
        // Update peak price
        if (tick.price > pos.peak_price) {
            pos.peak_price = tick.price;
        }

        // Check if trailing stop activation threshold is reached
        const double profit_pct = (pos.peak_price - pos.average_entry_price) / pos.average_entry_price;
        if (profit_pct >= trailing_activation_pct) {
            pos.trailing_active = true;
        }

        // Adjust trailing stop price (Monotonically non-decreasing)
        if (pos.trailing_active) {
            const double calculated_stop = pos.peak_price * (1.0 - trailing_dist_pct);
            if (calculated_stop > pos.trailing_stop_price) {
                pos.trailing_stop_price = calculated_stop;
            }
        }
    }
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
    return get_total_realized_pnl() + get_total_unrealized_pnl();
}

void PositionTracker::reset() {
    std::scoped_lock lock(mutex_);
    positions_.clear();
}

}  // namespace QuantXCore::execution

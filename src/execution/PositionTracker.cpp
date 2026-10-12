#include "QuantXCore/execution/PositionTracker.hpp"
#include <cmath>
#include <algorithm>

namespace QuantXCore::execution {

namespace {

constexpr double kFlatEpsilon = 1e-9;
constexpr double kBreakEvenBufferPct = 0.0005; // Lock +0.05% net profit after fees

[[nodiscard]] bool is_flat(double quantity) {
    return std::abs(quantity) < kFlatEpsilon;
}

[[nodiscard]] bool same_side(double a, double b) {
    return (a > 0.0 && b > 0.0) || (a < 0.0 && b < 0.0);
}

}  // namespace

void PositionTracker::apply_fill_locked(Position& pos, double signed_fill_qty, double fill_price) {
    // Case 1: Flat before this fill -> open new position and reset states
    if (is_flat(pos.quantity)) {
        pos.quantity = signed_fill_qty;
        pos.average_entry_price = fill_price;
        pos.peak_price = fill_price;
        pos.trailing_stop_price = 0.0;
        pos.break_even_active = false;
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
            pos.break_even_active = false;
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
        pos.break_even_active = false;
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

bool PositionTracker::on_tick(const data::MarketTick& tick,
                              double break_even_trigger_pct,
                              double trailing_dist_pct,
                              double trailing_activation_pct) {
    std::scoped_lock lock(mutex_);

    const auto it = positions_.find(tick.symbol);
    if (it == positions_.end()) {
        return false;
    }

    Position& pos = it->second;
    if (is_flat(pos.quantity)) {
        return false;
    }

    pos.current_price = tick.price;
    pos.unrealized_pnl = (pos.current_price - pos.average_entry_price) * pos.quantity;

    // Logic for active LONG position
    if (pos.quantity > 0.0 && pos.average_entry_price > 0.0) {
        if (tick.price > pos.peak_price) {
            pos.peak_price = tick.price;
        }

        const double peak_profit_pct = (pos.peak_price - pos.average_entry_price) / pos.average_entry_price;

        // 1. Break-Even Check (+0.35% profit reached -> Lock Entry + 0.05%)
        if (!pos.break_even_active && peak_profit_pct >= break_even_trigger_pct) {
            pos.break_even_active = true;
            const double break_even_price = pos.average_entry_price * (1.0 + kBreakEvenBufferPct);
            pos.trailing_stop_price = std::max(pos.trailing_stop_price, break_even_price);
        }

        // 2. Dynamic Trailing Stop Check
        if (peak_profit_pct >= trailing_activation_pct) {
            pos.trailing_active = true;
        }

        if (pos.trailing_active) {
            const double calculated_stop = pos.peak_price * (1.0 - trailing_dist_pct);
            pos.trailing_stop_price = std::max(pos.trailing_stop_price, calculated_stop);
        }

        // 3. Stop Loss Execution Check (Price fell below Trailing Stop)
        if (pos.trailing_stop_price > 0.0 && tick.price <= pos.trailing_stop_price) {
            return true; // Trigger MARKET SELL exit order
        }
    }

    return false;
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

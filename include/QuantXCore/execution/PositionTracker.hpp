#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "QuantXCore/execution/Order.hpp"
#include "QuantXCore/data/MarketTick.hpp"

namespace QuantXCore::execution {

// Plain-old-data snapshot of a single symbol's position and PnL state.
struct Position {
    std::string symbol{};
    double quantity{0.0};              // Signed size: +long / -short, 0 flat
    double average_entry_price{0.0};   // Volume-weighted average entry price
    double current_price{0.0};         // Last known market price
    double unrealized_pnl{0.0};        // (current_price - avg_entry_price) * quantity
    double realized_pnl{0.0};          // Cumulative realized PnL for this symbol

    // Trailing Stop Loss State
    double peak_price{0.0};            // Highest price reached since position open
    double trailing_stop_price{0.0};   // Monotonically non-decreasing stop loss price
    bool trailing_active{false};       // True when trailing mechanism is active
};

class PositionTracker {
public:
    PositionTracker() = default;

    // Apply a fill event to the tracker.
    void on_fill(const execution::Order& fill_order);

    // Update mark price, recompute unrealized PnL, and adjust dynamic trailing stop.
    // Accepts trailing stop distance % (e.g. 0.003 for 0.3%) and activation % (e.g. 0.005 for 0.5%).
    void on_tick(const data::MarketTick& tick, 
                 double trailing_dist_pct = 0.003, 
                 double trailing_activation_pct = 0.005);

    void reset();

    [[nodiscard]] Position get_position(const std::string& symbol) const;
    [[nodiscard]] double get_total_realized_pnl() const;
    [[nodiscard]] double get_total_unrealized_pnl() const;
    [[nodiscard]] double get_total_pnl() const;

private:
    static void apply_fill_locked(Position& pos, double signed_fill_qty, double fill_price);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Position> positions_;
};

}  // namespace QuantXCore::execution

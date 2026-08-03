#pragma once

// QuantXCore - PositionTracker.hpp
//
// Thread-safe position and PnL tracking for the QuantXCore trading engine.
// PositionTracker owns per-symbol Position state and is updated by two event
// sources: order fills (execution::Order) and market data (data::MarketTick).
// PnLTracker is a lightweight facade over PositionTracker that additionally
// records timestamped PnL snapshots, useful for building an equity curve.

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "QuantXCore/execution/Order.hpp"
#include "QuantXCore/data/MarketTick.hpp"

namespace QuantXCore::execution {

// -----------------------------------------------------------------------
// Position
// -----------------------------------------------------------------------
// Plain-old-data snapshot of a single symbol's position and PnL state.
// Sign convention: quantity > 0 is Long, quantity < 0 is Short, 0.0 is Flat.
struct Position {
    std::string symbol{};
    double quantity{0.0};              // Signed size: +long / -short
    double average_entry_price{0.0};   // Volume-weighted average entry price
    double current_price{0.0};         // Last known market price
    double unrealized_pnl{0.0};        // (current_price - avg_entry_price) * quantity
    double realized_pnl{0.0};          // Cumulative realized PnL for this symbol
};

// -----------------------------------------------------------------------
// PositionTracker
// -----------------------------------------------------------------------
// Maintains per-symbol Position state derived from order fills and market
// ticks. All public methods are thread-safe; internal state is protected by
// a single mutex since fills and ticks are both relatively low-frequency
// compared to, say, raw tick ingestion in a market data handler.
class PositionTracker {
public:
    PositionTracker() = default;

    // Apply a fill event to the tracker.
    // - Same-side fills against an existing position grow the position and
    //   update the volume-weighted average entry price.
    // - Opposite-side fills reduce, close, or flip the position, realizing
    //   PnL on the portion of the position that was closed.
    // Only FILLED / PARTIALLY_FILLED orders with positive filled_quantity
    // are applied; anything else is a no-op.
    void on_fill(const execution::Order& fill_order);

    // Update the current mark price for a symbol and recompute its
    // unrealized PnL. No-op (and no allocation) if the symbol is not yet
    // tracked, since a tick for an untracked symbol carries no PnL impact.
    void on_tick(const data::MarketTick& tick);


    void reset();
    

    // Returns a copy of the current Position for `symbol`. If the symbol has
    // never been seen, returns a default-constructed Position with the
    // symbol field populated and all numeric fields at zero.
    [[nodiscard]] Position get_position(const std::string& symbol) const;

    // Sum of realized_pnl across all tracked symbols.
    [[nodiscard]] double get_total_realized_pnl() const;

    // Sum of unrealized_pnl across all tracked symbols.
    [[nodiscard]] double get_total_unrealized_pnl() const;

    // get_total_realized_pnl() + get_total_unrealized_pnl()
    [[nodiscard]] double get_total_pnl() const;

private:
    // Applies a signed fill quantity/price to an already-located Position.
    // Caller must hold mutex_.
    static void apply_fill_locked(Position& pos, double signed_fill_qty, double fill_price);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Position> positions_;
};

}  // namespace QuantXCore::execution



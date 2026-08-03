#pragma once
// ============================================================================
// QuantXCore::execution::PnLTracker
// ----------------------------------------------------------------------------
// Equity-curve / PnL snapshot recorder. Records a `PnLSnapshot` each time the
// engine wants to mark the book (typically after every fill), tracking
// realized PnL, unrealized (mark-to-market) PnL, their sum, and resulting
// account equity.
//
// Design notes:
//   * `history_` capacity is pre-reserved (both on construction and on
//     `reset()`) to avoid reallocation churn on the hot snapshot-recording
//     path during a backtest run.
//   * All PnL/equity arithmetic is done in `double` for consistency with the
//     rest of the execution stack (Order::fill_price, commission, etc.).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace QuantXCore::execution {

struct PnLSnapshot {
    std::uint64_t timestamp{};
    double        realized_pnl{};
    double        unrealized_pnl{};
    double        total_pnl{};   // realized_pnl + unrealized_pnl
    double        equity{};      // initial_balance + total_pnl
};

class PnLTracker {
public:
    // Pre-reserved capacity for the snapshot history, applied on
    // construction and on every `reset()`. Sized generously for a typical
    // single-run backtest without over-allocating.
    static constexpr std::size_t kDefaultHistoryReserve = 4096;

    explicit PnLTracker(double initial_balance = 0.0);

    // Reinitializes the tracker for a fresh run: clears history, resets the
    // initial balance, and re-reserves capacity. Lets a single owned
    // instance (e.g. inside BacktestEngine) be reused across multiple runs
    // without reallocating the PnLTracker object itself.
    void reset(double initial_balance);

    // Appends a new PnLSnapshot at `timestamp` computed from the supplied
    // realized/unrealized PnL components.
    void record_snapshot(std::uint64_t timestamp, double realized_pnl, double unrealized_pnl);

    [[nodiscard]] const std::vector<PnLSnapshot>& history() const noexcept;

    [[nodiscard]] double initial_balance() const noexcept;

    // Current mark-to-market equity: the most recent snapshot's equity, or
    // `initial_balance()` if no snapshot has been recorded yet.
    [[nodiscard]] double current_equity() const noexcept;

private:
    double                   initial_balance_{};
    std::vector<PnLSnapshot> history_;
};

}  // namespace QuantXCore::execution

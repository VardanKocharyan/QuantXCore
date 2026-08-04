#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

#include "QuantXCore/execution/Order.hpp"
#include "QuantXCore/execution/PositionTracker.hpp"
#include "QuantXCore/risk/RiskEngine.hpp"

// Forward declaration only — avoids pulling the full data module into every
// translation unit that includes Portfolio.hpp.
namespace QuantXCore::data {
struct MarketTick;
}

namespace QuantXCore::portfolio {

/// Thread-safe ledger for a single trading account.
///
/// Portfolio owns the cash balance, tracks total commissions paid, and
/// derives realized/unrealized PnL and equity by delegating to an internal
/// execution::PositionTracker. All public accessors and mutators serialize
/// on `mutex_`, so a Portfolio instance can be shared across the execution,
/// market-data, and risk threads without external locking.
class Portfolio {
public:
    explicit Portfolio(double initial_balance = 0.0, double leverage = 1.0);

    // Portfolio owns a std::mutex, so it is neither copyable nor movable.
    // Share it via reference or std::shared_ptr<Portfolio> instead.
    Portfolio(const Portfolio&) = delete;
    Portfolio& operator=(const Portfolio&) = delete;
    Portfolio(Portfolio&&) = delete;
    Portfolio& operator=(Portfolio&&) = delete;

    ~Portfolio() = default;

    /// Applies a FILLED or PARTIALLY_FILLED order to cash and commissions,
    /// then forwards the fill to the internal PositionTracker. Orders in any
    /// other OrderStatus are ignored.
    ///
    /// @param filled_order   The order report from the execution venue.
    /// @param commission_pct Commission rate applied to gross trade value
    ///                       (default 0.001 == 10 bps, e.g. Binance taker fee).
    void process_fill(const execution::Order& filled_order, double commission_pct = 0.001);

    /// Forwards a market tick to the internal PositionTracker so it can
    /// mark open positions to market and refresh unrealized PnL.
    void on_tick(const data::MarketTick& tick);

    [[nodiscard]] double get_cash() const;
    [[nodiscard]] double get_realized_pnl() const;
    [[nodiscard]] double get_unrealized_pnl() const;
    [[nodiscard]] double get_total_equity() const;
    [[nodiscard]] double get_buying_power() const;
    [[nodiscard]] double get_total_commissions_paid() const;
    [[nodiscard]] double get_leverage() const;
    [[nodiscard]] double get_initial_balance() const;
    [[nodiscard]] std::size_t get_open_positions_count() const;

    /// Builds a point-in-time snapshot for risk::RiskEngine, including
    /// current balance, peak-equity high-water mark, the balance at the
    /// start of the trading day, and the number of open positions.
    [[nodiscard]] risk::AccountState get_account_state() const;

    /// Resets cash to `initial_balance`, clears commissions and open
    /// positions, re-bases peak-equity/day-start tracking, and clears the
    /// internal PositionTracker.
    void reset(double initial_balance);

private:
    // Internal helpers — callers must already hold `mutex_`.
    [[nodiscard]] double unlocked_total_equity() const;
    void update_peak_equity_locked();
    void update_open_positions_locked(const std::string& symbol);

    double initial_balance_{0.0};
    double current_cash_{0.0};
    double total_commissions_paid_{0.0};
    double leverage_{1.0};

    // Drawdown/risk bookkeeping surfaced via risk::AccountState.
    double peak_equity_{0.0};
    double daily_start_balance_{0.0};

    // Symbols currently holding a non-zero position. Maintained
    // incrementally on each fill since PositionTracker only exposes
    // per-symbol lookups, not an iterable position map.
    std::unordered_set<std::string> open_symbols_;

    execution::PositionTracker position_tracker_;

    mutable std::mutex mutex_;
};

} // namespace QuantXCore::portfolio

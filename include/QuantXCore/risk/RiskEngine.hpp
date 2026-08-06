#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/risk/RiskLimits.hpp"

namespace QuantXCore::risk {

// Tracked state for dynamic trailing stop checks per active position.
struct PositionRiskState {
  std::string symbol;
  alpha::SignalType side{alpha::SignalType::HOLD};
  double entry_price{0.0};
  double extreme_price{0.0};  // Peak price for BUY, Trough price for SELL
  double quantity{0.0};
};

// Mutable account state consulted (and updated) by the RiskEngine on every signal.
struct AccountState {
  double balance{0.0};
  double peak_balance{0.0};
  double daily_start_balance{0.0};
  std::size_t open_positions_count{0};
};

class RiskEngine {
 public:
  RiskEngine(RiskLimits limits, AccountState initial_state);

  // Evaluates `signal` against the configured RiskLimits and current AccountState.
  std::pair<RiskCheckResult, std::optional<OrderRequest>> process_signal(
      const alpha::Signal& signal, double account_risk_pct = 0.02);

  // Validates trade fee against configured max_fee_pct.
  bool validate_fee(double fee_pct) const;

  // Evaluates live tick market data against active positions for dynamic trailing stop loss.
  std::optional<OrderRequest> on_tick(const std::string& symbol, double current_price,
                                       std::uint64_t timestamp = 0);

  // Registers a newly opened position for trailing stop tracking.
  void register_position(const std::string& symbol, alpha::SignalType side, 
                         double entry_price, double quantity = 0.0);

  // Unregisters a closed position from trailing stop tracking.
  void unregister_position(const std::string& symbol);

  // Updates the account balance refreshing peak_balance if a new high-water mark is reached.
  void update_balance(double new_balance);

  // Resets daily_start_balance to the current balance.
  void reset_daily_start_balance();

  // Increments / decrements the tracked count of open positions.
  void increment_open_positions();
  void decrement_open_positions();

  // Returns a snapshot copy of the current account state.
  AccountState account_state() const;

  // Returns a copy of the currently configured risk limits.
  RiskLimits limits() const;

  void reset(double initial_balance);

  bool has_position(const std::string& symbol) const;
  std::optional<PositionRiskState> get_position(const std::string& symbol) const;

 private:
  double current_drawdown_pct() const;
  double current_daily_loss_pct() const;
  bool check_rate_limit_locked();

  mutable std::mutex mutex_;
  RiskLimits limits_;
  AccountState state_;

  std::chrono::steady_clock::time_point last_token_update_{std::chrono::steady_clock::now()};
  double available_tokens_{0.0};

  std::unordered_map<std::string, PositionRiskState> active_positions_;
};

}  // namespace QuantXCore::risk

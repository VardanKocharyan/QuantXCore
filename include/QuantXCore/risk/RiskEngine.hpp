#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/risk/RiskLimits.hpp"

namespace QuantXCore::risk {

class RiskEngine {
 public:
  RiskEngine(RiskLimits limits, AccountState initial_state);

  std::pair<RiskCheckResult, std::optional<OrderRequest>> process_signal(
      const alpha::Signal& signal, double account_risk_pct = 0.02);

  std::optional<OrderRequest> on_tick(const std::string& symbol,
                                      double current_price,
                                      std::uint64_t timestamp);

  void register_position(const std::string& symbol, alpha::SignalType side,
                          double entry_price, double quantity = 1.0);

  void unregister_position(const std::string& symbol);
  void mark_symbol_cooldown(const std::string& symbol, std::uint64_t timestamp_ms);

  bool validate_fee(double fee_pct) const;
  void update_balance(double new_balance);
  void reset_daily_start_balance();
  void increment_open_positions();
  void decrement_open_positions();
  void reset(double initial_balance);

  [[nodiscard]] double current_drawdown_pct() const;
  [[nodiscard]] double current_daily_loss_pct() const;
  [[nodiscard]] AccountState account_state() const;
  [[nodiscard]] RiskLimits limits() const;
  [[nodiscard]] bool has_position(const std::string& symbol) const;
  [[nodiscard]] std::optional<PositionRiskState> get_position(
      const std::string& symbol) const;

 private:
  bool check_rate_limit_locked();
  [[nodiscard]] double current_drawdown_pct_locked() const;
  [[nodiscard]] double current_daily_loss_pct_locked() const;

  mutable std::mutex mutex_;
  RiskLimits limits_;
  AccountState state_;

  double available_tokens_{0.0};
  std::chrono::steady_clock::time_point last_token_update_{
      std::chrono::steady_clock::now()};

  std::unordered_map<std::string, PositionRiskState> active_positions_;
  std::unordered_map<std::string, std::uint64_t> symbol_cooldowns_;
};

}  // namespace QuantXCore::risk

#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/risk/RiskLimits.hpp"

namespace QuantXCore::risk {

// Mutable account state consulted (and updated) by the RiskEngine on every
// signal. This is intentionally a plain-data struct; the RiskEngine owns
// synchronization for all reads/writes to an instance it holds internally,
// but callers may also inspect/seed a copy via RiskEngine::account_state().
struct AccountState {
  double balance{0.0};
  double peak_balance{0.0};
  double daily_start_balance{0.0};
  std::size_t open_positions_count{0};
};

// RiskEngine performs pre-trade risk checks on incoming alpha signals and,
// if approved, sizes and returns a concrete OrderRequest.
//
// Thread safety: all public methods are safe to call concurrently from
// multiple threads. Internal state is protected by a single mutex; the
// engine performs no dynamic allocation on the process_signal() hot path
// beyond what std::string / std::optional copy construction requires for
// the returned OrderRequest.
class RiskEngine {
 public:
  RiskEngine(RiskLimits limits, AccountState initial_state);

  // Evaluates `signal` against the configured RiskLimits and current
  // AccountState. On approval, returns {APPROVED, OrderRequest}. On
  // rejection, returns {REJECTED_*, std::nullopt}.
  //
  // `account_risk_pct` is the fraction of the current account balance to
  // risk on this trade (used for position sizing), defaulting to 2%.
  std::pair<RiskCheckResult, std::optional<OrderRequest>> process_signal(
      const alpha::Signal& signal, double account_risk_pct = 0.02);

  // Updates the account balance (e.g. after a fill or mark-to-market
  // event), refreshing peak_balance if a new high-water mark is reached.
  void update_balance(double new_balance);

  // Resets daily_start_balance to the current balance; intended to be
  // called once at the start of each trading day.
  void reset_daily_start_balance();

  // Increments / decrements the tracked count of open positions. Callers
  // are responsible for invoking these as positions are opened/closed.
  void increment_open_positions();
  void decrement_open_positions();

  // Returns a snapshot copy of the current account state.
  AccountState account_state() const;

  // Returns a copy of the currently configured risk limits.
  RiskLimits limits() const;


  void reset(double initial_balance);

 private:
  // Computes current drawdown from peak_balance, as a positive fraction
  // (0.10 == 10% drawdown). Assumes mutex_ is already held.
  double current_drawdown_pct() const;

  // Computes today's loss from daily_start_balance, as a positive fraction
  // (0.0 if the account is flat or up on the day). Assumes mutex_ is
  // already held.
  double current_daily_loss_pct() const;

  mutable std::mutex mutex_;
  RiskLimits limits_;
  AccountState state_;
};

}  // namespace QuantXCore::risk

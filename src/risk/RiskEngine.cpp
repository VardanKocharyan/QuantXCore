#include "QuantXCore/risk/RiskEngine.hpp"

#include <algorithm>

#include "QuantXCore/risk/PositionSizer.hpp"

namespace QuantXCore::risk {

namespace {
constexpr double kDefaultStopLossPct = 0.02;    // 2% default SL.
constexpr double kDefaultTakeProfitPct = 0.04;  // 4% default TP.
}  // namespace

RiskEngine::RiskEngine(RiskLimits limits, AccountState initial_state)
    : limits_(limits), state_(initial_state) {
  // Ensure peak_balance and daily_start_balance are sane even if the
  // caller seeded a partially-initialized AccountState.
  if (state_.peak_balance < state_.balance) {
    state_.peak_balance = state_.balance;
  }
  if (state_.daily_start_balance <= 0.0) {
    state_.daily_start_balance = state_.balance;
  }
}

double RiskEngine::current_drawdown_pct() const {
  if (state_.peak_balance <= 0.0) {
    return 0.0;
  }
  const double drawdown = (state_.peak_balance - state_.balance) /
                           state_.peak_balance;
  return std::max(0.0, drawdown);
}

double RiskEngine::current_daily_loss_pct() const {
  if (state_.daily_start_balance <= 0.0) {
    return 0.0;
  }
  const double loss = (state_.daily_start_balance - state_.balance) /
                       state_.daily_start_balance;
  return std::max(0.0, loss);
}

std::pair<RiskCheckResult, std::optional<OrderRequest>>
RiskEngine::process_signal(const alpha::Signal& signal,
                            double account_risk_pct) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 1. Validate the signal itself.
  if (signal.type == alpha::SignalType::HOLD || signal.symbol.empty() ||
      signal.price <= 0.0 || signal.strength < 0.0 ||
      signal.strength > 1.0) {
    return {RiskCheckResult::REJECTED_INVALID_SIGNAL, std::nullopt};
  }

  // 2. Drawdown check.
  if (current_drawdown_pct() >= limits_.max_drawdown_pct) {
    return {RiskCheckResult::REJECTED_MAX_DRAWDOWN, std::nullopt};
  }

  // 3. Daily loss check.
  if (current_daily_loss_pct() >= limits_.max_daily_loss_pct) {
    return {RiskCheckResult::REJECTED_DAILY_LOSS, std::nullopt};
  }

  // 4. Open positions check.
  if (state_.open_positions_count >= limits_.max_open_positions) {
    return {RiskCheckResult::REJECTED_MAX_OPEN_POSITIONS, std::nullopt};
  }

  // 5. Default stop-loss / take-profit, direction-aware.
  const bool is_buy = (signal.type == alpha::SignalType::BUY);
  const double stop_loss_price =
      is_buy ? signal.price * (1.0 - kDefaultStopLossPct)
             : signal.price * (1.0 + kDefaultStopLossPct);
  const double take_profit_price =
      is_buy ? signal.price * (1.0 + kDefaultTakeProfitPct)
             : signal.price * (1.0 - kDefaultTakeProfitPct);

  // 6. Position sizing: risk-based quantity capped by max USD notional.
  const double quantity = PositionSizer::ComputeSizedQuantity(
      state_.balance, account_risk_pct, signal.price, stop_loss_price,
      limits_.max_position_size_usd);

  if (quantity <= 0.0) {
    // Sizing collapsed to zero (e.g. zero/negative balance) -- treat as a
    // position-size rejection rather than silently approving a no-op
    // order.
    return {RiskCheckResult::REJECTED_MAX_POSITION_SIZE, std::nullopt};
  }

  OrderRequest order;
  order.symbol = signal.symbol;
  order.side = signal.type;
  order.quantity = quantity;
  order.price = signal.price;
  order.stop_loss_price = stop_loss_price;
  order.take_profit_price = take_profit_price;
  order.strategy_name = signal.strategy_name;
  order.timestamp = signal.timestamp;

  return {RiskCheckResult::APPROVED, std::make_optional(std::move(order))};
}

void RiskEngine::update_balance(double new_balance) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.balance = new_balance;
  if (state_.balance > state_.peak_balance) {
    state_.peak_balance = state_.balance;
  }
}

void RiskEngine::reset_daily_start_balance() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.daily_start_balance = state_.balance;
}

void RiskEngine::increment_open_positions() {
  std::lock_guard<std::mutex> lock(mutex_);
  ++state_.open_positions_count;
}

void RiskEngine::decrement_open_positions() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.open_positions_count > 0) {
    --state_.open_positions_count;
  }
}

AccountState RiskEngine::account_state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

RiskLimits RiskEngine::limits() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return limits_;
}

void RiskEngine::reset(double initial_balance) {
    std::lock_guard<std::mutex> lock(mutex_);

    state_.balance = initial_balance;
    state_.peak_balance = initial_balance;
    state_.daily_start_balance = initial_balance;
    state_.open_positions_count = 0;
}

}  // namespace QuantXCore::risk

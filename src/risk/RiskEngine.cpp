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
  if (state_.peak_balance < state_.balance) {
    state_.peak_balance = state_.balance;
  }
  if (state_.daily_start_balance <= 0.0) {
    state_.daily_start_balance = state_.balance;
  }
  available_tokens_ = static_cast<double>(limits_.max_orders_per_second);
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

bool RiskEngine::check_rate_limit_locked() {
  if (limits_.max_orders_per_second == 0) {
    return true;
  }

  const auto now = std::chrono::steady_clock::now();
  const std::chrono::duration<double> elapsed = now - last_token_update_;
  last_token_update_ = now;

  const double max_tokens = static_cast<double>(limits_.max_orders_per_second);
  available_tokens_ = std::min(max_tokens, available_tokens_ + elapsed.count() * max_tokens);

  if (available_tokens_ >= 1.0) {
    available_tokens_ -= 1.0;
    return true;
  }
  return false;
}

bool RiskEngine::validate_fee(double fee_pct) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (limits_.max_fee_pct <= 0.0) {
    return true;
  }
  return fee_pct <= limits_.max_fee_pct;
}

std::pair<RiskCheckResult, std::optional<OrderRequest>>
RiskEngine::process_signal(const alpha::Signal& signal,
                           double account_risk_pct) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (signal.type == alpha::SignalType::HOLD || signal.symbol.empty() ||
      signal.price <= 0.0 || signal.strength < 0.0 ||
      signal.strength > 1.0) {
    return {RiskCheckResult::REJECTED_INVALID_SIGNAL, std::nullopt};
  }

  if (!check_rate_limit_locked()) {
    return {RiskCheckResult::REJECTED_RATE_LIMIT_EXCEEDED, std::nullopt};
  }

  if (current_drawdown_pct() >= limits_.max_drawdown_pct) {
    return {RiskCheckResult::REJECTED_MAX_DRAWDOWN, std::nullopt};
  }

  if (current_daily_loss_pct() >= limits_.max_daily_loss_pct) {
    return {RiskCheckResult::REJECTED_DAILY_LOSS, std::nullopt};
  }

  if (state_.open_positions_count >= limits_.max_open_positions) {
    return {RiskCheckResult::REJECTED_MAX_OPEN_POSITIONS, std::nullopt};
  }

  const bool is_buy = (signal.type == alpha::SignalType::BUY);
  const double sl_pct = (limits_.trailing_stop_pct > 0.0) ? limits_.trailing_stop_pct : kDefaultStopLossPct;
  const double stop_loss_price =
      is_buy ? signal.price * (1.0 - sl_pct)
             : signal.price * (1.0 + sl_pct);
  const double take_profit_price =
      is_buy ? signal.price * (1.0 + kDefaultTakeProfitPct)
             : signal.price * (1.0 - kDefaultTakeProfitPct);

  const double quantity = PositionSizer::ComputeSizedQuantity(
      state_.balance, account_risk_pct, signal.price, stop_loss_price,
      limits_.max_position_size_usd);

  if (quantity <= 0.0) {
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

void RiskEngine::register_position(const std::string& symbol, alpha::SignalType side, double entry_price, double quantity) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_positions_[symbol] = PositionRiskState{symbol, side, entry_price, entry_price, quantity};
}

std::optional<OrderRequest> RiskEngine::on_tick(const std::string& symbol, double current_price, std::uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = active_positions_.find(symbol);
  if (it == active_positions_.end() || current_price <= 0.0) {
    return std::nullopt;
  }

  auto& pos = it->second;
  const double trail_pct = (limits_.trailing_stop_pct > 0.0) ? limits_.trailing_stop_pct : kDefaultStopLossPct;

  if (pos.side == alpha::SignalType::BUY) {
    if (current_price > pos.extreme_price) {
      pos.extreme_price = current_price;
    }
    const double dynamic_stop = pos.extreme_price * (1.0 - trail_pct);
    if (current_price <= dynamic_stop) {
      OrderRequest exit_order;
      exit_order.symbol = symbol;
      exit_order.side = alpha::SignalType::SELL;
      exit_order.quantity = pos.quantity;
      exit_order.price = current_price;
      exit_order.stop_loss_price = dynamic_stop;
      exit_order.strategy_name = "TrailingStopExit";
      exit_order.timestamp = timestamp;
      return exit_order;
    }
  } else if (pos.side == alpha::SignalType::SELL) {
    if (current_price < pos.extreme_price) {
      pos.extreme_price = current_price;
    }
    const double dynamic_stop = pos.extreme_price * (1.0 + trail_pct);
    if (current_price >= dynamic_stop) {
      OrderRequest exit_order;
      exit_order.symbol = symbol;
      exit_order.side = alpha::SignalType::BUY;
      exit_order.quantity = pos.quantity;
      exit_order.price = current_price;
      exit_order.stop_loss_price = dynamic_stop;
      exit_order.strategy_name = "TrailingStopExit";
      exit_order.timestamp = timestamp;
      return exit_order;
    }
  }

  return std::nullopt;
}

void RiskEngine::unregister_position(const std::string& symbol) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_positions_.erase(symbol);
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
  active_positions_.clear();
  available_tokens_ = static_cast<double>(limits_.max_orders_per_second);
}

bool RiskEngine::has_position(const std::string& symbol) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_positions_.find(symbol) != active_positions_.end();
}

std::optional<PositionRiskState> RiskEngine::get_position(const std::string& symbol) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = active_positions_.find(symbol);
  if (it != active_positions_.end()) {
    return it->second;
  }
  return std::nullopt;
}

}  // namespace QuantXCore::risk

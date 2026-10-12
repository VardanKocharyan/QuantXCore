#include "QuantXCore/risk/RiskEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace QuantXCore::risk {

namespace {
constexpr double kDefaultStopLossPct = 0.020;   // 2.0% Default SL
constexpr double kDefaultTakeProfitPct = 0.040; // 4.0% Default TP
constexpr std::uint64_t kCooldownMillis = 15 * 60 * 1000; // 15-minute Cooldown
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

double RiskEngine::current_drawdown_pct_locked() const {
  if (state_.peak_balance <= 0.0) return 0.0;
  const double drawdown = (state_.peak_balance - state_.balance) / state_.peak_balance;
  return std::max(0.0, drawdown);
}

double RiskEngine::current_drawdown_pct() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_drawdown_pct_locked();
}

double RiskEngine::current_daily_loss_pct_locked() const {
  if (state_.daily_start_balance <= 0.0) return 0.0;
  const double loss = (state_.daily_start_balance - state_.balance) / state_.daily_start_balance;
  return std::max(0.0, loss);
}

double RiskEngine::current_daily_loss_pct() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_daily_loss_pct_locked();
}

void RiskEngine::mark_symbol_cooldown(const std::string& symbol, std::uint64_t timestamp_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  symbol_cooldowns_[symbol] = timestamp_ms;
}

bool RiskEngine::check_rate_limit_locked() {
  if (limits_.max_orders_per_second == 0) return true;

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
  if (limits_.max_fee_pct <= 0.0) return true;
  return fee_pct <= limits_.max_fee_pct;
}

std::pair<RiskCheckResult, std::optional<OrderRequest>>
RiskEngine::process_signal(const alpha::Signal& signal, double account_risk_pct) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (signal.type == alpha::SignalType::HOLD || signal.symbol.empty() ||
      signal.price <= 0.0 || signal.strength < 0.0 || signal.strength > 1.0) {
    return {RiskCheckResult::REJECTED_INVALID_SIGNAL, std::nullopt};
  }

  // 1. HARD RULE: Max 1 active position per symbol
  if (active_positions_.find(signal.symbol) != active_positions_.end()) {
    return {RiskCheckResult::REJECTED_MAX_OPEN_POSITIONS, std::nullopt};
  }

  // 2. Cooldown check
  auto cooldown_it = symbol_cooldowns_.find(signal.symbol);
  if (cooldown_it != symbol_cooldowns_.end()) {
    if (signal.timestamp < cooldown_it->second + kCooldownMillis) {
      return {RiskCheckResult::REJECTED_RATE_LIMIT_EXCEEDED, std::nullopt};
    }
  }

  if (!check_rate_limit_locked()) {
    return {RiskCheckResult::REJECTED_RATE_LIMIT_EXCEEDED, std::nullopt};
  }

  if (current_drawdown_pct_locked() >= limits_.max_drawdown_pct) {
    return {RiskCheckResult::REJECTED_MAX_DRAWDOWN, std::nullopt};
  }

  if (current_daily_loss_pct_locked() >= limits_.max_daily_loss_pct) {
    return {RiskCheckResult::REJECTED_DAILY_LOSS, std::nullopt};
  }

  if (state_.open_positions_count >= limits_.max_open_positions) {
    return {RiskCheckResult::REJECTED_MAX_OPEN_POSITIONS, std::nullopt};
  }

  const bool is_buy = (signal.type == alpha::SignalType::BUY);
  double stop_loss_price = 0.0;
  double take_profit_price = 0.0;

  if (signal.stop_loss > 0.0 && signal.take_profit > 0.0) {
    stop_loss_price = signal.stop_loss;
    take_profit_price = signal.take_profit;
  }
  else if (signal.atr > 0.0) {
    const double sl_distance = 1.5 * signal.atr;
    stop_loss_price = is_buy ? (signal.price - sl_distance) : (signal.price + sl_distance);
    take_profit_price = is_buy ? (signal.price + (sl_distance * 2.0)) : (signal.price - (sl_distance * 2.0));
  }
  else {
    const double sl_pct = (limits_.trailing_stop_pct > 0.0) ? limits_.trailing_stop_pct : kDefaultStopLossPct;
    stop_loss_price = is_buy ? signal.price * (1.0 - sl_pct) : signal.price * (1.0 + sl_pct);
    take_profit_price = is_buy ? signal.price * (1.0 + kDefaultTakeProfitPct) : signal.price * (1.0 - kDefaultTakeProfitPct);
  }

  double stop_distance = std::abs(signal.price - stop_loss_price);
  const double min_stop_dist = signal.price * (limits_.min_stop_loss_pct > 0.0 ? limits_.min_stop_loss_pct : 0.005);

  if (stop_distance < min_stop_dist) {
    stop_distance = min_stop_dist;
    stop_loss_price = is_buy ? (signal.price - min_stop_dist) : (signal.price + min_stop_dist);
  }

  double quantity = 0.0;
  if (stop_distance > 1e-9) {
    const double risk_amount = state_.balance * account_risk_pct;
    quantity = risk_amount / stop_distance;
  }

  double used_capital_usd = 0.0;
  for (const auto& [sym, pos] : active_positions_) {
    used_capital_usd += pos.quantity * pos.entry_price;
  }

  const double available_usd = std::max(0.0, state_.balance - used_capital_usd);

  double max_allowed_usd = available_usd;

  if (limits_.allocated_capital_usd > 0.0) {
    const double limit_rem = std::max(0.0, limits_.allocated_capital_usd - used_capital_usd);
    max_allowed_usd = std::min(max_allowed_usd, limit_rem);
  }
  if (limits_.max_position_size_usd > 0.0) {
    max_allowed_usd = std::min(max_allowed_usd, limits_.max_position_size_usd);
  }

  if (signal.price > 0.0) {
    const double max_qty = max_allowed_usd / signal.price;
    quantity = std::min(quantity, max_qty);
  }

  if (quantity <= 1e-8) {
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

void RiskEngine::register_position(const std::string& symbol,
                                    alpha::SignalType side,
                                    double entry_price,
                                    double quantity) {
    std::lock_guard<std::mutex> lock(mutex_);

    PositionRiskState pos;
    pos.symbol = symbol;
    pos.side = side;
    pos.entry_price = entry_price;
    pos.current_price = entry_price;
    pos.quantity = quantity;
    pos.opened_timestamp = static_cast<std::uint64_t>(std::time(nullptr));

    active_positions_[symbol] = pos;
}

void RiskEngine::unregister_position(const std::string& symbol) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_positions_.erase(symbol);
}

std::optional<OrderRequest> RiskEngine::on_tick(const std::string& symbol, double current_price, std::uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = active_positions_.find(symbol);
  if (it == active_positions_.end()) {
    return std::nullopt;
  }

  auto& pos = it->second;
  const double trailing_pct = (limits_.trailing_stop_pct > 0.0) ? limits_.trailing_stop_pct : kDefaultStopLossPct;
  bool trigger_exit = false;
  double stop_price = 0.0;

  if (pos.side == alpha::SignalType::BUY) {
    if (current_price > pos.current_price) {
      pos.current_price = current_price;
    }
    stop_price = pos.current_price * (1.0 - trailing_pct);
    if (current_price <= stop_price) {
      trigger_exit = true;
    }
  } else if (pos.side == alpha::SignalType::SELL) {
    if (pos.current_price == 0.0 || current_price < pos.current_price) {
      pos.current_price = current_price;
    }
    stop_price = pos.current_price * (1.0 + trailing_pct);
    if (current_price >= stop_price) {
      trigger_exit = true;
    }
  }

  if (trigger_exit) {
    OrderRequest exit_order;
    exit_order.symbol = symbol;
    exit_order.side = (pos.side == alpha::SignalType::BUY) ? alpha::SignalType::SELL : alpha::SignalType::BUY;
    exit_order.quantity = pos.quantity;
    exit_order.price = current_price;
    exit_order.stop_loss_price = stop_price;
    exit_order.strategy_name = "TrailingStop";
    exit_order.timestamp = timestamp;

    active_positions_.erase(it);
    if (state_.open_positions_count > 0) {
      --state_.open_positions_count;
    }

    return exit_order;
  }

  return std::nullopt;
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
  symbol_cooldowns_.clear();
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

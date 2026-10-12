#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "QuantXCore/alpha/Signal.hpp"

namespace QuantXCore::risk {

// Static configuration describing the risk envelope
struct RiskLimits {
  double max_drawdown_pct{0.15};
  double max_daily_loss_pct{0.05};
  double max_position_size_usd{100000.0};
  double allocated_capital_usd{0.0};
  double min_stop_loss_pct{0.005};
  double max_leverage{3.0};
  std::size_t max_open_positions{5};
  double trailing_stop_pct{0.02};
  double max_fee_pct{0.001};
  std::size_t max_orders_per_second{0};
};

// Result of a pre-trade risk check performed by the RiskEngine
enum class RiskCheckResult {
  APPROVED,
  REJECTED_MAX_DRAWDOWN,
  REJECTED_DAILY_LOSS,
  REJECTED_MAX_POSITION_SIZE,
  REJECTED_MAX_OPEN_POSITIONS,
  REJECTED_INVALID_SIGNAL,
  REJECTED_RATE_LIMIT_EXCEEDED,
  REJECTED_FEE_LIMIT_EXCEEDED,
};

// A fully-sized, risk-checked order ready to be routed to execution
struct OrderRequest {
  std::string symbol;
  alpha::SignalType side{alpha::SignalType::HOLD};
  double quantity{0.0};
  double price{0.0};
  double stop_loss_price{0.0};
  double take_profit_price{0.0};
  std::string strategy_name;
  std::uint64_t timestamp{0};
};

// Account balance and tracking state
struct AccountState {
  double balance{0.0};
  double peak_balance{0.0};
  double daily_start_balance{0.0};
  std::size_t open_positions_count{0};
};

// Position risk state tracker
struct PositionRiskState {
  std::string symbol;
  alpha::SignalType side{alpha::SignalType::HOLD};
  double entry_price{0.0};
  double current_price{0.0};
  double quantity{0.0};
  std::uint64_t opened_timestamp{0};
};

}  // namespace QuantXCore::risk

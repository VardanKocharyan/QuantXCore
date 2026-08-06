#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "QuantXCore/alpha/Signal.hpp"

namespace QuantXCore::risk {

// Static configuration describing the risk envelope that the RiskEngine
// enforces for a given account / strategy book. All percentage fields are
// expressed as fractions (e.g. 0.15 == 15%).
struct RiskLimits {
  double max_drawdown_pct{0.15};
  double max_daily_loss_pct{0.05};
  double max_position_size_usd{100000.0};
  double max_leverage{3.0};
  std::size_t max_open_positions{5};

  // Trailing stop loss distance percentage (e.g. 0.02 == 2%).
  double trailing_stop_pct{0.02};

  // Maximum allowed taker/maker fee tier percentage (e.g. 0.001 == 0.1%).
  double max_fee_pct{0.001};

  // Rate limiter: maximum orders allowed per second (0 = disabled).
  std::size_t max_orders_per_second{0};
};

// Result of a pre-trade risk check performed by the RiskEngine.
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

// A fully-sized, risk-checked order ready to be routed to an execution
// gateway.
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

}  // namespace QuantXCore::risk

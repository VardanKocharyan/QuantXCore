#pragma once

#include <algorithm>
#include <cmath>

namespace QuantXCore::risk {

// PositionSizer is a stateless, header-only collection of pure functions
// used to translate an account's risk budget into a concrete order
// quantity. All functions are `inline` / `constexpr` where possible so that
// they can be used from a hot path without incurring call overhead or
// allocations, and are safe to call concurrently from multiple threads
// since they carry no shared state.
class PositionSizer {
 public:
  PositionSizer() = delete;

  // Computes the notional risk (in account currency) that a single trade is
  // allowed to lose, given the total account balance and a risk percentage
  // per trade (e.g. 0.02 == risk 2% of the account per trade).
  //
  // Returns 0.0 if either input is non-positive.
  static constexpr double ComputeRiskBudget(double account_balance,
                                             double account_risk_pct) {
    if (account_balance <= 0.0 || account_risk_pct <= 0.0) {
      return 0.0;
    }
    return account_balance * account_risk_pct;
  }

  // Computes the per-unit risk implied by an entry price and a stop-loss
  // price, i.e. the absolute distance between the two. Returns 0.0 if the
  // prices are equal (which would otherwise imply infinite size).
  static constexpr double ComputePerUnitRisk(double entry_price,
                                              double stop_loss_price) {
    return std::fabs(entry_price - stop_loss_price);
  }

  // Computes the order quantity (in units of the underlying instrument)
  // such that, if the position is stopped out at `stop_loss_price`, the
  // realized loss equals `account_balance * account_risk_pct`.
  //
  // Returns 0.0 for any degenerate input (non-positive balance/price, or
  // entry_price == stop_loss_price) rather than dividing by zero.
  static double ComputeQuantityByRisk(double account_balance,
                                       double account_risk_pct,
                                       double entry_price,
                                       double stop_loss_price) {
    const double risk_budget =
        ComputeRiskBudget(account_balance, account_risk_pct);
    const double per_unit_risk =
        ComputePerUnitRisk(entry_price, stop_loss_price);

    if (risk_budget <= 0.0 || per_unit_risk <= 0.0 || entry_price <= 0.0) {
      return 0.0;
    }
    return risk_budget / per_unit_risk;
  }

  // Caps a notional USD exposure (quantity * price) to a maximum allowed
  // USD position size, returning the (possibly reduced) quantity.
  static double CapQuantityByNotional(double quantity, double price,
                                       double max_position_size_usd) {
    if (quantity <= 0.0 || price <= 0.0) {
      return 0.0;
    }
    const double notional = quantity * price;
    if (max_position_size_usd <= 0.0) {
      return 0.0;
    }
    if (notional <= max_position_size_usd) {
      return quantity;
    }
    return max_position_size_usd / price;
  }

  // Convenience helper combining risk-based sizing with a hard notional
  // cap. This is the primary entry point used by RiskEngine.
  static double ComputeSizedQuantity(double account_balance,
                                      double account_risk_pct,
                                      double entry_price,
                                      double stop_loss_price,
                                      double max_position_size_usd) {
    const double raw_quantity = ComputeQuantityByRisk(
        account_balance, account_risk_pct, entry_price, stop_loss_price);
    return CapQuantityByNotional(raw_quantity, entry_price,
                                  max_position_size_usd);
  }
};

}  // namespace QuantXCore::risk

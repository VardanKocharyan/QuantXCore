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
  static constexpr double ComputeRiskBudget(double account_balance,
                                             double account_risk_pct) {
    if (account_balance <= 0.0 || account_risk_pct <= 0.0) {
      return 0.0;
    }
    return account_balance * account_risk_pct;
  }

  // Computes the per-unit risk implied by an entry price and a stop-loss
  // price, i.e. the absolute distance between the two.
  static constexpr double ComputePerUnitRisk(double entry_price,
                                              double stop_loss_price) {
    return std::fabs(entry_price - stop_loss_price);
  }

  // Calculates per-unit risk taking into account round-trip Binance trading fees
  // (e.g. fee_pct = 0.001 for 0.1%).
  static constexpr double ComputeFeeAdjustedPerUnitRisk(double entry_price,
                                                        double stop_loss_price,
                                                        double fee_pct = 0.001) {
    if (entry_price <= 0.0 || stop_loss_price <= 0.0) {
      return 0.0;
    }
    const double effective_entry = entry_price * (1.0 + fee_pct);
    const double effective_exit  = stop_loss_price * (1.0 - fee_pct);
    const double risk = effective_entry - effective_exit;
    return (risk > 0.0) ? risk : 0.0;
  }

  // Computes the minimum exit price required to break even after paying
  // round-trip trading fees.
  static constexpr double ComputeBreakEvenPrice(double entry_price,
                                                 double fee_pct = 0.001) {
    if (entry_price <= 0.0 || fee_pct >= 1.0) {
      return 0.0;
    }
    return entry_price * ((1.0 + fee_pct) / (1.0 - fee_pct));
  }

  // Computes raw order quantity based on risk budget and per-unit risk.
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

  // Caps a notional USD exposure (quantity * price) to a maximum allowed USD size.
  static double CapQuantityByNotional(double quantity, double price,
                                       double max_position_size_usd) {
    if (quantity <= 0.0 || price <= 0.0 || max_position_size_usd <= 0.0) {
      return 0.0;
    }
    const double notional = quantity * price;
    if (notional <= max_position_size_usd) {
      return quantity;
    }
    return max_position_size_usd / price;
  }

  // Standard entry point combining risk-based sizing with hard USD cap.
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

  // Fee-aware entry point combining fee-adjusted risk sizing with hard USD cap.
  static double ComputeFeeAdjustedSizedQuantity(double account_balance,
                                                 double account_risk_pct,
                                                 double entry_price,
                                                 double stop_loss_price,
                                                 double max_position_size_usd,
                                                 double fee_pct = 0.001) {
    const double risk_budget = ComputeRiskBudget(account_balance, account_risk_pct);
    const double per_unit_risk = ComputeFeeAdjustedPerUnitRisk(entry_price, stop_loss_price, fee_pct);

    if (risk_budget <= 0.0 || per_unit_risk <= 0.0 || entry_price <= 0.0) {
      return 0.0;
    }
    const double raw_quantity = risk_budget / per_unit_risk;
    return CapQuantityByNotional(raw_quantity, entry_price, max_position_size_usd);
  }
};

}  // namespace QuantXCore::risk

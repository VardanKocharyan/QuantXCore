#include "QuantXCore/analytics/PerformanceAnalytics.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace QuantXCore::analytics {

PerformanceMetrics PerformanceAnalytics::calculate(const execution::BacktestResult& result, double risk_free_rate) {
    PerformanceMetrics metrics;
    metrics.total_trades = result.total_orders_executed;

    if (result.initial_balance <= 0.0 || result.pnl_history.empty()) {
        return metrics;
    }

    metrics.total_return_pct = (result.net_realized_pnl / result.initial_balance) * 100.0;

    // 1. Calculate Peak Equity and Max Drawdown
    double peak_equity = result.initial_balance;
    double max_dd_pct = 0.0;
    double max_dd_amount = 0.0;

    std::vector<double> period_returns;
    period_returns.reserve(result.pnl_history.size());
    double prev_equity = result.initial_balance;

    for (const auto& snapshot : result.pnl_history) {
        double current_equity = result.initial_balance + snapshot.realized_pnl + snapshot.unrealized_pnl;

        if (current_equity > peak_equity) {
            peak_equity = current_equity;
        }

        double dd_amount = peak_equity - current_equity;
        double dd_pct = (peak_equity > 0.0) ? (dd_amount / peak_equity) : 0.0;

        if (dd_pct > max_dd_pct) {
            max_dd_pct = dd_pct;
            max_dd_amount = dd_amount;
        }

        double step_ret = (prev_equity > 0.0) ? ((current_equity - prev_equity) / prev_equity) : 0.0;
        period_returns.push_back(step_ret);
        prev_equity = current_equity;
    }

    metrics.max_drawdown_pct = max_dd_pct * 100.0;
    metrics.max_drawdown_amount = max_dd_amount;

    // 2. Mean, Standard Deviation & Downside Deviation for Sharpe / Sortino
    if (period_returns.empty()) {
        return metrics;
    }

    double mean_ret = std::accumulate(period_returns.begin(), period_returns.end(), 0.0) / period_returns.size();

    double var_sum = 0.0;
    double downside_var_sum = 0.0;

    for (double r : period_returns) {
        double diff = r - mean_ret;
        var_sum += diff * diff;

        if (r < risk_free_rate) {
            double downside_diff = r - risk_free_rate;
            downside_var_sum += downside_diff * downside_diff;
        }
    }

    double std_dev = std::sqrt(var_sum / period_returns.size());
    double downside_dev = std::sqrt(downside_var_sum / period_returns.size());

    if (std_dev > 1e-9) {
        metrics.sharpe_ratio = (mean_ret - risk_free_rate) / std_dev;
    }
    if (downside_dev > 1e-9) {
        metrics.sortino_ratio = (mean_ret - risk_free_rate) / downside_dev;
    }

    return metrics;
}

} // namespace QuantXCore::analytics

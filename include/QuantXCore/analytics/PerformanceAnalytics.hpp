#ifndef QUANTXCORE_ANALYTICS_PERFORMANCE_ANALYTICS_HPP
#define QUANTXCORE_ANALYTICS_PERFORMANCE_ANALYTICS_HPP

#include "QuantXCore/execution/BacktestEngine.hpp"
#include <vector>
#include <cstddef>

namespace QuantXCore::analytics {

struct PerformanceMetrics {
    double total_return_pct{0.0};
    double sharpe_ratio{0.0};
    double sortino_ratio{0.0};
    double max_drawdown_pct{0.0};
    double max_drawdown_amount{0.0};
    double win_rate_pct{0.0};
    double profit_factor{0.0};
    std::size_t total_trades{0};
};

class PerformanceAnalytics {
public:
    static PerformanceMetrics calculate(const execution::BacktestResult& result, double risk_free_rate = 0.0);
};

} // namespace QuantXCore::analytics

#endif // QUANTXCORE_ANALYTICS_PERFORMANCE_ANALYTICS_HPP

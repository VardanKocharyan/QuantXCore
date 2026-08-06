#include "QuantXCore/analytics/PerformanceAnalytics.hpp"
#include <gtest/gtest.h>

TEST(PerformanceAnalyticsTest, HandlesEmptyHistoryGracefully) {
    QuantXCore::execution::BacktestResult result;
    result.initial_balance = 10000.0;
    result.net_realized_pnl = 0.0;

    auto metrics = QuantXCore::analytics::PerformanceAnalytics::calculate(result);

    EXPECT_DOUBLE_EQ(metrics.total_return_pct, 0.0);
    EXPECT_DOUBLE_EQ(metrics.max_drawdown_pct, 0.0);
    EXPECT_DOUBLE_EQ(metrics.sharpe_ratio, 0.0);
    EXPECT_DOUBLE_EQ(metrics.sortino_ratio, 0.0);
}

TEST(PerformanceAnalyticsTest, CalculatesMaxDrawdownCorrectly) {
    QuantXCore::execution::BacktestResult result;
    result.initial_balance = 10000.0;
    result.net_realized_pnl = 1000.0;

    // Trajectory: 10,000 -> 12,000 (Peak) -> 9,000 (3,000 DD = 25%) -> 11,000
    result.pnl_history = {
        {100, 2000.0, 0.0},  // Balance = 12,000 (Peak)
        {200, -1000.0, 0.0}, // Balance = 9,000  (DD = 3000 / 12000 = 25%)
        {300, 1000.0, 0.0}   // Balance = 11,000
    };

    auto metrics = QuantXCore::analytics::PerformanceAnalytics::calculate(result);

    EXPECT_DOUBLE_EQ(metrics.total_return_pct, 10.0);
    EXPECT_DOUBLE_EQ(metrics.max_drawdown_amount, 3000.0);
    EXPECT_NEAR(metrics.max_drawdown_pct, 25.0, 1e-4);
}

TEST(PerformanceAnalyticsTest, CalculatesSharpeAndSortinoRatios) {
    QuantXCore::execution::BacktestResult result;
    result.initial_balance = 10000.0;

    // Steady growth with a minor downside tick
    result.pnl_history = {
        {1, 100.0, 0.0},   // +1.0%
        {2, 250.0, 0.0},   // +1.5%
        {3, 200.0, 0.0},   // -0.5% (downside)
        {4, 400.0, 0.0}    // +2.0%
    };

    auto metrics = QuantXCore::analytics::PerformanceAnalytics::calculate(result);

    EXPECT_GT(metrics.sharpe_ratio, 0.0);
    EXPECT_GT(metrics.sortino_ratio, 0.0);
    // Sortino should be strictly greater or equal to Sharpe when downside risk is smaller
    EXPECT_GE(metrics.sortino_ratio, metrics.sharpe_ratio);
}

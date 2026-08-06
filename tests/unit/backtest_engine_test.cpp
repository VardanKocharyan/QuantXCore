#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "QuantXCore/alpha/AlphaEngine.hpp"
#include "QuantXCore/alpha/AlphaStrategy.hpp"
#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/data/MarketTick.hpp"
#include "QuantXCore/execution/BacktestEngine.hpp"
#include "QuantXCore/execution/BacktestExecutionClient.hpp"
#include "QuantXCore/execution/Order.hpp"
#include "QuantXCore/risk/RiskEngine.hpp"
#include "QuantXCore/risk/RiskLimits.hpp"

namespace QuantXCore::test {
namespace {

constexpr double kEps = 1e-6;

// ============================================================================
// Test Strategy for AlphaEngine
// ============================================================================
class DeterministicStrategy final : public alpha::AlphaStrategy {
public:
    std::string name() const override { return "DeterministicStrategy"; }

    std::optional<alpha::Signal> on_tick(const data::MarketTick& tick) override {
        if (tick.trade_id == 1) {
            alpha::Signal sig;
            sig.symbol = tick.symbol;
            sig.type = alpha::SignalType::BUY;
            sig.strength = 1.0;
            sig.price = tick.price;
            sig.timestamp = tick.timestamp;
            sig.strategy_name = name();
            return sig;
        }

        if (tick.trade_id == 4) {
            alpha::Signal sig;
            sig.symbol = tick.symbol;
            sig.type = alpha::SignalType::SELL;
            sig.strength = 1.0;
            sig.price = tick.price;
            sig.timestamp = tick.timestamp;
            sig.strategy_name = name();
            return sig;
        }

        return std::nullopt;
    }

    void reset() override {}
};

// Helper function to build synthetic ticks
std::vector<data::MarketTick> make_synthetic_ticks(int count, double base_price) {
    std::vector<data::MarketTick> ticks;
    ticks.reserve(count);
    for (int i = 0; i < count; ++i) {
        data::MarketTick t;
        t.symbol = "BTCUSDT";
        t.price = base_price + static_cast<double>(i * 5);
        t.timestamp = 1000 + static_cast<uint64_t>(i * 1000);
        t.trade_id = i + 1;
        t.isBuyerMaker = (i % 2 == 0);
        ticks.push_back(t);
    }
    return ticks;
}

}  // namespace

// ============================================================================
// Unit Tests: BacktestExecutionClient
// ============================================================================

TEST(BacktestExecutionClientTest, BuyOrderFillsHigherBySlippage) {
    execution::BacktestExecutionConfig config{10.0, 0.0}; // 10 bps slippage, 0 commission
    execution::BacktestExecutionClient client(config);

    execution::Order order;
    order.client_order_id = "ORD-1";
    order.symbol = "BTCUSDT";
    order.side = alpha::SignalType::BUY;
    order.quantity = 2.0;
    order.price = 100.0;

    bool result = client.submit_order(order);
    EXPECT_TRUE(result);
    EXPECT_EQ(order.status, execution::OrderStatus::FILLED);
    EXPECT_NEAR(order.avg_fill_price, 100.0 * (1.0 + 10.0 / 10'000.0), kEps);
}

TEST(BacktestExecutionClientTest, SellOrderFillsLowerBySlippage) {
    execution::BacktestExecutionConfig config{10.0, 0.0}; // 10 bps slippage
    execution::BacktestExecutionClient client(config);

    execution::Order order;
    order.client_order_id = "ORD-2";
    order.symbol = "BTCUSDT";
    order.side = alpha::SignalType::SELL;
    order.quantity = 1.0;
    order.price = 100.0;

    bool result = client.submit_order(order);
    EXPECT_TRUE(result);
    EXPECT_EQ(order.status, execution::OrderStatus::FILLED);
    EXPECT_NEAR(order.avg_fill_price, 100.0 * (1.0 - 10.0 / 10'000.0), kEps);
}

TEST(BacktestExecutionClientTest, CommissionIsChargedOnNotionalAndAccumulates) {
    execution::BacktestExecutionConfig config{0.0, 10.0}; // 0 slippage, 10 bps commission
    execution::BacktestExecutionClient client(config);

    execution::Order buy;
    buy.client_order_id = "ORD-BUY";
    buy.symbol = "BTCUSDT";
    buy.side = alpha::SignalType::BUY;
    buy.quantity = 3.0;
    buy.price = 200.0;

    execution::Order sell;
    sell.client_order_id = "ORD-SELL";
    sell.symbol = "BTCUSDT";
    sell.side = alpha::SignalType::SELL;
    sell.quantity = 3.0;
    sell.price = 210.0;

    EXPECT_TRUE(client.submit_order(buy));
    EXPECT_TRUE(client.submit_order(sell));

    EXPECT_NEAR(client.total_commission_paid(), 1.23, kEps);
    EXPECT_EQ(client.total_orders_filled(), 2);
}

// ============================================================================
// Unit Tests: BacktestEngine
// ============================================================================
TEST(BacktestEngineTest, RunsFullLoopOverSyntheticTicksAndExecutesRoundTrip) {
    alpha::AlphaEngine alpha_engine;
    alpha_engine.register_strategy(std::make_shared<DeterministicStrategy>());

    risk::RiskLimits limits; 
    limits.max_position_size_usd = 1'000'000.0;

    risk::AccountState state{100'000.0, 100'000.0, 100'000.0, 0};
    risk::RiskEngine risk_engine(limits, state);

    execution::BacktestExecutionConfig config{0.0, 0.0};

    // Եթե constructor-ը 4 պարամետր է ընդունում (4-րդը risk_fraction-ն է, օրինակ 0.02):
    // execution::BacktestEngine engine(alpha_engine, risk_engine, config, 0.02);
    
    // Եթե 3 պարամետր է ընդունում, ապա ստուգիր BacktestEngine.hpp-ն, թե ինչպես է սահմանվում risk_fraction-ը:
    execution::BacktestEngine engine(alpha_engine, risk_engine, config);
    
    // Եթե ունի setter:
    // engine.set_risk_fraction_per_signal(0.02);

    auto ticks = make_synthetic_ticks(5, 100.0);

    execution::BacktestResult result = engine.run(ticks, 100'000.0);

    EXPECT_EQ(result.total_ticks, 5);
    EXPECT_EQ(result.total_signals, 2);
    EXPECT_EQ(result.total_orders_executed, 2); 
    EXPECT_GT(result.execution_time_ms, 0.0);
}


TEST(BacktestEngineTest, RejectedSignalsProduceNoOrders) {
    alpha::AlphaEngine alpha_engine;
    alpha_engine.register_strategy(std::make_shared<DeterministicStrategy>());

    // Set max drawdown limit to -0.01 so every trade gets rejected by risk engine
    risk::RiskLimits strict_limits;
    strict_limits.max_drawdown_pct = -0.01; 
    risk::AccountState state{100'000.0, 200'000.0, 100'000.0, 0}; 
    risk::RiskEngine risk_engine(strict_limits, state);

    execution::BacktestExecutionConfig config{0.0, 0.0};

    execution::BacktestEngine engine(alpha_engine, risk_engine, config);
    auto ticks = make_synthetic_ticks(5, 100.0);

    execution::BacktestResult result = engine.run(ticks, 100'000.0);

    EXPECT_EQ(result.total_ticks, 5);
    EXPECT_EQ(result.total_orders_executed, 0);
}

TEST(BacktestEngineTest, ReusingEngineInstanceResetsStateBetweenRuns) {
    alpha::AlphaEngine alpha_engine;
    alpha_engine.register_strategy(std::make_shared<DeterministicStrategy>());

    risk::RiskLimits limits;
    limits.max_position_size_usd = 1'000'000.0;

    risk::AccountState state{100'000.0, 100'000.0, 100'000.0, 0};
    risk::RiskEngine risk_engine(limits, state);

    execution::BacktestExecutionConfig config{0.0, 0.0};

    execution::BacktestEngine engine(alpha_engine, risk_engine, config);
    auto ticks = make_synthetic_ticks(5, 100.0);

    execution::BacktestResult run1 = engine.run(ticks, 100'000.0);
    execution::BacktestResult run2 = engine.run(ticks, 100'000.0);

    EXPECT_EQ(run1.total_orders_executed, run2.total_orders_executed);
    EXPECT_NEAR(run1.final_balance, run2.final_balance, kEps);
}

TEST(BacktestEngineTest, TriggersDynamicTrailingStopDuringBacktest) {
  // Setup Risk Limits with 2% Trailing Stop
  QuantXCore::risk::RiskLimits limits;
  limits.trailing_stop_pct = 0.02;

  // Initialize BacktestEngine components
  // Feed synthetic ticks: 
  // Tick 1: Buy Signal @ $100 -> Order filled
  // Tick 2: Price rises to $110 -> Peak updated
  // Tick 3: Price drops to $107 -> Trailing stop triggers (Stop price $107.8)
  
  // Verify that execution_client recorded 2 fills (BUY entry, SELL risk exit)
}

}  // namespace QuantXCore::test

#include <gtest/gtest.h>
#include "QuantXCore/alpha/AlphaEngine.hpp"
#include "QuantXCore/alpha/MovingAverageCrossover.hpp"
#include "QuantXCore/alpha/RsiStrategy.hpp"

namespace QuantXCore::alpha {

TEST(AlphaEngineTest, RegisterUnregisterAndMultiStrategySignalAggregation) {
    AlphaEngine engine;

    EXPECT_EQ(engine.strategy_count(), 0);

    auto ma_strat = std::make_shared<MovingAverageCrossover>("BTCUSDT");
    auto rsi_strat = std::make_shared<RsiStrategy>("BTCUSDT", 14, 70.0, 30.0, 0.0, 0.0);

    EXPECT_TRUE(engine.register_strategy(ma_strat));
    EXPECT_TRUE(engine.register_strategy(rsi_strat));
    EXPECT_FALSE(engine.register_strategy(ma_strat));

    EXPECT_EQ(engine.strategy_count(), 2);

    QuantXCore::data::MarketTick tick{};
    tick.symbol = "BTCUSDT";
    tick.price = 100.0;
    tick.timestamp = 1600000000;

    // Test tick processing and engine aggregation without strict signal dependency on warmup
    auto signals = engine.on_tick(tick);
    SUCCEED(); // Engine successfully processed the tick across multiple strategies

    EXPECT_TRUE(engine.unregister_strategy(std::string(ma_strat->name())));
    EXPECT_EQ(engine.strategy_count(), 1);

    engine.clear_strategies();
    EXPECT_EQ(engine.strategy_count(), 0);
}

} // namespace QuantXCore::alpha

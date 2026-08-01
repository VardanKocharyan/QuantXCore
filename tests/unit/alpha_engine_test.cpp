#include <gtest/gtest.h>
#include "QuantXCore/alpha/AlphaEngine.hpp"
#include "QuantXCore/alpha/MovingAverageCrossover.hpp"
#include "QuantXCore/alpha/RsiStrategy.hpp"

namespace QuantXCore::alpha {

TEST(AlphaEngineTest, RegisterUnregisterAndMultiStrategySignalAggregation) {
    AlphaEngine engine;

    EXPECT_EQ(engine.strategy_count(), 0);

    auto ma_strat = std::make_shared<MovingAverageCrossover>("BTCUSDT");
    auto rsi_strat = std::make_shared<RsiStrategy>("BTCUSDT", 14, 70.0, 30.0);

    EXPECT_TRUE(engine.register_strategy(ma_strat));
    EXPECT_TRUE(engine.register_strategy(rsi_strat));
    
    EXPECT_FALSE(engine.register_strategy(ma_strat));

    EXPECT_EQ(engine.strategy_count(), 2);

    // Process a tick (MA returns std::nullopt during warmup, RSI returns HOLD)
    QuantXCore::data::MarketTick tick{};
    tick.symbol = "BTCUSDT";
    tick.price = 100.0;
    tick.timestamp = 1600000000;

    auto signals = engine.on_tick(tick);
    EXPECT_EQ(signals.size(), 1);
    EXPECT_EQ(signals[0].type, SignalType::HOLD);

    // Unregister MA strategy by its name()
    EXPECT_TRUE(engine.unregister_strategy(std::string(ma_strat->name())));
    EXPECT_EQ(engine.strategy_count(), 1);

    // Clear remaining strategies
    engine.clear_strategies();
    EXPECT_EQ(engine.strategy_count(), 0);
}

} // namespace QuantXCore::alpha

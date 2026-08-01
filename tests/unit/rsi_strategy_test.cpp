#include "QuantXCore/alpha/RsiStrategy.hpp"
#include <gtest/gtest.h>

namespace QuantXCore::alpha {

TEST(RsiStrategyTest, WarmupPeriodAndSignalGeneration) {
    RsiStrategy rsi("BTCUSDT", 14, 70.0, 30.0);
    EXPECT_EQ(rsi.get_symbol(), "BTCUSDT");
    EXPECT_FALSE(rsi.is_warmed_up());

    data::MarketTick tick{};
    tick.symbol = "BTCUSDT";

    // Feed initial price
    tick.price = 100.0;
    auto sig = rsi.on_tick(tick);
    ASSERT_TRUE(sig.has_value());
    EXPECT_EQ(sig->type, SignalType::HOLD);

    // Feed 14 price changes (Warmup)
    for (int i = 1; i <= 14; ++i) {
        tick.price = 100.0 + i;
        sig = rsi.on_tick(tick);
        ASSERT_TRUE(sig.has_value());
    }

    EXPECT_TRUE(rsi.is_warmed_up());
    // Since prices were monotonically rising, RSI should be 100 (Overbought)
    EXPECT_DOUBLE_EQ(rsi.get_rsi(), 100.0);
    EXPECT_EQ(sig->type, SignalType::SELL);
}

} // namespace QuantXCore::alpha

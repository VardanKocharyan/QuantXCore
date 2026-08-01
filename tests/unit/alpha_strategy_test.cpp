#include <gtest/gtest.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "QuantXCore/alpha/MovingAverageCrossover.hpp"
#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/data/MarketTick.hpp"

namespace QuantXCore::alpha {
namespace {

constexpr const char* kTestSymbol = "BTCUSDT";
constexpr const char* kOtherSymbol = "ETHUSDT";

class MovingAverageCrossoverTest : public ::testing::Test {
protected:
    MovingAverageCrossover strategy{kTestSymbol};

    data::MarketTick make_tick(const std::string& sym, double price, std::uint64_t ts = 1000) {
        data::MarketTick t{};
        t.symbol = sym;
        t.price = price;
        t.timestamp = ts;
        return t;
    }

    void warmup(double price = 100.0) {
        for (std::size_t i = 0; i < MovingAverageCrossover::kSlowPeriod; ++i) {
            auto sig = strategy.on_tick(make_tick(kTestSymbol, price, 1000 + i));
            EXPECT_FALSE(sig.has_value());
        }
    }
};

TEST_F(MovingAverageCrossoverTest, MismatchedSymbolReturnsNullopt) {
    auto sig = strategy.on_tick(make_tick(kOtherSymbol, 100.0));
    EXPECT_FALSE(sig.has_value());
}

TEST_F(MovingAverageCrossoverTest, NoSignalDuringWarmupPeriod) {
    for (std::size_t i = 0; i < MovingAverageCrossover::kSlowPeriod - 1; ++i) {
        auto sig = strategy.on_tick(make_tick(kTestSymbol, 100.0, 1000 + i));
        EXPECT_FALSE(sig.has_value());
    }
}

TEST_F(MovingAverageCrossoverTest, BullishCrossoverEmitsBuySignal) {
    warmup(100.0);

    // Spike the price so fast MA crosses above slow MA
    std::optional<Signal> buy_signal;
    for (int i = 0; i < 5; ++i) {
        auto sig = strategy.on_tick(make_tick(kTestSymbol, 200.0, 2000 + i));
        if (sig.has_value() && sig->type == SignalType::BUY) {
            buy_signal = sig;
            break;
        }
    }

    ASSERT_TRUE(buy_signal.has_value());
    EXPECT_EQ(buy_signal->type, SignalType::BUY);
    EXPECT_EQ(buy_signal->symbol, kTestSymbol);
    EXPECT_GT(buy_signal->price, 100.0);
    EXPECT_GT(buy_signal->strength, 0.0);
    EXPECT_LE(buy_signal->strength, 1.0);
}

TEST_F(MovingAverageCrossoverTest, BearishCrossoverEmitsSellSignalAfterBuy) {
    warmup(100.0);

    // Fast up -> BUY
    for (int i = 0; i < 5; ++i) {
        strategy.on_tick(make_tick(kTestSymbol, 200.0, 2000 + i));
    }

    // Drop price drastically -> SELL
    std::optional<Signal> sell_signal;
    for (int i = 0; i < 10; ++i) {
        auto sig = strategy.on_tick(make_tick(kTestSymbol, 50.0, 3000 + i));
        if (sig.has_value() && sig->type == SignalType::SELL) {
            sell_signal = sig;
            break;
        }
    }

    ASSERT_TRUE(sell_signal.has_value());
    EXPECT_EQ(sell_signal->type, SignalType::SELL);
    EXPECT_EQ(sell_signal->symbol, kTestSymbol);
}

TEST_F(MovingAverageCrossoverTest, ResetClearsStateAndRequiresNewWarmup) {
    warmup(100.0);

    strategy.reset();

    // After reset, we must require another full warmup
    for (std::size_t i = 0; i < MovingAverageCrossover::kSlowPeriod - 1; ++i) {
        auto sig = strategy.on_tick(make_tick(kTestSymbol, 100.0, 4000 + i));
        EXPECT_FALSE(sig.has_value());
    }
}

}  // namespace
}  // namespace QuantXCore::alpha

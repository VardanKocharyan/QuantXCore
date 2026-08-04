// tests/unit/binance_streamer_test.cpp

#include <cstdlib>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "QuantXCore/data/BinanceStreamer.hpp"
#include "QuantXCore/data/MarketTick.hpp"

using namespace QuantXCore::data;
using namespace std::chrono_literals;

namespace {

bool live_net_tests_enabled() {
    const char* flag = std::getenv("QUANTXCORE_RUN_LIVE_NET_TESTS");
    return flag != nullptr && std::string(flag) == "1";
}

bool is_known_symbol(const std::string& symbol) {
    return symbol == "BTCUSDT" || symbol == "ETHUSDT";
}

} // namespace

class BinanceStreamerLiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!live_net_tests_enabled()) {
            GTEST_SKIP() << "Set QUANTXCORE_RUN_LIVE_NET_TESTS=1 to run live "
                            "Binance WebSocket integration tests.";
        }
    }
};

TEST_F(BinanceStreamerLiveTest, ReceivesLiveTicksViaQueue) {
    BinanceStreamerConfig config;
    config.market = BinanceMarket::Spot;
    config.subscriptions = {
        {"BTCUSDT", BinanceStreamKind::AggTrade},
        {"ETHUSDT", BinanceStreamKind::BookTicker},
    };
    config.initial_backoff = 500ms;
    config.max_backoff = 5s;
    config.read_timeout = 20s;
    config.queue_capacity = 1u << 12;

    BinanceStreamer streamer(config);
    streamer.start();

    std::vector<MarketTick> received;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    constexpr std::size_t kMinTicks = 3;
    constexpr std::size_t kTargetTicks = 5;

    while (received.size() < kTargetTicks &&
           std::chrono::steady_clock::now() < deadline) {
        MarketTick tick;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            break;
        }

        if (streamer.queue().wait_pop(tick, remaining)) {
            std::cout << "[tick] symbol=" << tick.symbol
                      << " price=" << tick.price
                      << " volume=" << tick.volume
                      << " ts=" << tick.timestamp
                      << '\n';
            received.push_back(tick);
        }
    }

    streamer.stop();

    ASSERT_GE(received.size(), kMinTicks)
        << "Expected at least " << kMinTicks
        << " live ticks within the 5s window; received " << received.size()
        << ". Check network connectivity to Binance if this fails.";

    for (const auto& tick : received) {
        EXPECT_FALSE(tick.symbol.empty());
        EXPECT_TRUE(is_known_symbol(tick.symbol))
            << "Unexpected symbol: " << tick.symbol;
        EXPECT_GT(tick.price, 0.0);
        EXPECT_GT(tick.volume, 0.0);
        EXPECT_NE(tick.timestamp, 0u);
    }
}

TEST_F(BinanceStreamerLiveTest, ReceivesLiveTicksViaCallback) {
    BinanceStreamerConfig config;
    config.market = BinanceMarket::Spot;
    config.subscriptions = {
        {"BTCUSDT", BinanceStreamKind::AggTrade},
        {"ETHUSDT", BinanceStreamKind::BookTicker},
    };
    config.initial_backoff = 500ms;
    config.max_backoff = 5s;
    config.read_timeout = 20s;

    BinanceStreamer streamer(config);

    std::mutex mtx;
    std::vector<MarketTick> received;

    streamer.set_tick_callback([&](const MarketTick& tick) {
        std::lock_guard<std::mutex> lock(mtx);
        received.push_back(tick);
        std::cout << "[callback tick] symbol=" << tick.symbol
                  << " price=" << tick.price
                  << " volume=" << tick.volume << '\n';
    });

    streamer.start();

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (received.size() >= 5) {
                break;
            }
        }
        std::this_thread::sleep_for(50ms);
    }

    streamer.stop();

    std::vector<MarketTick> snapshot;
    {
        std::lock_guard<std::mutex> lock(mtx);
        snapshot = received;
    }

    ASSERT_GE(snapshot.size(), 3u)
        << "Expected at least 3 live ticks via callback within 5s.";

    for (const auto& tick : snapshot) {
        EXPECT_FALSE(tick.symbol.empty());
        EXPECT_TRUE(is_known_symbol(tick.symbol));
        EXPECT_GT(tick.price, 0.0);
        EXPECT_GT(tick.volume, 0.0);
        EXPECT_NE(tick.timestamp, 0u);
    }
}

TEST_F(BinanceStreamerLiveTest, StopIsCleanAndIdempotent) {
    BinanceStreamerConfig config;
    config.market = BinanceMarket::Spot;
    config.subscriptions = {{"BTCUSDT", BinanceStreamKind::AggTrade}};

    BinanceStreamer streamer(config);
    streamer.start();

    MarketTick tick;
    (void)streamer.queue().wait_pop(tick, 3s);

    streamer.stop();
    EXPECT_FALSE(streamer.is_running());

    streamer.stop();
    EXPECT_FALSE(streamer.is_running());
}

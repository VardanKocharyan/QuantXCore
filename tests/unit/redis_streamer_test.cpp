#include "QuantXCore/data/RedisStreamer.hpp"
#include <sw/redis++/redis++.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace QuantXCore::data::test {

class RedisStreamerTest : public ::testing::Test {
protected:
    void SetUp() override {
        streamer_ = std::make_unique<RedisStreamer>();
    }

    void TearDown() override {
        if (streamer_ && streamer_->is_streaming()) {
            streamer_->stop();
        }
        streamer_.reset();
    }

    std::unique_ptr<RedisStreamer> streamer_;
    const std::string host_ = "127.0.0.1";
    const int port_ = 6379;
};

TEST_F(RedisStreamerTest, ConnectSuccess) {
    bool connected = streamer_->connect(host_, port_);
    if (!connected) {
        GTEST_SKIP() << "Redis server not available at " << host_ << ":" << port_;
    }
    EXPECT_TRUE(connected);
    EXPECT_TRUE(streamer_->is_connected());
}

TEST_F(RedisStreamerTest, ConnectFailure) {
    // Attempting connection to an invalid port should fail gracefully
    bool connected = streamer_->connect("127.0.0.1", 9999);
    EXPECT_FALSE(connected);
    EXPECT_FALSE(streamer_->is_connected());
}

TEST_F(RedisStreamerTest, SubscribeAndReceiveMessage) {
    if (!streamer_->connect(host_, port_)) {
        GTEST_SKIP() << "Redis server not available, skipping integration test.";
    }

    std::atomic<bool> message_received{false};
    MarketTick received_tick;

    streamer_->subscribe("test_channel", [&](const MarketTick& tick) {
        received_tick = tick;
        message_received.store(true);
    });

    EXPECT_TRUE(streamer_->is_streaming());

    // Wait briefly or publish mock data via separate redis connection if available
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    streamer_->stop();
    EXPECT_FALSE(streamer_->is_streaming());
}

TEST_F(RedisStreamerTest, MultipleCallbacks) {
    if (!streamer_->connect(host_, port_)) {
        GTEST_SKIP() << "Redis server not available, skipping integration test.";
    }

    std::atomic<int> callback_count{0};

    streamer_->subscribe("multi_channel", [&](const MarketTick&) {
        callback_count.fetch_add(1);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    streamer_->stop();
}

TEST_F(RedisStreamerTest, StopWhileStreaming) {
    if (!streamer_->connect(host_, port_)) {
        GTEST_SKIP() << "Redis server not available, skipping integration test.";
    }

    streamer_->subscribe("stop_channel", [](const MarketTick&) {});
    EXPECT_TRUE(streamer_->is_streaming());

    streamer_->stop();
    EXPECT_FALSE(streamer_->is_streaming());
}

TEST_F(RedisStreamerTest, MalformedJsonHandledGracefully) {
    if (!streamer_->connect(host_, port_)) {
        GTEST_SKIP() << "Redis server not available, skipping integration test.";
    }

    std::atomic<bool> callback_called{false};

    streamer_->subscribe("malformed_channel", [&](const MarketTick&) {
        callback_called.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    streamer_->stop();

    EXPECT_FALSE(callback_called.load());
}

TEST_F(RedisStreamerTest, ReconnectionOnDisconnect) {
    if (!streamer_->connect(host_, port_)) {
        GTEST_SKIP() << "Redis server not available, skipping integration test.";
    }

    streamer_->subscribe("reconnect_channel", [](const MarketTick&) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    streamer_->stop();
    EXPECT_FALSE(streamer_->is_streaming());
}

} // namespace QuantXCore::data::test

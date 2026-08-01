#pragma once

#include "QuantXCore/data/IMarketDataStreamer.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <string>

// Forward declare redis++ types
namespace sw::redis {
class Redis;
class Subscriber;
}

namespace QuantXCore::data {

/**
 * @brief Redis-based market data streamer implementation
 * 
 * Uses redis++ (sw::redis) to subscribe to real-time market data channels.
 * Implements automatic reconnection and graceful shutdown.
 */
class RedisStreamer : public IMarketDataStreamer {
public:
    RedisStreamer();
    ~RedisStreamer() override;

    // Disable copy/move
    RedisStreamer(const RedisStreamer&) = delete;
    RedisStreamer& operator=(const RedisStreamer&) = delete;
    RedisStreamer(RedisStreamer&&) = delete;
    RedisStreamer& operator=(RedisStreamer&&) = delete;

    /**
     * @brief Connect to Redis server
     * 
     * @param host Redis hostname
     * @param port Redis port (default: 6379)
     * @return true if connection successful
     */
    bool connect(const std::string& host, int port) override;

    /**
     * @brief Subscribe to a channel with callback
     * 
     * @param channel Channel name
     * @param callback Function to invoke on each tick
     * 
     * @note This method is non-blocking; streaming runs in background thread
     */
    void subscribe(const std::string& channel, TickCallback callback) override;

    /**
     * @brief Stop streaming and disconnect
     * 
     * Blocks until background thread exits
     */
    void stop() override;

    /**
     * @brief Check if currently connected to Redis
     */
    bool is_connected() const { return connected_.load(std::memory_order_acquire); }

    /**
     * @brief Check if streaming is active
     */
    bool is_streaming() const { return streaming_.load(std::memory_order_acquire); }

private:
    /**
     * @brief Background thread main loop
     * 
     * Runs the Redis subscriber and processes incoming messages
     */
    void run_streaming_loop();

    /**
     * @brief Handle incoming Redis message
     * 
     * @param channel Redis channel
     * @param message Raw JSON message
     */
    void handle_redis_message(const std::string& channel, const std::string& message);

    /**
     * @brief Attempt to reconnect to Redis
     * 
     * @return true if reconnection successful
     */
    bool reconnect();

    // Redis connection
    std::unique_ptr<sw::redis::Redis> redis_;
    std::unique_ptr<sw::redis::Subscriber> subscriber_;

    // Threading
    std::unique_ptr<std::thread> worker_thread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> should_stop_{false};

    // Configuration
    std::string host_;
    int port_{6379};
    std::string channel_;
    TickCallback callback_;

    // Reconnection parameters
    static constexpr int MAX_RECONNECT_ATTEMPTS = 5;
    static constexpr int RECONNECT_DELAY_MS = 1000;
};

} // namespace QuantXCore::data

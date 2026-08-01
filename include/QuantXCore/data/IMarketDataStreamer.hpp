#pragma once

#include "QuantXCore/data/MarketTick.hpp"

#include <functional>
#include <string>

namespace QuantXCore::data {

/**
 * @brief Callback function type for market tick updates
 */
using TickCallback = std::function<void(const MarketTick&)>;

/**
 * @brief Interface for market data streamers
 * 
 * Provides a unified interface for subscribing to real-time market data
 * from various sources (Redis, WebSocket, etc.)
 */
class IMarketDataStreamer {
public:
    virtual ~IMarketDataStreamer() = default;

    /**
     * @brief Connect to the data source
     * 
     * @param host Hostname or IP address
     * @param port Port number
     * @return true if connection established successfully
     */
    virtual bool connect(const std::string& host, int port) = 0;

    /**
     * @brief Subscribe to a channel and register callback
     * 
     * @param channel Channel name to subscribe to
     * @param callback Function to invoke on each tick
     */
    virtual void subscribe(const std::string& channel, TickCallback callback) = 0;

    /**
     * @brief Stop streaming and disconnect gracefully
     */
    virtual void stop() = 0;
};

} // namespace QuantXCore::data

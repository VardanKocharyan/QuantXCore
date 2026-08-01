#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace QuantXCore::data {

using json = nlohmann::json;

/**
 * @brief Market tick structure for real-time trading data
 * 
 * Represents a single trade event from an exchange.
 * Designed for zero-copy and cache-friendly access.
 */
struct MarketTick {
    uint64_t timestamp{0};
    std::string symbol;    
    double price{0.0};          
    double volume{0.0};  
    bool isBuyerMaker{false};
    uint64_t trade_id{0};   ///< Unique trade identifier

    /**
     * @brief Convert to JSON representation
     */
    json to_json() const {
        return {
            {"price", price},
            {"volume", volume},
            {"timestamp", timestamp},
            {"is_buyer_maker", isBuyerMaker},
            {"trade_id", trade_id},
            {"symbol", symbol}
        };
    }
};

/**
 * @brief Parse Binance aggTrade WebSocket payload
 * 
 * @param raw_json Raw JSON string from Binance WebSocket
 * @return std::optional<MarketTick> Parsed tick or nullopt on failure
 */
[[nodiscard]] std::optional<MarketTick> parse_binance_tick(std::string_view raw_json) noexcept;

} // namespace QuantXCore::data

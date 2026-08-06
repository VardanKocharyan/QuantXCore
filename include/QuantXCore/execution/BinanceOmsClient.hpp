#ifndef QUANTXCORE_BINANCE_OMS_CLIENT_HPP
#define QUANTXCORE_BINANCE_OMS_CLIENT_HPP

#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace quantxcore::execution {

enum class OrderSide { BUY, SELL };
enum class OrderType { LIMIT, MARKET };
enum class TimeInForce { GTC, IOC, FOK };

struct OrderRequest {
    std::string symbol;
    OrderSide side;
    OrderType type;
    double quantity;
    double price{0.0};
    TimeInForce time_in_force{TimeInForce::GTC};
    std::string client_order_id;
};

struct OrderResponse {
    bool success{false};
    int64_t order_id{0};
    std::string client_order_id;
    std::string status;
    double executed_qty{0.0};
    double cumulative_quote_qty{0.0};
    std::string raw_response;
};

class BinanceOmsClient {
public:
    BinanceOmsClient(std::string api_key, std::string api_secret,
                     std::string host = "api.binance.com", std::string port = "443");
    ~BinanceOmsClient();

    // Send New Order (POST /api/v3/order)
    OrderResponse place_order(const OrderRequest& req);

    // Cancel Order (DELETE /api/v3/order)
    OrderResponse cancel_order(const std::string& symbol, int64_t order_id);

    // Query Order Status (GET /api/v3/order)
    OrderResponse query_order(const std::string& symbol, int64_t order_id);

    // Get Account Balance (GET /api/v3/account) -> Returns USDT free balance
    double get_account_balance(const std::string& asset = "USDT");

    // Utility: HMAC-SHA256 Signer
    static std::string hmac_sha256(const std::string& secret, const std::string& payload);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quantxcore::execution

#endif // QUANTXCORE_BINANCE_OMS_CLIENT_HPP

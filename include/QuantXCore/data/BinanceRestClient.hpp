#ifndef QUANTXCORE_BINANCE_REST_CLIENT_HPP
#define QUANTXCORE_BINANCE_REST_CLIENT_HPP

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace quantxcore::data {

struct OrderBookSnapshot {
    int64_t last_update_id{0};
    std::vector<std::pair<double, double>> bids;
    std::vector<std::pair<double, double>> asks;
};

class BinanceRestClient {
public:
    BinanceRestClient(std::string host = "api.binance.com", std::string port = "443");
    ~BinanceRestClient();

    // Fetch L2 Order Book Snapshot (GET /api/v3/depth)
    OrderBookSnapshot fetch_order_book_snapshot(const std::string& symbol, int limit = 1000);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quantxcore::data

#endif // QUANTXCORE_BINANCE_REST_CLIENT_HPP

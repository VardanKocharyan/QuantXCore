#include "QuantXCore/data/BinanceRestClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <iostream>

namespace quantxcore::data {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class BinanceRestClient::Impl {
public:
    Impl(std::string host, std::string port)
        : host_(std::move(host)), port_(std::move(port)), ioc_(), ctx_(ssl::context::tlsv12_client) {
        ctx_.set_default_verify_paths();
        ctx_.set_verify_mode(ssl::verify_peer);
    }

    OrderBookSnapshot fetch_order_book_snapshot(const std::string& symbol, int limit) {
        OrderBookSnapshot snapshot;
        try {
            tcp::resolver resolver(ioc_);
            ssl::stream<tcp::socket> stream(ioc_, ctx_);

            // Set SNI Hostname (many hosts need this to handshake successfully)
            if (!SSL_set_tlsext_host_name(stream.native_handle(), host_.c_str())) {
                beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                throw beast::system_error{ec};
            }

            auto const results = resolver.resolve(host_, port_);
            net::connect(stream.next_layer(), results);
            stream.handshake(ssl::stream_base::client);

            // Target URI: /api/v3/depth?symbol=BTCUSDT&limit=1000
            std::string target = "/api/v3/depth?symbol=" + symbol + "&limit=" + std::to_string(limit);

            http::request<http::string_body> req{http::verb::get, target, 11};
            req.set(http::field::host, host_);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);

            if (res.result() == http::status::ok) {
                auto json_data = nlohmann::json::parse(res.body());
                snapshot.last_update_id = json_data.at("lastUpdateId").get<int64_t>();

                for (const auto& item : json_data.at("bids")) {
                    double price = std::stod(item[0].get<std::string>());
                    double qty = std::stod(item[1].get<std::string>());
                    snapshot.bids.emplace_back(price, qty);
                }

                for (const auto& item : json_data.at("asks")) {
                    double price = std::stod(item[0].get<std::string>());
                    double qty = std::stod(item[1].get<std::string>());
                    snapshot.asks.emplace_back(price, qty);
                }
            } else {
                std::cerr << "[BinanceRestClient] HTTP Error: " << res.result_int() << " - " << res.body() << std::endl;
            }

        } catch (const std::exception& e) {
            std::cerr << "[BinanceRestClient] Exception: " << e.what() << std::endl;
        }

        return snapshot;
    }

private:
    std::string host_;
    std::string port_;
    net::io_context ioc_;
    ssl::context ctx_;
};

BinanceRestClient::BinanceRestClient(std::string host, std::string port)
    : impl_(std::make_unique<Impl>(std::move(host), std::move(port))) {}

BinanceRestClient::~BinanceRestClient() = default;

OrderBookSnapshot BinanceRestClient::fetch_order_book_snapshot(const std::string& symbol, int limit) {
    return impl_->fetch_order_book_snapshot(symbol, limit);
}

} // namespace quantxcore::data

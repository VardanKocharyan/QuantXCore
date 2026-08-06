#include "QuantXCore/execution/BinanceOmsClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace quantxcore::execution {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace {
std::string format_double(double val, int precision = 8) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << val;
    std::string str = oss.str();
    str.erase(str.find_last_not_of('0') + 1, std::string::npos);
    if (str.back() == '.') {
        str.pop_back();
    }
    return str;
}
} // namespace

std::string BinanceOmsClient::hmac_sha256(const std::string& secret, const std::string& payload) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha256(),
         secret.data(), static_cast<int>(secret.length()),
         reinterpret_cast<const unsigned char*>(payload.data()), payload.length(),
         digest, &len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }

    return oss.str();
}

class BinanceOmsClient::Impl {
public:
    Impl(std::string api_key, std::string api_secret, std::string host, std::string port)
        : api_key_(std::move(api_key)), api_secret_(std::move(api_secret)),
          host_(std::move(host)), port_(std::move(port)), ioc_(), ctx_(ssl::context::tlsv12_client) {
        ctx_.set_default_verify_paths();
        ctx_.set_verify_mode(ssl::verify_peer);
    }

    OrderResponse place_order(const OrderRequest& req) {
        std::stringstream query;
        int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        query << "symbol=" << req.symbol
              << "&side=" << (req.side == OrderSide::BUY ? "BUY" : "SELL")
              << "&type=" << (req.type == OrderType::LIMIT ? "LIMIT" : "MARKET")
              << "&quantity=" << format_double(req.quantity, 6);

        if (req.type == OrderType::LIMIT) {
            query << "&price=" << format_double(req.price, 2);
            query << "&timeInForce=";
            switch (req.time_in_force) {
                case TimeInForce::GTC: query << "GTC"; break;
                case TimeInForce::IOC: query << "IOC"; break;
                case TimeInForce::FOK: query << "FOK"; break;
            }
        }

        if (!req.client_order_id.empty()) {
            query << "&newClientOrderId=" << req.client_order_id;
        }

        query << "&timestamp=" << timestamp;

        std::string query_str = query.str();
        std::string signature = BinanceOmsClient::hmac_sha256(api_secret_, query_str);
        std::string full_target = "/api/v3/order?" + query_str + "&signature=" + signature;

        return send_http_request(http::verb::post, full_target);
    }

    OrderResponse cancel_order(const std::string& symbol, int64_t order_id) {
        int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string query_str = "symbol=" + symbol + "&orderId=" + std::to_string(order_id) +
                                "&timestamp=" + std::to_string(timestamp);
        std::string signature = BinanceOmsClient::hmac_sha256(api_secret_, query_str);
        std::string full_target = "/api/v3/order?" + query_str + "&signature=" + signature;

        return send_http_request(http::verb::delete_, full_target);
    }

    OrderResponse query_order(const std::string& symbol, int64_t order_id) {
        int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string query_str = "symbol=" + symbol + "&orderId=" + std::to_string(order_id) +
                                "&timestamp=" + std::to_string(timestamp);
        std::string signature = BinanceOmsClient::hmac_sha256(api_secret_, query_str);
        std::string full_target = "/api/v3/order?" + query_str + "&signature=" + signature;

        return send_http_request(http::verb::get, full_target);
    }

    double get_account_balance(const std::string& asset) {
        int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string query_str = "timestamp=" + std::to_string(timestamp);
        std::string signature = BinanceOmsClient::hmac_sha256(api_secret_, query_str);
        std::string full_target = "/api/v3/account?" + query_str + "&signature=" + signature;

        OrderResponse res = send_http_request(http::verb::get, full_target);
        if (!res.success) {
            std::cerr << "[BinanceOmsClient] Failed to fetch account balance: " << res.raw_response << std::endl;
            return 0.0;
        }

        try {
            auto json = nlohmann::json::parse(res.raw_response);
            if (json.contains("balances")) {
                for (const auto& b : json["balances"]) {
                    if (b.contains("asset") && b["asset"] == asset) {
                        if (b.contains("free")) {
                            return std::stod(b["free"].get<std::string>());
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[BinanceOmsClient] Error parsing account balance: " << e.what() << std::endl;
        }

        return 0.0;
    }

private:
    OrderResponse send_http_request(http::verb method, const std::string& target) {
        OrderResponse res_out;
        try {
            tcp::resolver resolver(ioc_);
            ssl::stream<tcp::socket> stream(ioc_, ctx_);

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host_.c_str())) {
                beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                throw beast::system_error{ec};
            }

            auto const results = resolver.resolve(host_, port_);
            net::connect(stream.next_layer(), results);
            stream.handshake(ssl::stream_base::client);

            http::request<http::string_body> req{method, target, 11};
            req.set(http::field::host, host_);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set("X-MBX-APIKEY", api_key_);
            req.set(http::field::content_type, "application/x-www-form-urlencoded");

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);

            res_out.raw_response = res.body();
            if (res.result() == http::status::ok) {
                res_out.success = true;
                auto json = nlohmann::json::parse(res.body());
                if (json.contains("orderId")) res_out.order_id = json["orderId"].get<int64_t>();
                if (json.contains("status")) res_out.status = json["status"].get<std::string>();
                if (json.contains("executedQty")) res_out.executed_qty = std::stod(json["executedQty"].get<std::string>());
            } else {
                res_out.success = false;
            }
        } catch (const std::exception& e) {
            res_out.success = false;
            res_out.raw_response = std::string("Exception: ") + e.what();
        }
        return res_out;
    }

    std::string api_key_;
    std::string api_secret_;
    std::string host_;
    std::string port_;
    net::io_context ioc_;
    ssl::context ctx_;
};

BinanceOmsClient::BinanceOmsClient(std::string api_key, std::string api_secret, std::string host, std::string port)
    : impl_(std::make_unique<Impl>(std::move(api_key), std::move(api_secret), std::move(host), std::move(port))) {}

BinanceOmsClient::~BinanceOmsClient() = default;

OrderResponse BinanceOmsClient::place_order(const OrderRequest& req) { return impl_->place_order(req); }
OrderResponse BinanceOmsClient::cancel_order(const std::string& symbol, int64_t order_id) { return impl_->cancel_order(symbol, order_id); }
OrderResponse BinanceOmsClient::query_order(const std::string& symbol, int64_t order_id) { return impl_->query_order(symbol, order_id); }
double BinanceOmsClient::get_account_balance(const std::string& asset) { return impl_->get_account_balance(asset); }

} // namespace quantxcore::execution

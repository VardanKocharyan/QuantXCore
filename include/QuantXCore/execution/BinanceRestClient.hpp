#pragma once

#include "QuantXCore/execution/IExecutionClient.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <chrono>

namespace QuantXCore::execution {

class BinanceRestClient : public IExecutionClient {
public:
    // Default URL points to Binance Spot Testnet
    BinanceRestClient(std::string api_key, 
                      std::string api_secret, 
                      std::string base_url = "https://testnet.binance.vision")
        : api_key_(std::move(api_key)),
          api_secret_(std::move(api_secret)),
          base_url_(std::move(base_url)) {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    ~BinanceRestClient() override {
        curl_global_cleanup();
    }

    bool submit_order(Order& order) override {
        const std::string endpoint = "/api/v3/order";
        
        const std::string side_str = (order.side == alpha::SignalType::BUY) ? "BUY" : "SELL";
        
        std::ostringstream query;
        query << "symbol=" << order.symbol
              << "&side=" << side_str
              << "&type=LIMIT"
              << "&timeInForce=GTC"
              << "&quantity=" << std::fixed << std::setprecision(6) << order.quantity
              << "&price=" << std::fixed << std::setprecision(2) << order.price
              << "&newClientOrderId=" << order.client_order_id
              << "&timestamp=" << get_current_timestamp_ms();

        const std::string signature = hmac_sha256(api_secret_, query.str());
        query << "&signature=" << signature;

        std::string response_data;
        long http_code = 0;

        if (!send_post_request(endpoint, query.str(), response_data, http_code)) {
            order.status = OrderStatus::REJECTED;
            std::cerr << "[BINANCE REST] HTTP request failed.\n";
            return false;
        }

        if (http_code != 200) {
            order.status = OrderStatus::REJECTED;
            std::cerr << "[BINANCE REST ERROR] Code " << http_code << " -> " << response_data << "\n";
            return false;
        }

        try {
            auto j = nlohmann::json::parse(response_data);
            order.exchange_order_id = std::to_string(j.at("orderId").get<std::uint64_t>());
            
            std::string raw_status = j.at("status").get<std::string>();
            if (raw_status == "NEW") {
                order.status = OrderStatus::NEW;
            } else if (raw_status == "FILLED") {
                order.status = OrderStatus::FILLED;
                order.filled_quantity = order.quantity;
                order.avg_fill_price = order.price;
            } else {
                order.status = OrderStatus::PENDING_SUBMIT;
            }

            std::cout << "  [BINANCE TESTNET EXECUTE] " << order.symbol 
                      << " " << side_str << " OrderPlaced! ExchID: " 
                      << order.exchange_order_id << " Status: " << raw_status << "\n";

            return true;
        } catch (const std::exception& e) {
            std::cerr << "[BINANCE REST] JSON Parsing error: " << e.what() << "\n";
            order.status = OrderStatus::REJECTED;
            return false;
        }
    }

    bool cancel_order(const std::string& client_order_id) override {
        // Can be extended via DELETE /api/v3/order
        (void)client_order_id;
        return true;
    }

private:
    std::string api_key_;
    std::string api_secret_;
    std::string base_url_;

    static std::uint64_t get_current_timestamp_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    static std::string hmac_sha256(const std::string& key, const std::string& data) {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digest_len = 0;

        HMAC(EVP_sha256(),
             key.c_str(), static_cast<int>(key.length()),
             reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
             digest, &digest_len);

        std::ostringstream result;
        for (unsigned int i = 0; i < digest_len; ++i) {
            result << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
        }
        return result.str();
    }

    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t total_size = size * nmemb;
        auto* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), total_size);
        return total_size;
    }

    bool send_post_request(const std::string& endpoint, 
                           const std::string& query_params, 
                           std::string& response_data, 
                           long& http_code) {
        CURL* curl = curl_easy_init();
        if (!curl) return false;

        std::string full_url = base_url_ + endpoint + "?" + query_params;

        struct curl_slist* headers = nullptr;
        std::string key_header = "X-MBX-APIKEY: " + api_key_;
        headers = curl_slist_append(headers, key_header.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L); // Empty body, params in URL
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return (res == CURLE_OK);
    }
};

} // namespace QuantXCore::execution

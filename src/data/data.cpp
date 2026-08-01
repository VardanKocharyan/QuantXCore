#include "QuantXCore/data/MarketTick.hpp"

#include <charconv>
#include <system_error>
#include <nlohmann/json.hpp>

namespace QuantXCore::data {

// Helper: Fast double parsing from string_view using charconv (zero-allocation)
[[nodiscard]] static std::optional<double> parse_double(std::string_view sv) noexcept {
    double value = 0.0;
    const char* begin = sv.data();
    const char* end = sv.data() + sv.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    
    if (ec == std::errc() && ptr == end) {
        return value;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<MarketTick> parse_binance_tick(std::string_view raw_json) noexcept {
    try {
        const json j = json::parse(raw_json, nullptr, false);
        
        if (j.is_discarded()) {
            return std::nullopt;
        }

        if (!j.contains("p") || !j["p"].is_string() ||
            !j.contains("q") || !j["q"].is_string() ||
            !j.contains("T") || !j["T"].is_number() ||
            !j.contains("m") || !j["m"].is_boolean()) {
            return std::nullopt;
        }

        MarketTick tick{};

        const std::string& price_str = j["p"].get_ref<const std::string&>();
        auto price_opt = parse_double(price_str);
        if (!price_opt.has_value()) {
            return std::nullopt;
        }
        tick.price = price_opt.value();

        const std::string& qty_str = j["q"].get_ref<const std::string&>();
        auto qty_opt = parse_double(qty_str);
        if (!qty_opt.has_value()) {
            return std::nullopt;
        }
        tick.volume = qty_opt.value(); // Ուղղված է: quantity -> volume

        if (j["T"].is_number_integer() || j["T"].is_number_unsigned()) {
            tick.timestamp = j["T"].get<uint64_t>();
        } else {
            return std::nullopt;
        }

        tick.isBuyerMaker = j["m"].get<bool>(); // Ուղղված է: is_buyer_maker -> isBuyerMaker

        if (j.contains("a") && j["a"].is_number()) {
            tick.trade_id = j["a"].get<uint64_t>();
        }

        if (j.contains("s") && j["s"].is_string()) {
            const std::string& symbol = j["s"].get_ref<const std::string&>();
            tick.symbol = symbol;
        }

        return tick;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace QuantXCore::data

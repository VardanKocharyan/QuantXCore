#include "QuantXCore/data/CsvTickParser.hpp"

#include <array>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace QuantXCore::data {

namespace {

constexpr std::size_t kMaxFields = 8;

// Splits `line` on `delim` into up to kMaxFields non-owning string_view
// tokens. Returns the number of tokens found. No heap allocation.
[[nodiscard]] std::size_t split_csv_line(std::string_view line,
                                          std::array<std::string_view, kMaxFields>& out,
                                          char delim = ',') {
    std::size_t count = 0;
    std::size_t start = 0;

    while (count < kMaxFields) {
        const std::size_t pos = line.find(delim, start);
        if (pos == std::string_view::npos) {
            out[count++] = line.substr(start);
            break;
        }
        out[count++] = line.substr(start, pos - start);
        start = pos + 1;
    }
    return count;
}

// Trims trailing '\r' (Windows line endings) and surrounding whitespace.
[[nodiscard]] std::string_view trim(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() &&
           (sv.back() == '\r' || sv.back() == '\n' || sv.back() == ' ' || sv.back() == '\t')) {
        sv.remove_suffix(1);
    }
    return sv;
}

template <typename T>
[[nodiscard]] bool parse_numeric(std::string_view field, T& out) {
    field = trim(field);
    if (field.empty()) return false;
    const auto* begin = field.data();
    const auto* end   = field.data() + field.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr == end;
}

[[nodiscard]] bool parse_bool_flag(std::string_view field) {
    field = trim(field);
    if (field == "1" || field == "true" || field == "True" || field == "TRUE") {
        return true;
    }
    return false;
}

}  // namespace

CsvTickParser::CsvTickParser(std::string file_path, Format format, std::string default_symbol)
    : file_path_(std::move(file_path)),
      format_(format),
      default_symbol_(std::move(default_symbol)) {}

std::optional<MarketTick> CsvTickParser::parse_line(std::string_view line,
                                                      Format format,
                                                      std::string_view default_symbol) {
    line = trim(line);
    if (line.empty()) return std::nullopt;

    std::array<std::string_view, kMaxFields> fields{};
    const std::size_t n = split_csv_line(line, fields);

    MarketTick tick;

    switch (format) {
        case Format::STANDARD_QUANTX: {
            // timestamp,symbol,price,volume,is_buyer_maker,trade_id
            if (n < 6) return std::nullopt;

            if (!parse_numeric(fields[0], tick.timestamp)) return std::nullopt;
            tick.symbol = std::string{trim(fields[1])};
            if (tick.symbol.empty()) return std::nullopt;
            if (!parse_numeric(fields[2], tick.price)) return std::nullopt;
            if (!parse_numeric(fields[3], tick.volume)) return std::nullopt;
            tick.isBuyerMaker = parse_bool_flag(fields[4]);
            if (!parse_numeric(fields[5], tick.trade_id)) return std::nullopt;
            break;
        }
        case Format::BINANCE_AGG_TRADE: {
            // trade_id,price,quantity,first_trade_id,last_trade_id,timestamp,is_buyer_maker
            if (n < 7) return std::nullopt;

            if (!parse_numeric(fields[0], tick.trade_id)) return std::nullopt;
            if (!parse_numeric(fields[1], tick.price)) return std::nullopt;
            if (!parse_numeric(fields[2], tick.volume)) return std::nullopt;
            // fields[3], fields[4] (first/last trade id) intentionally unused.
            if (!parse_numeric(fields[5], tick.timestamp)) return std::nullopt;
            tick.isBuyerMaker = parse_bool_flag(fields[6]);
            tick.symbol = std::string{default_symbol};
            break;
        }
    }

    if (tick.price <= 0.0 || tick.volume < 0.0) return std::nullopt;

    return tick;
}

std::vector<MarketTick> CsvTickParser::parse_all() const {
    std::ifstream file(file_path_, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("CsvTickParser: failed to open file: " + file_path_);
    }

    // Estimate row count from file size to pre-reserve and avoid reallocation
    // churn on the hot parsing loop. ~32 bytes/row is a conservative average
    // for tick CSV rows; actual push_back growth still handles any overshoot.
    file.seekg(0, std::ios::end);
    const std::streamoff file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<MarketTick> ticks;
    if (file_size > 0) {
        const std::size_t estimated_rows =
            static_cast<std::size_t>(file_size) / 32ULL + 16ULL;
        ticks.reserve(estimated_rows);
    }

    std::string line;
    line.reserve(256);
    bool first_line = true;

    while (std::getline(file, line)) {
        auto parsed = parse_line(line, format_, default_symbol_);
        if (!parsed.has_value()) {
            // Silently skip malformed rows -- this also transparently skips
            // a CSV header row on the first line without special-casing it.
            first_line = false;
            continue;
        }
        first_line = false;
        ticks.push_back(std::move(*parsed));
    }
    (void)first_line;

    return ticks;
}

}  // namespace QuantXCore::data

#pragma once
// ============================================================================
// QuantXCore::data::CsvTickParser
// ----------------------------------------------------------------------------
// High-performance, zero-copy CSV tick parser.
//
// Design notes:
//   * `parse_line` never allocates for numeric fields (std::from_chars) and
//     only allocates once per tick for the `symbol` std::string member of
//     MarketTick itself (unavoidable, since MarketTick owns its symbol).
//   * Line splitting is done via std::string_view::find, no intermediate
//     std::vector<std::string> / std::stringstream / std::regex.
//   * `parse_all()` pre-reserves the output vector based on an estimate of
//     average bytes-per-line so the hot loop does not repeatedly reallocate.
// ============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "QuantXCore/data/MarketTick.hpp"

namespace QuantXCore::data {

class CsvTickParser {
public:
    enum class Format {
        STANDARD_QUANTX,   // timestamp,symbol,price,volume,is_buyer_maker,trade_id
        BINANCE_AGG_TRADE  // trade_id,price,quantity,first_trade_id,last_trade_id,timestamp,is_buyer_maker
    };

    CsvTickParser(std::string file_path,
                  Format format,
                  std::string default_symbol = "BTCUSDT");

    // Parses a single CSV line in the given format into a MarketTick.
    // Zero-copy: string_view fields are parsed in place via std::from_chars.
    // Returns std::nullopt on malformed input (wrong field count, parse
    // failure, blank line, or a non-numeric header row).
    [[nodiscard]] static std::optional<MarketTick> parse_line(
        std::string_view line,
        Format format,
        std::string_view default_symbol = "BTCUSDT");

    // Reads the configured file end-to-end and parses every line.
    // Pre-allocates capacity based on file size to avoid reallocation
    // churn on the hot path. Lines that fail to parse (e.g. a CSV header)
    // are silently skipped.
    [[nodiscard]] std::vector<MarketTick> parse_all() const;

    [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }
    [[nodiscard]] Format format() const noexcept { return format_; }

private:
    std::string file_path_;
    Format      format_;
    std::string default_symbol_;
};

}  // namespace QuantXCore::data

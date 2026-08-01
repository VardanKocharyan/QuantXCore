#include "QuantXCore/data/MarketTick.hpp"

#include <gtest/gtest.h>

namespace QuantXCore::data {

class ParserTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test 1: Null / Empty payload
TEST_F(ParserTest, HandlesEmptyOrNullPayload) {
    EXPECT_FALSE(parse_binance_tick("").has_value());
    EXPECT_FALSE(parse_binance_tick("   ").has_value());
}

// Test 2: Invalid JSON format
TEST_F(ParserTest, HandlesInvalidJsonFormat) {
    EXPECT_FALSE(parse_binance_tick("not a json").has_value());
    EXPECT_FALSE(parse_binance_tick("{invalid_json:").has_value());
    EXPECT_FALSE(parse_binance_tick("{'p': '100'}").has_value());
}

// Test 3: Missing mandatory fields
TEST_F(ParserTest, HandlesMissingMandatoryFields) {
    // Missing 'p'
    EXPECT_FALSE(parse_binance_tick(R"({"q":"0.1","T":1600000000,"m":true})").has_value());
    // Missing 'q'
    EXPECT_FALSE(parse_binance_tick(R"({"p":"50000.5","T":1600000000,"m":true})").has_value());
    // Missing 'T'
    EXPECT_FALSE(parse_binance_tick(R"({"p":"50000.5","q":"0.1","m":true})").has_value());
    // Missing 'm'
    EXPECT_FALSE(parse_binance_tick(R"({"p":"50000.5","q":"0.1","T":1600000000})").has_value());
}

// Test 4: Valid payload (standard Binance aggTrade stream structure)
TEST_F(ParserTest, ValidPayloadParsesCorrectly) {
    std::string_view json_payload = R"({
        "e": "aggTrade",
        "E": 123456789,
        "s": "BTCUSDT",
        "a": 12345,
        "p": "50000.50",
        "q": "0.125",
        "f": 100,
        "l": 105,
        "T": 1672531199000,
        "m": true,
        "M": true
    })";

    auto tick_opt = parse_binance_tick(json_payload);
    ASSERT_TRUE(tick_opt.has_value());

    const auto& tick = tick_opt.value();
    EXPECT_DOUBLE_EQ(tick.price, 50000.50);
    EXPECT_DOUBLE_EQ(tick.volume, 0.125); // Ուղղված է: quantity -> volume
    EXPECT_EQ(tick.timestamp, 1672531199000ULL);
    EXPECT_TRUE(tick.isBuyerMaker);       // Ուղղված է: is_buyer_maker -> isBuyerMaker
    EXPECT_EQ(tick.trade_id, 12345ULL);
    EXPECT_EQ(tick.symbol, "BTCUSDT");
}

// Test 5: Valid payload variant (ETHUSDT)
TEST_F(ParserTest, ValidPayload2ParsesCorrectly) {
    std::string_view json_payload = R"({
        "s": "ETHUSDT",
        "p": "1850.25",
        "q": "1.500",
        "T": 1672531200000,
        "m": false,
        "a": 99999
    })";

    auto tick_opt = parse_binance_tick(json_payload);
    ASSERT_TRUE(tick_opt.has_value());

    const auto& tick = tick_opt.value();
    EXPECT_DOUBLE_EQ(tick.price, 1850.25);
    EXPECT_DOUBLE_EQ(tick.volume, 1.500); // Ուղղված է: quantity -> volume
    EXPECT_EQ(tick.timestamp, 1672531200000ULL);
    EXPECT_FALSE(tick.isBuyerMaker);      // Ուղղված է: is_buyer_maker -> isBuyerMaker
    EXPECT_EQ(tick.trade_id, 99999ULL);
    EXPECT_EQ(tick.symbol, "ETHUSDT");
}

// Test 6: Malformed field data types
TEST_F(ParserTest, HandlesMalformedFieldDataTypes) {
    // 'p' is not a string
    EXPECT_FALSE(parse_binance_tick(R"({"p":50000.5,"q":"0.1","T":1600000000,"m":true})").has_value());
    // 'q' is not a string
    EXPECT_FALSE(parse_binance_tick(R"({"p":"50000.5","q":0.1,"T":1600000000,"m":true})").has_value());
    // 'T' is string instead of int
    EXPECT_FALSE(parse_binance_tick(R"({"p":"50000.5","q":"0.1","T":"1600000000","m":true})").has_value());
    // 'm' is string instead of bool
    EXPECT_FALSE(parse_binance_tick(R"({"p":"50000.5","q":"0.1","T":1600000000,"m":"true"})").has_value());
}

// Test 7: Unparsable double values
TEST_F(ParserTest, HandlesUnparsableDoubleStrings) {
    // Non-numeric price
    EXPECT_FALSE(parse_binance_tick(R"({"p":"abc","q":"0.1","T":1600000000,"m":true})").has_value());
    // Non-numeric quantity
    EXPECT_FALSE(parse_binance_tick(R"({"p":"50000.5","q":"xyz","T":1600000000,"m":true})").has_value());
    // Extra garbage in float
    EXPECT_FALSE(parse_binance_tick(R"({"p":"50000.5foo","q":"0.1","T":1600000000,"m":true})").has_value());
}

// Test 8: Optional fields missing (symbol and trade_id)
TEST_F(ParserTest, OptionalFieldsHandledGracefully) {
    std::string_view json_payload = R"({
        "p": "100.0",
        "q": "0.125",
        "T": 10000000,
        "m": true
    })";

    auto tick_opt = parse_binance_tick(json_payload);
    ASSERT_TRUE(tick_opt.has_value());

    const auto& tick = tick_opt.value();
    EXPECT_DOUBLE_EQ(tick.price, 100.0);
    EXPECT_DOUBLE_EQ(tick.volume, 0.125); // Ուղղված է: quantity -> volume
    EXPECT_EQ(tick.timestamp, 10000000ULL);
    EXPECT_TRUE(tick.isBuyerMaker);       // Ուղղված է: is_buyer_maker -> isBuyerMaker
    EXPECT_EQ(tick.trade_id, 0ULL);
    EXPECT_TRUE(tick.symbol.empty());
}

// Test 9: Edge cases - very large numeric values
TEST_F(ParserTest, VeryLargeNumbersParsedCorrectly) {
    std::string_view json_payload = R"({
        "p": "123456789.987654321",
        "q": "9999999.999",
        "T": 18446744073709551614,
        "m": false
    })";

    auto tick_opt = parse_binance_tick(json_payload);
    ASSERT_TRUE(tick_opt.has_value());
    const auto& tick = tick_opt.value();
    EXPECT_DOUBLE_EQ(tick.price, 123456789.987654321);
    EXPECT_DOUBLE_EQ(tick.volume, 9999999.999); // Ուղղված է: quantity -> volume
}

// Test 10: Edge cases - zero values
TEST_F(ParserTest, ZeroValuesParsedCorrectly) {
    std::string_view json_payload = R"({
        "p": "0.00000000",
        "q": "0.0",
        "T": 0,
        "m": false
    })";

    auto tick_opt = parse_binance_tick(json_payload);
    ASSERT_TRUE(tick_opt.has_value());
    const auto& tick = tick_opt.value();
    EXPECT_DOUBLE_EQ(tick.price, 0.0);
    EXPECT_DOUBLE_EQ(tick.volume, 0.0); // Ուղղված է: quantity -> volume
    EXPECT_EQ(tick.timestamp, 0ULL);
    EXPECT_FALSE(tick.isBuyerMaker);    // Ուղղված է: is_buyer_maker -> isBuyerMaker
}

// Test 11: Scientific notation double strings
TEST_F(ParserTest, ScientificNotationParsedCorrectly) {
    std::string_view json_payload = R"({
        "p": "1.23e4",
        "q": "5.67e8",
        "T": 12345,
        "m": true
    })";

    auto tick_opt = parse_binance_tick(json_payload);
    ASSERT_TRUE(tick_opt.has_value());
    const auto& tick = tick_opt.value();
    EXPECT_DOUBLE_EQ(tick.price, 12300.0);
    EXPECT_DOUBLE_EQ(tick.volume, 567000000.0); // Ուղղված է: quantity -> volume
}

} // namespace QuantXCore::data

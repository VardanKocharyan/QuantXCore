#include <gtest/gtest.h>
#include "QuantXCore/execution/BinanceOmsClient.hpp"

using namespace quantxcore::execution;

TEST(BinanceOmsTest, HmacSha256SignatureValidation) {
    // Official Binance API Documentation Example Test Vector
    std::string secret = "NhqMzO6wJg8M19SCFvtltZ2fc5y41M20RkyTOtTxA0dnhOEZEYgAeMfdT6p3cnwY";
    std::string query = "symbol=LTCBTC&side=BUY&type=LIMIT&timeInForce=GTC&quantity=1&price=0.1&timestamp=1499827319559";

    std::string expected_sig = "02cdc70b7c570b64a2f683339c2f48c77a678489a1346c42f0b0ca0e0e2c82a0";
    std::string actual_sig = BinanceOmsClient::hmac_sha256(secret, query);

    EXPECT_EQ(actual_sig, expected_sig);
}

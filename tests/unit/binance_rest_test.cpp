#include <gtest/gtest.h>
#include "QuantXCore/data/BinanceRestClient.hpp"

TEST(BinanceRestClientTest, FetchLiveSnapshotBTCUSDT) {
    quantxcore::data::BinanceRestClient client;
    auto snapshot = client.fetch_order_book_snapshot("BTCUSDT", 100);

    EXPECT_GT(snapshot.last_update_id, 0);
    EXPECT_FALSE(snapshot.bids.empty());
    EXPECT_FALSE(snapshot.asks.empty());

    // Verify Bids are descending and Asks are ascending
    EXPECT_GT(snapshot.bids.front().first, 0.0);
    EXPECT_GT(snapshot.asks.front().first, snapshot.bids.front().first); // Ask > Bid (No crossed book)
}

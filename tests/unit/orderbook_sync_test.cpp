#include <gtest/gtest.h>
#include "QuantXCore/data/OrderBook.hpp"
#include "QuantXCore/data/OrderBookSync.hpp"

using namespace QuantXCore::data;

class OrderBookSyncTest : public ::testing::Test {
protected:
    OrderBook book;
};

TEST_F(OrderBookSyncTest, InitialStateIsBuffering) {
    OrderBookSync sync("BTCUSDT", book);
    EXPECT_EQ(sync.state(), SyncState::Buffering);
    EXPECT_FALSE(sync.is_in_sync());
}

TEST_F(OrderBookSyncTest, ApplySnapshotTransitionsToInSync) {
    OrderBookSync sync("BTCUSDT", book);

    DepthSnapshot snapshot;
    snapshot.last_update_id = 100;
    snapshot.bids = {{50000.0, 1.5}, {49900.0, 2.0}};
    snapshot.asks = {{50100.0, 0.8}, {50200.0, 3.0}};

    EXPECT_TRUE(sync.apply_snapshot(snapshot));
    EXPECT_EQ(sync.state(), SyncState::InSync);
    EXPECT_TRUE(sync.is_in_sync());
    EXPECT_EQ(sync.last_applied_update_id(), 100u);

    EXPECT_EQ(book.get_best_bid()->price, 50000.0);
    EXPECT_EQ(book.get_best_ask()->price, 50100.0);
}

TEST_F(OrderBookSyncTest, ReplaysBufferedEventsCorrectly) {
    OrderBookSync sync("BTCUSDT", book);

    // Buffer stale event (u <= 100)
    DepthEvent ev1;
    ev1.first_update_id = 90;
    ev1.final_update_id = 95;
    ev1.bids = {{49800.0, 1.0}};
    sync.process_depth_event(ev1);

    // Buffer valid spanning event (U <= 101, u >= 101)
    DepthEvent ev2;
    ev2.first_update_id = 96;
    ev2.final_update_id = 105;
    ev2.bids = {{50050.0, 2.5}}; // New best bid
    sync.process_depth_event(ev2);

    DepthSnapshot snapshot;
    snapshot.last_update_id = 100;
    snapshot.bids = {{50000.0, 1.5}};
    snapshot.asks = {{50100.0, 0.8}};

    EXPECT_TRUE(sync.apply_snapshot(snapshot));
    EXPECT_EQ(sync.state(), SyncState::InSync);
    EXPECT_EQ(sync.last_applied_update_id(), 105u);

    // ev2 should have been applied: best bid updated to 50050.0
    EXPECT_EQ(book.get_best_bid()->price, 50050.0);
}

TEST_F(OrderBookSyncTest, SequenceGapTriggersOutOfSync) {
    OrderBookSync sync("BTCUSDT", book);

    DepthSnapshot snapshot;
    snapshot.last_update_id = 100;
    sync.apply_snapshot(snapshot);
    EXPECT_EQ(sync.state(), SyncState::InSync);

    // Send event with a sequence gap (U = 110, expected <= 101)
    DepthEvent ev;
    ev.first_update_id = 110;
    ev.final_update_id = 115;
    sync.process_depth_event(ev);

    EXPECT_EQ(sync.state(), SyncState::OutOfSync);
    EXPECT_FALSE(sync.is_in_sync());
}

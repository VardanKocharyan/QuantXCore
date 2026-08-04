#include "QuantXCore/data/OrderBook.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using QuantXCore::data::OrderBook;
using QuantXCore::data::PriceLevel;
using QuantXCore::data::Side;

namespace {

// ---------------------------------------------------------------------
// Empty book behavior
// ---------------------------------------------------------------------

TEST(OrderBookTest, EmptyBookHasNoBestBidOrAsk) {
    OrderBook book;
    EXPECT_FALSE(book.get_best_bid().has_value());
    EXPECT_FALSE(book.get_best_ask().has_value());
}

TEST(OrderBookTest, EmptyBookHasNoSpreadOrMidOrMicroPrice) {
    OrderBook book;
    EXPECT_FALSE(book.get_spread().has_value());
    EXPECT_FALSE(book.get_mid_price().has_value());
    EXPECT_FALSE(book.get_micro_price().has_value());
}

TEST(OrderBookTest, EmptyBookImbalanceIsZero) {
    OrderBook book;
    EXPECT_DOUBLE_EQ(book.get_imbalance(), 0.0);
    EXPECT_DOUBLE_EQ(book.get_imbalance(5), 0.0);
}

TEST(OrderBookTest, EmptyBookDepthQueriesReturnEmptyVectors) {
    OrderBook book;
    EXPECT_TRUE(book.get_bids().empty());
    EXPECT_TRUE(book.get_asks().empty());
}

TEST(OrderBookTest, OneSidedBookHasNoSpreadMidOrMicroPrice) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);

    EXPECT_TRUE(book.get_best_bid().has_value());
    EXPECT_FALSE(book.get_best_ask().has_value());
    EXPECT_FALSE(book.get_spread().has_value());
    EXPECT_FALSE(book.get_mid_price().has_value());
    EXPECT_FALSE(book.get_micro_price().has_value());
}

// ---------------------------------------------------------------------
// Level insertion, update, sorting
// ---------------------------------------------------------------------

TEST(OrderBookTest, InsertSingleBidBecomesBestBid) {
    OrderBook book;
    book.update_level(Side::Buy, 99.5, 2.0);

    auto best = book.get_best_bid();
    ASSERT_TRUE(best.has_value());
    EXPECT_DOUBLE_EQ(best->price, 99.5);
    EXPECT_DOUBLE_EQ(best->quantity, 2.0);
}

TEST(OrderBookTest, InsertSingleAskBecomesBestAsk) {
    OrderBook book;
    book.update_level(Side::Sell, 101.0, 3.0);

    auto best = book.get_best_ask();
    ASSERT_TRUE(best.has_value());
    EXPECT_DOUBLE_EQ(best->price, 101.0);
    EXPECT_DOUBLE_EQ(best->quantity, 3.0);
}

TEST(OrderBookTest, BidsAreSortedDescending) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Buy, 102.0, 1.0);
    book.update_level(Side::Buy, 101.0, 1.0);

    auto bids = book.get_bids();
    ASSERT_EQ(bids.size(), 3u);
    EXPECT_DOUBLE_EQ(bids[0].price, 102.0);
    EXPECT_DOUBLE_EQ(bids[1].price, 101.0);
    EXPECT_DOUBLE_EQ(bids[2].price, 100.0);
}

TEST(OrderBookTest, AsksAreSortedAscending) {
    OrderBook book;
    book.update_level(Side::Sell, 105.0, 1.0);
    book.update_level(Side::Sell, 103.0, 1.0);
    book.update_level(Side::Sell, 104.0, 1.0);

    auto asks = book.get_asks();
    ASSERT_EQ(asks.size(), 3u);
    EXPECT_DOUBLE_EQ(asks[0].price, 103.0);
    EXPECT_DOUBLE_EQ(asks[1].price, 104.0);
    EXPECT_DOUBLE_EQ(asks[2].price, 105.0);
}

TEST(OrderBookTest, UpdatingExistingLevelOverwritesQuantity) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Buy, 100.0, 5.0);

    auto best = book.get_best_bid();
    ASSERT_TRUE(best.has_value());
    EXPECT_DOUBLE_EQ(best->price, 100.0);
    EXPECT_DOUBLE_EQ(best->quantity, 5.0);
    EXPECT_EQ(book.bid_level_count(), 1u);
}

// ---------------------------------------------------------------------
// Zero-quantity removal
// ---------------------------------------------------------------------

TEST(OrderBookTest, ZeroQuantityRemovesBidLevel) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    ASSERT_EQ(book.bid_level_count(), 1u);

    book.update_level(Side::Buy, 100.0, 0.0);
    EXPECT_EQ(book.bid_level_count(), 0u);
    EXPECT_FALSE(book.get_best_bid().has_value());
}

TEST(OrderBookTest, ZeroQuantityRemovesAskLevel) {
    OrderBook book;
    book.update_level(Side::Sell, 100.0, 1.0);
    ASSERT_EQ(book.ask_level_count(), 1u);

    book.update_level(Side::Sell, 100.0, 0.0);
    EXPECT_EQ(book.ask_level_count(), 0u);
    EXPECT_FALSE(book.get_best_ask().has_value());
}

TEST(OrderBookTest, RemovingNonExistentLevelIsNoOp) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    // Removing a price that was never inserted should not throw or affect
    // existing levels.
    book.update_level(Side::Buy, 999.0, 0.0);
    EXPECT_EQ(book.bid_level_count(), 1u);
}

TEST(OrderBookTest, NegativeQuantityAlsoRemovesLevel) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Buy, 100.0, -1.0);
    EXPECT_EQ(book.bid_level_count(), 0u);
}

TEST(OrderBookTest, RemovingOnlyBestLevelPromotesNextLevel) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Buy, 101.0, 1.0);

    book.update_level(Side::Buy, 101.0, 0.0);

    auto best = book.get_best_bid();
    ASSERT_TRUE(best.has_value());
    EXPECT_DOUBLE_EQ(best->price, 100.0);
}

// ---------------------------------------------------------------------
// clear()
// ---------------------------------------------------------------------

TEST(OrderBookTest, ClearResetsBookToEmpty) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Sell, 101.0, 1.0);

    book.clear();

    EXPECT_EQ(book.bid_level_count(), 0u);
    EXPECT_EQ(book.ask_level_count(), 0u);
    EXPECT_FALSE(book.get_best_bid().has_value());
    EXPECT_FALSE(book.get_best_ask().has_value());
}

// ---------------------------------------------------------------------
// Spread / mid-price
// ---------------------------------------------------------------------

TEST(OrderBookTest, SpreadIsAskMinusBid) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Sell, 100.5, 1.0);

    auto spread = book.get_spread();
    ASSERT_TRUE(spread.has_value());
    EXPECT_DOUBLE_EQ(*spread, 0.5);
}

TEST(OrderBookTest, MidPriceIsAverageOfBestBidAndAsk) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Sell, 102.0, 1.0);

    auto mid = book.get_mid_price();
    ASSERT_TRUE(mid.has_value());
    EXPECT_DOUBLE_EQ(*mid, 101.0);
}

// ---------------------------------------------------------------------
// Micro-price
// ---------------------------------------------------------------------

TEST(OrderBookTest, MicroPriceWeightsTowardsLargerOppositeVolume) {
    OrderBook book;
    // Bid 100 @ qty 1, Ask 102 @ qty 3
    // Micro = (100 * 3 + 102 * 1) / (1 + 3) = (300 + 102) / 4 = 100.5
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Sell, 102.0, 3.0);

    auto micro = book.get_micro_price();
    ASSERT_TRUE(micro.has_value());
    EXPECT_DOUBLE_EQ(*micro, 100.5);
}

TEST(OrderBookTest, MicroPriceEqualsMidPriceWhenVolumesEqual) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 5.0);
    book.update_level(Side::Sell, 102.0, 5.0);

    auto micro = book.get_micro_price();
    auto mid = book.get_mid_price();
    ASSERT_TRUE(micro.has_value());
    ASSERT_TRUE(mid.has_value());
    EXPECT_DOUBLE_EQ(*micro, *mid);
}

// ---------------------------------------------------------------------
// Imbalance
// ---------------------------------------------------------------------

TEST(OrderBookTest, ImbalanceIsPositiveWhenBidsDominate) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 8.0);
    book.update_level(Side::Sell, 101.0, 2.0);

    // (8 - 2) / (8 + 2) = 0.6
    EXPECT_DOUBLE_EQ(book.get_imbalance(), 0.6);
}

TEST(OrderBookTest, ImbalanceIsNegativeWhenAsksDominate) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Sell, 101.0, 9.0);

    // (1 - 9) / (1 + 9) = -0.8
    EXPECT_DOUBLE_EQ(book.get_imbalance(), -0.8);
}

TEST(OrderBookTest, ImbalanceIsZeroWhenBalanced) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 5.0);
    book.update_level(Side::Sell, 101.0, 5.0);

    EXPECT_DOUBLE_EQ(book.get_imbalance(), 0.0);
}

TEST(OrderBookTest, ImbalanceRespectsDepthCutoff) {
    OrderBook book;
    // Top 2 bid levels: 10 + 10 = 20. Deep level (not counted at depth=2): 100.
    book.update_level(Side::Buy, 100.0, 10.0);
    book.update_level(Side::Buy, 99.0, 10.0);
    book.update_level(Side::Buy, 98.0, 100.0);

    book.update_level(Side::Sell, 101.0, 10.0);
    book.update_level(Side::Sell, 102.0, 10.0);

    // At depth=2: bid_vol=20, ask_vol=20 -> imbalance 0.
    EXPECT_DOUBLE_EQ(book.get_imbalance(2), 0.0);

    // At depth=5 (includes deep bid level): bid_vol=120, ask_vol=20
    // -> (120-20)/(140) = 100/140
    EXPECT_NEAR(book.get_imbalance(5), 100.0 / 140.0, 1e-12);
}

TEST(OrderBookTest, ImbalanceWithOnlyBidsIsOne) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 5.0);
    EXPECT_DOUBLE_EQ(book.get_imbalance(), 1.0);
}

TEST(OrderBookTest, ImbalanceWithOnlyAsksIsNegativeOne) {
    OrderBook book;
    book.update_level(Side::Sell, 100.0, 5.0);
    EXPECT_DOUBLE_EQ(book.get_imbalance(), -1.0);
}

// ---------------------------------------------------------------------
// Depth-limited get_bids / get_asks
// ---------------------------------------------------------------------

TEST(OrderBookTest, GetBidsRespectsRequestedDepth) {
    OrderBook book;
    for (double p = 100.0; p < 110.0; p += 1.0) {
        book.update_level(Side::Buy, p, 1.0);
    }
    ASSERT_EQ(book.bid_level_count(), 10u);

    auto top3 = book.get_bids(3);
    ASSERT_EQ(top3.size(), 3u);
    EXPECT_DOUBLE_EQ(top3[0].price, 109.0);
    EXPECT_DOUBLE_EQ(top3[1].price, 108.0);
    EXPECT_DOUBLE_EQ(top3[2].price, 107.0);
}

TEST(OrderBookTest, GetAsksRespectsRequestedDepth) {
    OrderBook book;
    for (double p = 100.0; p < 110.0; p += 1.0) {
        book.update_level(Side::Sell, p, 1.0);
    }
    ASSERT_EQ(book.ask_level_count(), 10u);

    auto top3 = book.get_asks(3);
    ASSERT_EQ(top3.size(), 3u);
    EXPECT_DOUBLE_EQ(top3[0].price, 100.0);
    EXPECT_DOUBLE_EQ(top3[1].price, 101.0);
    EXPECT_DOUBLE_EQ(top3[2].price, 102.0);
}

TEST(OrderBookTest, DepthGreaterThanAvailableLevelsReturnsAllLevels) {
    OrderBook book;
    book.update_level(Side::Buy, 100.0, 1.0);
    book.update_level(Side::Buy, 99.0, 1.0);

    auto bids = book.get_bids(100);
    EXPECT_EQ(bids.size(), 2u);
}

// ---------------------------------------------------------------------
// Concurrency smoke test
// ---------------------------------------------------------------------

TEST(OrderBookTest, ConcurrentReadsAndWritesDoNotCrashOrDeadlock) {
    OrderBook book;
    std::atomic<bool> stop{false};

    // Writer thread: continuously updates levels.
    std::thread writer([&]() {
        double price = 100.0;
        while (!stop.load(std::memory_order_relaxed)) {
            book.update_level(Side::Buy, price, 1.0);
            book.update_level(Side::Sell, price + 1.0, 1.0);
            price = (price >= 105.0) ? 100.0 : price + 1.0;
        }
    });

    // Reader threads: continuously query analytics.
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                (void)book.get_best_bid();
                (void)book.get_best_ask();
                (void)book.get_spread();
                (void)book.get_mid_price();
                (void)book.get_micro_price();
                (void)book.get_imbalance(5);
                (void)book.get_bids(10);
                (void)book.get_asks(10);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_relaxed);

    writer.join();
    for (auto& t : readers) {
        t.join();
    }

    // If we reach here without hanging or crashing, the shared_mutex-based
    // synchronization is behaving correctly under contention.
    SUCCEED();
}

} // namespace

#include "QuantXCore/persistence/DatabaseManager.hpp"
#include <gtest/gtest.h>
#include <cstdio>

using namespace QuantXCore::persistence;

class DatabaseManagerTest : public ::testing::Test {
protected:
    const std::string test_db_path = "test_quantxcore.db";

    void SetUp() override {
        // Յուրաքանչյուր թեստից առաջ ջնջում ենք հին թեստային DB-ն
        std::remove(test_db_path.c_str());
    }

    void TearDown() override {
        // Թեստից հետո մաքրում ենք ստեղծված ֆայլը
        std::remove(test_db_path.c_str());
    }
};

TEST_F(DatabaseManagerTest, DatabaseInitSuccess) {
    DatabaseManager db(test_db_path);
    EXPECT_TRUE(db.init());
}

TEST_F(DatabaseManagerTest, SaveAndLoadOpenPositions) {
    DatabaseManager db(test_db_path);
    ASSERT_TRUE(db.init());

    PositionRecord pos1{
        .symbol = "BTCUSDT",
        .side = "BUY",
        .entry_price = 65000.0,
        .quantity = 0.5,
        .stop_loss = 64000.0,
        .take_profit = 68000.0,
        .peak_price = 65500.0,
        .trailing_pct = 0.01
    };

    EXPECT_TRUE(db.save_position(pos1));

    // Բեռնում ենք բաց դիրքերը DB-ից
    auto positions = db.load_open_positions();
    ASSERT_EQ(positions.size(), 1);
    EXPECT_EQ(positions[0].symbol, "BTCUSDT");
    EXPECT_EQ(positions[0].side, "BUY");
    EXPECT_DOUBLE_EQ(positions[0].entry_price, 65000.0);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 0.5);

    // Թարմացնում ենք գոյություն ունեցող դիրքը (INSERT OR REPLACE)
    pos1.peak_price = 66000.0;
    EXPECT_TRUE(db.save_position(pos1));

    positions = db.load_open_positions();
    ASSERT_EQ(positions.size(), 1);
    EXPECT_DOUBLE_EQ(positions[0].peak_price, 66000.0);

    // Ջնջում ենք դիրքը փակվելուց հետո
    EXPECT_TRUE(db.delete_position("BTCUSDT"));
    positions = db.load_open_positions();
    EXPECT_EQ(positions.size(), 0);
}

TEST_F(DatabaseManagerTest, SaveTradeAndPnLSnapshot) {
    DatabaseManager db(test_db_path);
    ASSERT_TRUE(db.init());

    TradeRecord trade{
        .id = 1,
        .symbol = "ETHUSDT",
        .side = "SELL",
        .price = 3500.0,
        .quantity = 2.0,
        .realized_pnl = 120.5,
        .timestamp = 1700000000
    };

    EXPECT_TRUE(db.save_trade(trade));
    EXPECT_TRUE(db.save_pnl_snapshot(10120.5, 120.5, 0.0, 1700000000));
}

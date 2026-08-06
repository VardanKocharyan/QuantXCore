#pragma once

#include <sqlite3.h>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

namespace QuantXCore::persistence {

struct TradeRecord {
    int64_t id{0};
    std::string symbol;
    std::string side; // "BUY" կամ "SELL"
    double price{0.0};
    double quantity{0.0};
    double realized_pnl{0.0};
    uint64_t timestamp{0};
};

struct PositionRecord {
    std::string symbol;
    std::string side; // "BUY" / "SELL"
    double entry_price{0.0};
    double quantity{0.0};
    double stop_loss{0.0};
    double take_profit{0.0};
    double peak_price{0.0};
    double trailing_pct{0.0};
};

class DatabaseManager {
public:
    explicit DatabaseManager(const std::string& db_path = "quantxcore.db")
        : db_path_(db_path) {}

    ~DatabaseManager() {
        close();
    }

    bool init() {
        std::lock_guard<std::mutex> lock(mutex_);
        int rc = sqlite3_open(db_path_.c_str(), &db_);
        if (rc != SQLITE_OK) {
            std::cerr << "[DatabaseManager] Failed to open DB: " << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        return create_tables();
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    // 1. Trade Logging
    bool save_trade(const TradeRecord& trade) {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql = "INSERT INTO trades (symbol, side, price, quantity, realized_pnl, timestamp) "
                          "VALUES (?, ?, ?, ?, ?, ?);";
        
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, trade.symbol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, trade.side.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, trade.price);
        sqlite3_bind_double(stmt, 4, trade.quantity);
        sqlite3_bind_double(stmt, 5, trade.realized_pnl);
        sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(trade.timestamp));

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    // 2. Open Positions Management (State Persistence)
    bool save_position(const PositionRecord& pos) {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql = "INSERT OR REPLACE INTO open_positions "
                          "(symbol, side, entry_price, quantity, stop_loss, take_profit, peak_price, trailing_pct) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, pos.symbol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pos.side.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, pos.entry_price);
        sqlite3_bind_double(stmt, 4, pos.quantity);
        sqlite3_bind_double(stmt, 5, pos.stop_loss);
        sqlite3_bind_double(stmt, 6, pos.take_profit);
        sqlite3_bind_double(stmt, 7, pos.peak_price);
        sqlite3_bind_double(stmt, 8, pos.trailing_pct);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    bool delete_position(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql = "DELETE FROM open_positions WHERE symbol = ?;";
        
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    // 3. State Recovery (Restart-ից հետո բաց դիրքերը բեռնել RAM)
    std::vector<PositionRecord> load_open_positions() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PositionRecord> positions;
        const char* sql = "SELECT symbol, side, entry_price, quantity, stop_loss, take_profit, peak_price, trailing_pct FROM open_positions;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return positions;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PositionRecord pos;
            pos.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            pos.side = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            pos.entry_price = sqlite3_column_double(stmt, 2);
            pos.quantity = sqlite3_column_double(stmt, 3);
            pos.stop_loss = sqlite3_column_double(stmt, 4);
            pos.take_profit = sqlite3_column_double(stmt, 5);
            pos.peak_price = sqlite3_column_double(stmt, 6);
            pos.trailing_pct = sqlite3_column_double(stmt, 7);

            positions.push_back(pos);
        }

        sqlite3_finalize(stmt);
        return positions;
    }

    // 4. PnL Equity Curve Tracking
    bool save_pnl_snapshot(double total_equity, double realized_pnl, double unrealized_pnl, uint64_t timestamp) {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql = "INSERT INTO pnl_snapshots (total_equity, realized_pnl, unrealized_pnl, timestamp) "
                          "VALUES (?, ?, ?, ?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_double(stmt, 1, total_equity);
        sqlite3_bind_double(stmt, 2, realized_pnl);
        sqlite3_bind_double(stmt, 3, unrealized_pnl);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(timestamp));

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

private:
    bool create_tables() {
        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS trades (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                symbol TEXT NOT NULL,
                side TEXT NOT NULL,
                price REAL NOT NULL,
                quantity REAL NOT NULL,
                realized_pnl REAL DEFAULT 0.0,
                timestamp INTEGER NOT NULL
            );

            CREATE TABLE IF NOT EXISTS open_positions (
                symbol TEXT PRIMARY KEY,
                side TEXT NOT NULL,
                entry_price REAL NOT NULL,
                quantity REAL NOT NULL,
                stop_loss REAL NOT NULL,
                take_profit REAL NOT NULL,
                peak_price REAL NOT NULL,
                trailing_pct REAL NOT NULL
            );

            CREATE TABLE IF NOT EXISTS pnl_snapshots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                total_equity REAL NOT NULL,
                realized_pnl REAL NOT NULL,
                unrealized_pnl REAL NOT NULL,
                timestamp INTEGER NOT NULL
            );
        )";

        char* err_msg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "[DatabaseManager] Table creation error: " << err_msg << std::endl;
            sqlite3_free(err_msg);
            return false;
        }

        std::cout << "[DatabaseManager] SQLite Database initialized successfully." << std::endl;
        return true;
    }

    std::string db_path_;
    sqlite3* db_{nullptr};
    std::mutex mutex_;
};

} // namespace QuantXCore::persistence

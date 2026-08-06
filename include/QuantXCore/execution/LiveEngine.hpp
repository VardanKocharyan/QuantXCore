#ifndef QUANTXCORE_EXECUTION_LIVE_ENGINE_HPP
#define QUANTXCORE_EXECUTION_LIVE_ENGINE_HPP

#include "QuantXCore/alpha/AlphaEngine.hpp"
#include "QuantXCore/data/BinanceStreamer.hpp"
#include "QuantXCore/execution/BinanceOmsClient.hpp"
#include "QuantXCore/execution/BracketOrderManager.hpp"
#include "QuantXCore/persistence/DatabaseManager.hpp"
#include "QuantXCore/risk/RiskEngine.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace QuantXCore::execution {

using BinanceOmsClient = quantxcore::execution::BinanceOmsClient;

enum class LiveEngineMode {
    PAPER_TRADING,
    LIVE_TRADING
};

class LiveEngine {
public:
    LiveEngine(data::BinanceStreamer& streamer,
               alpha::AlphaEngine& alpha_engine,
               risk::RiskEngine& risk_engine,
               std::shared_ptr<BinanceOmsClient> oms,
               LiveEngineMode mode = LiveEngineMode::PAPER_TRADING,
               double risk_fraction_per_signal = 0.02);

    ~LiveEngine();

    void start();
    void stop();
    void stop_and_close_all_positions();
    bool is_running() const { return is_running_.load(); }
    uint64_t ticks_processed() const { return ticks_processed_.load(std::memory_order_relaxed); }

    bool init_database(const std::string& db_path = "quantxcore.db") {
        db_manager_ = std::make_shared<persistence::DatabaseManager>(db_path);
        return db_manager_->init();
    }

    persistence::DatabaseManager* db_manager() { return db_manager_.get(); }

    void set_bracket_params(double sl_pct, double tp_pct, double trailing_pct) {
        default_sl_pct_ = sl_pct;
        default_tp_pct_ = tp_pct;
        default_trailing_pct_ = trailing_pct;
    }

    void set_db_manager(std::shared_ptr<persistence::DatabaseManager> db_manager) {
        db_manager_ = db_manager;
    }

    void set_spot_mode(bool is_spot) { is_spot_mode_ = is_spot; }

private:
    void process_ticks_loop();
    void handle_tick(const data::MarketTick& tick);
    void recover_positions();

    data::BinanceStreamer& streamer_;
    alpha::AlphaEngine& alpha_engine_;
    risk::RiskEngine& risk_engine_;
    std::shared_ptr<BinanceOmsClient> oms_;
    LiveEngineMode mode_;
    double risk_fraction_per_signal_;

    std::shared_ptr<persistence::DatabaseManager> db_manager_;
    BracketOrderManager bracket_manager_;
    double default_sl_pct_{0.015};       // 1.5% Stop-Loss
    double default_tp_pct_{0.030};       // 3.0% Take-Profit
    double default_trailing_pct_{0.010}; // 1.0% Trailing Stop

    std::atomic<bool> is_running_{false};
    std::atomic<uint64_t> ticks_processed_{0};
    std::thread worker_thread_;

    bool is_spot_mode_{true}; 
};

} // namespace QuantXCore::execution

#endif // QUANTXCORE_EXECUTION_LIVE_ENGINE_HPP

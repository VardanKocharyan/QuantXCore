// src/main.cpp
//
// QuantXCore - End-to-end pipeline wiring:
//   MarketTick (data) -> AlphaEngine (alpha) -> RiskEngine (risk) 
//   -> OrderRequest -> OrderManager (OMS) -> Execution Client (Mock / Binance)
//   -> PositionTracker & PnLTracker (execution)

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "QuantXCore/data/MarketTick.hpp"
#include "QuantXCore/data/IMarketDataStreamer.hpp"
#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/alpha/AlphaEngine.hpp"
#include "QuantXCore/alpha/MovingAverageCrossover.hpp"
#include "QuantXCore/alpha/RsiStrategy.hpp"
#include "QuantXCore/risk/RiskEngine.hpp"
#include "QuantXCore/risk/RiskLimits.hpp"
#include "QuantXCore/utils/ConfigLoader.hpp"

// OMS & Execution Headers
#include "QuantXCore/execution/Order.hpp"
#include "QuantXCore/execution/IExecutionClient.hpp"
#include "QuantXCore/execution/MockExecutionClient.hpp"
#include "QuantXCore/execution/BinanceRestClient.hpp"
#include "QuantXCore/execution/OrderManager.hpp"
#include "QuantXCore/execution/PositionTracker.hpp"
#include "QuantXCore/execution/PnLTracker.hpp"

#if defined(QUANTXCORE_WITH_REDIS)
#include "QuantXCore/data/RedisStreamer.hpp"
#endif

namespace {

using QuantXCore::data::MarketTick;
using QuantXCore::alpha::AlphaEngine;
using QuantXCore::alpha::Signal;
using QuantXCore::alpha::SignalType;
using QuantXCore::alpha::MovingAverageCrossover;
using QuantXCore::alpha::RsiStrategy;
using QuantXCore::risk::RiskEngine;
using QuantXCore::risk::RiskLimits;
using QuantXCore::risk::AccountState;
using QuantXCore::risk::RiskCheckResult;
using QuantXCore::risk::OrderRequest;
using QuantXCore::execution::OrderManager;
using QuantXCore::execution::Order;
using QuantXCore::execution::PositionTracker;
using QuantXCore::execution::PnLTracker;
using QuantXCore::execution::status_to_string;

// Global shutdown flag
std::atomic<bool> g_running{true};

void handle_signal(int signum) {
    (void)signum;
    g_running.store(false, std::memory_order_relaxed);
}

void install_signal_handlers() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
}

struct RunStats {
    std::uint64_t ticks_processed{0};
    std::uint64_t signals_generated{0};
    std::uint64_t orders_approved{0};
    std::uint64_t orders_rejected{0};
    std::uint64_t orders_executed{0};
};

std::string now_timestamp_str() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void print_rejection(const Signal& signal, const RiskCheckResult& result) {
    std::cout << "  [RISK REJECTED] "
              << "symbol=" << signal.symbol
              << " side=" << QuantXCore::alpha::type_to_string(signal.type)
              << " strategy=" << signal.strategy_name
              << " result_code=" << static_cast<int>(result)
              << '\n';
}

// ---------------------------------------------------------------------------
// Core Pipeline: AlphaEngine -> RiskEngine -> OrderManager (OMS) -> PositionTracker
// ---------------------------------------------------------------------------
class TradingPipeline {
public:
    TradingPipeline(AlphaEngine& alpha_engine, 
                    RiskEngine& risk_engine,
                    OrderManager& order_manager,
                    PositionTracker& position_tracker,
                    PnLTracker& pnl_tracker)
        : alpha_engine_(alpha_engine), 
          risk_engine_(risk_engine),
          order_manager_(order_manager),
          position_tracker_(position_tracker),
          pnl_tracker_(pnl_tracker) {}

    void on_tick(const MarketTick& tick) {
        ++stats_.ticks_processed;

        // 1. Update position mark price and unrealized PnL
        position_tracker_.on_tick(tick);

        // 2. Pass tick to strategies
        const std::vector<Signal> signals = alpha_engine_.on_tick(tick);
        stats_.signals_generated += signals.size();

        for (const Signal& signal : signals) {
            if (signal.type == SignalType::HOLD) {
                continue;
            }

            // 3. Risk Check & Position Sizing
            const auto [result, maybe_order_req] =
                risk_engine_.process_signal(signal, kAccountRiskPct);

            if (maybe_order_req.has_value()) {
                ++stats_.orders_approved;

                // 4. Route approved request through OrderManager to Execution Client
                std::optional<Order> executed_order = 
                    order_manager_.execute_approved_request(*maybe_order_req);

                if (executed_order.has_value()) {
                    ++stats_.orders_executed;

                    // 5. Update position state and record PnL snapshot on fill
                    position_tracker_.on_fill(*executed_order);
                    pnl_tracker_.record_snapshot(
                        tick.timestamp,
                        position_tracker_.get_total_realized_pnl(),
                        position_tracker_.get_total_unrealized_pnl()
                    );
                }
            } else {
                ++stats_.orders_rejected;
                print_rejection(signal, result);
            }
        }
    }

    const RunStats& stats() const { return stats_; }

private:
    static constexpr double kAccountRiskPct = 0.02;

    AlphaEngine& alpha_engine_;
    RiskEngine& risk_engine_;
    OrderManager& order_manager_;
    PositionTracker& position_tracker_;
    PnLTracker& pnl_tracker_;
    RunStats stats_;
};

// ---------------------------------------------------------------------------
// Synthetic Market Data Generator
// ---------------------------------------------------------------------------
void run_synthetic_market_simulator(TradingPipeline& pipeline,
                                     int num_ticks = 50,
                                     std::chrono::milliseconds tick_interval =
                                         std::chrono::milliseconds(100)) {
    std::cout << "[SIM] Starting synthetic BTC/USDT tick generator ("
              << num_ticks << " ticks)\n";

    std::mt19937_64 rng(std::random_device{}());
    std::normal_distribution<double> price_step(0.0, 25.0);
    std::uniform_real_distribution<double> volume_dist(0.01, 2.5);
    std::bernoulli_distribution buyer_maker_dist(0.5);

    double price = 65000.0;
    std::uint64_t trade_id = 1;

    for (int i = 0; i < num_ticks && g_running.load(std::memory_order_relaxed); ++i) {
        price = std::max(1.0, price + price_step(rng));

        MarketTick tick;
        tick.timestamp = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        tick.symbol = "BTCUSDT";
        tick.price = price;
        tick.volume = volume_dist(rng);
        tick.isBuyerMaker = buyer_maker_dist(rng);
        tick.trade_id = trade_id++;

        pipeline.on_tick(tick);

        std::this_thread::sleep_for(tick_interval);
    }

    std::cout << "[SIM] Synthetic tick generation complete.\n";
}

void print_summary(const RunStats& stats,
                   const PositionTracker& position_tracker,
                   const PnLTracker& pnl_tracker) {
    std::cout << "\n============================================\n";
    std::cout << " QuantXCore Run Summary  (" << now_timestamp_str() << ")\n";
    std::cout << "--------------------------------------------\n";
    std::cout << "  Total Ticks Processed  : " << stats.ticks_processed << '\n';
    std::cout << "  Alpha Signals Generated: " << stats.signals_generated << '\n';
    std::cout << "  Orders Approved (Risk) : " << stats.orders_approved << '\n';
    std::cout << "  Orders Rejected (Risk) : " << stats.orders_rejected << '\n';
    std::cout << "  Orders Executed (OMS)  : " << stats.orders_executed << '\n';
    std::cout << "--------------------------------------------\n";
    std::cout << "  Realized PnL           : $" << std::fixed << std::setprecision(2) 
              << position_tracker.get_total_realized_pnl() << '\n';
    std::cout << "  Unrealized PnL         : $" << position_tracker.get_total_unrealized_pnl() << '\n';
    std::cout << "  Total PnL              : $" << position_tracker.get_total_pnl() << '\n';
    std::cout << "  PnL Snapshots Recorded : " << pnl_tracker.history().size() << '\n';
    std::cout << "============================================\n";
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    install_signal_handlers();


    // --- STEP 3: Load environment variables and verify keys ---
    QuantXCore::utils::ConfigLoader::load_env_file();

    std::string api_key = QuantXCore::utils::ConfigLoader::get_env("BINANCE_TESTNET_API_KEY");
    if (!api_key.empty() && api_key != "your_binance_testnet_api_key_here") {
        std::cout << "[Config] Binance Testnet API Key loaded successfully.\n";
    } else {
        std::cout << "[Config] Warning: Binance Testnet API Key is missing or default.\n";
    }
    // --------------------------------------------------------


    std::cout << "============================================\n";
    std::cout << " QuantXCore - Quantitative Trading Engine   \n";
    std::cout << " Starting pipeline with OMS & PnL Tracker...\n";
    std::cout << "============================================\n\n";

    // 1. Setup Alpha Strategies
    AlphaEngine alpha_engine;
    auto ma_crossover = std::make_shared<MovingAverageCrossover>("BTCUSDT");
    auto rsi_strategy = std::make_shared<RsiStrategy>("BTCUSDT", 14, 70.0, 30.0);

    alpha_engine.register_strategy(ma_crossover);
    alpha_engine.register_strategy(rsi_strategy);

    // 2. Setup Risk Limits
    RiskLimits risk_limits;
    risk_limits.max_drawdown_pct = 0.10;
    risk_limits.max_daily_loss_pct = 0.05;
    risk_limits.max_open_positions = 3;
    risk_limits.max_position_size_usd = 50000.0;

    AccountState initial_state;
    initial_state.balance = 100000.0;
    initial_state.peak_balance = 100000.0;
    initial_state.daily_start_balance = 100000.0;
    initial_state.open_positions_count = 0;

    RiskEngine risk_engine(risk_limits, initial_state);

    // 3. Setup Order Management System (OMS) and Execution Client
    // --- Mock Execution Client (Default) ---
    auto exec_client = std::make_shared<QuantXCore::execution::MockExecutionClient>();

    // --- Binance Spot Testnet REST Client (Uncomment to use real Testnet API) ---
    /*
    auto exec_client = std::make_shared<QuantXCore::execution::BinanceRestClient>(
        "YOUR_BINANCE_TESTNET_API_KEY",
        "YOUR_BINANCE_TESTNET_API_SECRET",
        "https://testnet.binance.vision"
    );
    */

    OrderManager order_manager(exec_client);

    // 4. Setup Position and PnL Trackers
    PositionTracker position_tracker;
    PnLTracker pnl_tracker(100000.0);

    // 5. Wire Pipeline
    TradingPipeline pipeline(alpha_engine, risk_engine, order_manager, position_tracker, pnl_tracker);

    // 6. Run Market Data Feed (Synthetic vs Live Redis Streamer)
    bool use_live_data = QuantXCore::utils::ConfigLoader::get_env("USE_LIVE_DATA", "false") == "true";

    if (use_live_data) {
        std::cout << "[Live] Initializing Redis Streamer for live data feed...\n";
        
        #if defined(QUANTXCORE_WITH_REDIS)
        std::string redis_host = QuantXCore::utils::ConfigLoader::get_env("REDIS_HOST", "127.0.0.1");
        int redis_port = std::stoi(QuantXCore::utils::ConfigLoader::get_env("REDIS_PORT", "6379"));

        QuantXCore::data::RedisStreamer streamer(redis_host, redis_port, "market_ticks_channel");
        
        streamer.set_callback([&pipeline](const MarketTick& tick) {
            pipeline.on_tick(tick);
        });

        if (streamer.connect()) {
            streamer.subscribe();
            std::cout << "[Live] Connected to Redis stream. Listening for ticks (Press Ctrl+C to stop)...\n";
            
            // Keep running until shutdown signal is received
            while (g_running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            streamer.stop();
        } else {
            std::cerr << "[Error] Failed to connect to Redis. Falling back or exiting.\n";
        }
        #else
        std::cerr << "[Error] QUANTXCORE_WITH_REDIS is not enabled in CMake.\n";
        #endif

    } else {
        // Run standard synthetic simulator feed
        run_synthetic_market_simulator(pipeline, /*num_ticks=*/50, std::chrono::milliseconds(50));
    }

    // 7. Summary
    print_summary(pipeline.stats(), position_tracker, pnl_tracker);

    return EXIT_SUCCESS;
}

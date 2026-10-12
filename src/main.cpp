#include "QuantXCore/alpha/AlphaEngine.hpp"
#include "QuantXCore/alpha/CompositeAlphaStrategy.hpp"
#include "QuantXCore/alpha/MeanReversionStrategy.hpp"
#include "QuantXCore/data/BinanceStreamer.hpp"
#include "QuantXCore/execution/BinanceOmsClient.hpp"
#include "QuantXCore/execution/LiveEngine.hpp"
#include "QuantXCore/persistence/DatabaseManager.hpp"
#include "QuantXCore/risk/RiskEngine.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[Main] Shutdown signal received. Stopping QuantXCore..." << std::endl;
        g_running.store(false);
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "==========================================" << std::endl;
    std::cout << "      Starting QuantXCore Live Bot        " << std::endl;
    std::cout << "==========================================" << std::endl;

    // 0. Capital Allocation Input
    double allocated_capital = 1000.0;
    std::cout << "\n\033[36m\033[1m[STEP 1] Enter allocated capital in USDT (Default: 1000.0): \033[0m";
    std::string input_capital;
    std::getline(std::cin, input_capital);
    
    if (!input_capital.empty()) {
        try {
            allocated_capital = std::stod(input_capital);
        } catch (...) {
            std::cout << "\033[33m[CONFIG] Invalid input! Using default: $1000.0 USDT\033[0m\n";
            allocated_capital = 1000.0;
        }
    }

    // 0.1 Market Mode Selection (SPOT vs FUTURES)
    bool is_spot_mode = true;
    std::cout << "\033[36m\033[1m[STEP 2] Select Market Mode:\n  1) SPOT Trading (Long-only, Spot Rules)\n  2) FUTURES Trading (Long & Short)\nChoice (1 or 2, Default: 1): \033[0m";
    std::string input_mode;
    std::getline(std::cin, input_mode);
    if (input_mode == "2") {
        is_spot_mode = false;
    }

    std::cout << "\n\033[32m\033[1m[CONFIG CONFIRMED]\033[0m\n";
    std::cout << " Active Capital : $" << allocated_capital << " USDT\n";
    std::cout << " Market Mode    : " << (is_spot_mode ? "SPOT (Spot Rules Enabled)" : "FUTURES") << "\n\n";

    // 1. API Keys
    const char* env_api_key = std::getenv("BINANCE_TESTNET_API_KEY");
    const char* env_secret_key = std::getenv("BINANCE_TESTNET_API_SECRET");

    std::string api_key = env_api_key ? env_api_key : "YOUR_API_KEY";
    std::string secret_key = env_secret_key ? env_secret_key : "YOUR_SECRET_KEY";

    // 2. Database Manager
    std::string db_path = "quantxcore_trading.db";
    auto db_manager = std::make_shared<QuantXCore::persistence::DatabaseManager>(db_path);
    if (!db_manager->init()) {
        std::cerr << "[Main] Failed to initialize SQLite database!" << std::endl;
        return 1;
    }

    // 3. Binance Streamer Config
    QuantXCore::data::BinanceStreamerConfig streamer_config;
    streamer_config.market = is_spot_mode ? QuantXCore::data::BinanceMarket::Spot 
                                           : QuantXCore::data::BinanceMarket::UsdMFutures;
    streamer_config.subscriptions = {
        {"BTCUSDT", QuantXCore::data::BinanceStreamKind::AggTrade},
        {"ETHUSDT", QuantXCore::data::BinanceStreamKind::AggTrade}
    };
    QuantXCore::data::BinanceStreamer streamer(streamer_config);

    // 4. Alpha Strategy (Multi-Asset & Multi-Strategy)
    QuantXCore::alpha::AlphaEngine alpha_engine;

    // BTCUSDT Strategies
    auto btc_composite = std::make_shared<QuantXCore::alpha::CompositeAlphaStrategy>("BTCUSDT");
    auto btc_mean_rev  = std::make_shared<QuantXCore::alpha::MeanReversionStrategy>("BTCUSDT");

    // ETHUSDT Strategies
    auto eth_composite = std::make_shared<QuantXCore::alpha::CompositeAlphaStrategy>("ETHUSDT");
    auto eth_mean_rev  = std::make_shared<QuantXCore::alpha::MeanReversionStrategy>("ETHUSDT");

    // Register all 4 instances in AlphaEngine
    alpha_engine.register_strategy(btc_composite);
    alpha_engine.register_strategy(btc_mean_rev);
    alpha_engine.register_strategy(eth_composite);
    alpha_engine.register_strategy(eth_mean_rev);

    // 5. Risk Engine
    QuantXCore::risk::RiskLimits limits;
    limits.allocated_capital_usd = allocated_capital;
    limits.max_position_size_usd = allocated_capital;

    QuantXCore::risk::AccountState initial_state;
    initial_state.balance = allocated_capital; 
    QuantXCore::risk::RiskEngine risk_engine(limits, initial_state);

    // 6. OMS Client
    std::string host = "testnet.binance.vision";
    auto oms = std::make_shared<QuantXCore::execution::BinanceOmsClient>(api_key, secret_key, host, "443");

    // 7. LiveEngine Mode
    auto mode = QuantXCore::execution::LiveEngineMode::PAPER_TRADING;
    double risk_fraction_per_trade = 0.02;

    QuantXCore::execution::LiveEngine engine(
        streamer, alpha_engine, risk_engine, oms, mode, risk_fraction_per_trade
    );

    engine.set_spot_mode(is_spot_mode);
    engine.set_db_manager(db_manager);

    // 8. Start
    engine.start();
    std::cout << "[Main] Engine started in " 
              << (mode == QuantXCore::execution::LiveEngineMode::LIVE_TRADING ? "LIVE" : "PAPER") 
              << " mode on " << (is_spot_mode ? "SPOT" : "FUTURES") << "." << std::endl;

    // 9. Main Event Loop
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // 10. Stop
    std::cout << "[Main] Stopping engine..." << std::endl;
    engine.stop();

    return 0;
}

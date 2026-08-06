#include "QuantXCore/execution/LiveEngine.hpp"
#include "QuantXCore/alpha/RsiStrategy.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

using namespace QuantXCore;

class LiveEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        data::BinanceStreamerConfig config;
        config.subscriptions = {{"BTCUSDT", data::BinanceStreamKind::AggTrade}};
        
        streamer_ = std::make_unique<data::BinanceStreamer>(config);
        alpha_engine_ = std::make_unique<alpha::AlphaEngine>();
        
        risk::RiskLimits limits;
        limits.max_position_size_usd = 5.0;
        limits.max_drawdown_pct = 0.20;
        
        risk::AccountState state;
        state.balance = 10000.0;
        state.peak_balance = 10000.0;
        state.daily_start_balance = 10000.0;
        
        risk_engine_ = std::make_unique<risk::RiskEngine>(limits, state);
    }

    std::unique_ptr<data::BinanceStreamer> streamer_;
    std::unique_ptr<alpha::AlphaEngine> alpha_engine_;
    std::unique_ptr<risk::RiskEngine> risk_engine_;
};

TEST_F(LiveEngineTest, StartsAndStopsCleanly) {
    execution::LiveEngine engine(
        *streamer_, *alpha_engine_, *risk_engine_, nullptr, execution::LiveEngineMode::PAPER_TRADING);

    EXPECT_FALSE(engine.is_running());
    engine.start();
    EXPECT_TRUE(engine.is_running());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    engine.stop();
    EXPECT_FALSE(engine.is_running());
}

TEST_F(LiveEngineTest, ProcessesTicksInPaperTradingMode) {
    auto rsi_strat = std::make_unique<alpha::RsiStrategy>("RSI_Test", 14, 30.0, 70.0);
    alpha_engine_->register_strategy(std::move(rsi_strat));

    execution::LiveEngine engine(
        *streamer_, *alpha_engine_, *risk_engine_, nullptr, execution::LiveEngineMode::PAPER_TRADING);

    engine.start();

    // Simulate incoming tick pushed direct to queue
    data::MarketTick tick;
    tick.symbol = "BTCUSDT";
    tick.price = 50000.0;
    tick.volume = 1.5;
    tick.timestamp = 1600000000000;
    
    streamer_->queue().push(tick);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    engine.stop();
    SUCCEED();
}

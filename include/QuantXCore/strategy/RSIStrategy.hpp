#ifndef QUANTXCORE_RSI_STRATEGY_HPP
#define QUANTXCORE_RSI_STRATEGY_HPP

#include <atomic>
#include <string>
#include <cmath>
#include <cstdint>

namespace quantx {

// Event-driven Signal Type
enum class SignalType { NONE, BUY, SELL };

// QuantXCore Standard Signal Event Structure
struct SignalEvent {
    SignalType type{SignalType::NONE};
    double price{0.0};
    std::string symbol;
    uint64_t timestamp_us{0}; // Microseconds from Epoch
    double expected_profit_pct{0.0};
    double rsi_value{0.0};
};

class RSIStrategy {
public:
    struct Params {
        std::string symbol = "BTCUSDT";
        int period = 14;                  // RSI period (Wilder's)
        double oversold_threshold = 30.0;  // BUY condition
        double overbought_threshold = 70.0;// SELL condition
        double profit_threshold = 0.0035;  // 0.35% (Covers 0.15% Fee + Tax + Slippage)
        double cooldown_seconds = 10.0;    // Signal cool-down
        int warmup_ticks = 14;            // Minimum ticks required before generating signals
    };

    explicit RSIStrategy(const Params& params);

    // Core Event Loop Execution Method
    // Timestamp-ը փոխանցվում է Market Feed-ից (No syscall overhead)
    SignalEvent onTick(double price, uint64_t timestamp_us);

    // Warmup Status Check
    bool isWarmedUp() const { return ticks_processed_ >= warmup_ticks_; }

private:
    const std::string symbol_;
    const int period_;
    const double oversold_thresh_;
    const double overbought_thresh_;
    const double profit_thresh_;
    const double cooldown_us_; // Stored in microseconds for zero-conversion runtime
    const int warmup_ticks_;

    // Atomic timers for Non-Blocking Thread Safety
    std::atomic<uint64_t> last_buy_time_us_{0};
    std::atomic<uint64_t> last_sell_time_us_{0};

    // RSI Engine State
    double prev_price_{0.0};
    double avg_gain_{0.0};
    double avg_loss_{0.0};
    int ticks_processed_{0};
};

} // namespace quantx

#endif // QUANTXCORE_RSI_STRATEGY_HPP

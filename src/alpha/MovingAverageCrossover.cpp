#include "QuantXCore/alpha/MovingAverageCrossover.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace QuantXCore::alpha {

MovingAverageCrossover::MovingAverageCrossover(std::string symbol)
    : symbol_(std::move(symbol)) {}

std::string MovingAverageCrossover::name() const {
    return "MovingAverageCrossover(" + symbol_ + "," +
           std::to_string(kFastPeriod) + "," + std::to_string(kSlowPeriod) + ")";
}

std::optional<Signal> MovingAverageCrossover::on_tick(const QuantXCore::data::MarketTick& tick) {
    if (tick.symbol != symbol_) {
        return std::nullopt;
    }

    fast_ma_.push(tick.price);
    slow_ma_.push(tick.price);

    if (!fast_ma_.is_full() || !slow_ma_.is_full()) {
        return std::nullopt;
    }

    const double fast_avg = fast_ma_.average();
    const double slow_avg = slow_ma_.average();

    std::optional<Signal> signal;

    if (has_prev_) {
        const bool crossed_up = (prev_fast_ <= prev_slow_) && (fast_avg > slow_avg);
        const bool crossed_down = (prev_fast_ >= prev_slow_) && (fast_avg < slow_avg);

        if (crossed_up || crossed_down) {
            const double spread = std::abs(fast_avg - slow_avg);
            const double strength =
                slow_avg != 0.0 ? std::clamp(spread / slow_avg, 0.0, 1.0) : 0.0;

            signal = Signal{
                .symbol = symbol_,
                .type = crossed_up ? SignalType::BUY : SignalType::SELL,
                .price = tick.price,
                .strength = strength,
                .timestamp = tick.timestamp,
                .strategy_name = name(),
            };
        }
    }

    prev_fast_ = fast_avg;
    prev_slow_ = slow_avg;
    has_prev_ = true;

    return signal;
}

void MovingAverageCrossover::reset() {
    fast_ma_.reset();
    slow_ma_.reset();
    prev_fast_ = 0.0;
    prev_slow_ = 0.0;
    has_prev_ = false;
}

}  // namespace QuantXCore::alpha

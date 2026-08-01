#include "QuantXCore/alpha/RsiStrategy.hpp"
#include <cmath>
#include <algorithm>

namespace QuantXCore::alpha {

RsiStrategy::RsiStrategy(std::string symbol, std::size_t period, double overbought, double oversold)
    : symbol_(std::move(symbol)),
      period_(std::min(period, MAX_WARMUP_PERIOD)),
      overbought_(overbought),
      oversold_(oversold) {
}

std::optional<Signal> RsiStrategy::on_tick(const QuantXCore::data::MarketTick& tick) {
    if (tick.symbol != symbol_) {
        return std::nullopt;
    }

    if (tick_count_ == 0) {
        prev_price_ = tick.price;
        tick_count_++;
        return Signal{symbol_, SignalType::HOLD, tick.price, 0.0, tick.timestamp, name()};
    }

    double change = tick.price - prev_price_;
    prev_price_ = tick.price;
    tick_count_++;

    if (!is_warmed_up_) {
        price_changes_[change_count_++] = change;
        
        if (change_count_ == period_) {
            double sum_gain = 0.0;
            double sum_loss = 0.0;
            for (std::size_t i = 0; i < period_; ++i) {
                if (price_changes_[i] > 0) sum_gain += price_changes_[i];
                else sum_loss += std::abs(price_changes_[i]);
            }
            avg_gain_ = sum_gain / static_cast<double>(period_);
            avg_loss_ = sum_loss / static_cast<double>(period_);

            if (avg_loss_ == 0.0) {
                current_rsi_ = 100.0;
            } else {
                double rs = avg_gain_ / avg_loss_;
                current_rsi_ = 100.0 - (100.0 / (1.0 + rs));
            }
            is_warmed_up_ = true;
        } else {
            return Signal{symbol_, SignalType::HOLD, tick.price, 0.0, tick.timestamp, name()};
        }
    } else {
        // Wilder's Smoothing
        double gain = (change > 0) ? change : 0.0;
        double loss = (change < 0) ? std::abs(change) : 0.0;

        double p = static_cast<double>(period_);
        avg_gain_ = ((avg_gain_ * (p - 1.0)) + gain) / p;
        avg_loss_ = ((avg_loss_ * (p - 1.0)) + loss) / p;

        if (avg_loss_ == 0.0) {
            current_rsi_ = 100.0;
        } else {
            double rs = avg_gain_ / avg_loss_;
            current_rsi_ = 100.0 - (100.0 / (1.0 + rs));
        }
    }

    SignalType type = SignalType::HOLD;
    double strength = 0.0;

    if (current_rsi_ < oversold_) {
        type = SignalType::BUY;
        strength = (oversold_ - current_rsi_) / oversold_;
    } else if (current_rsi_ > overbought_) {
        type = SignalType::SELL;
        strength = (current_rsi_ - overbought_) / (100.0 - overbought_);
    }

    return Signal{
        symbol_,
        type,
        tick.price,
        std::clamp(strength, 0.0, 1.0),
        tick.timestamp,
        name()
    };
}

void RsiStrategy::reset() {
    prev_price_ = 0.0;
    avg_gain_ = 0.0;
    avg_loss_ = 0.0;
    current_rsi_ = 50.0;
    tick_count_ = 0;
    is_warmed_up_ = false;
    change_count_ = 0;
}

} // namespace QuantXCore::alpha

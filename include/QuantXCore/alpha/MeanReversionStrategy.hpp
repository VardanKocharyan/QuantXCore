#pragma once

#include "QuantXCore/alpha/AlphaStrategy.hpp"
#include "QuantXCore/data/MarketTick.hpp"
#include <deque>
#include <cmath>
#include <string>
#include <optional>
#include <algorithm>

namespace QuantXCore::alpha {

class RollingSMA {
public:
    explicit RollingSMA(int period = 20) : period_(period) {}
    void push(double x) {
        buf_.push_back(x);
        sum_ += x;
        if (static_cast<int>(buf_.size()) > period_) {
            sum_ -= buf_.front();
            buf_.pop_front();
        }
    }
    void reset() { buf_.clear(); sum_ = 0.0; }
    bool ready() const { return static_cast<int>(buf_.size()) == period_; }
    double mean() const { return ready() ? sum_ / period_ : 0.0; }
    double stddev() const {
        if (!ready()) return 0.0;
        double m = mean();
        double acc = 0.0;
        for (double v : buf_) acc += (v - m) * (v - m);
        return std::sqrt(acc / period_);
    }
private:
    int period_;
    std::deque<double> buf_;
    double sum_ = 0.0;
};

class RollingRSI {
public:
    explicit RollingRSI(int period = 14) : period_(period) {}
    void push(double price) {
        if (!has_prev_) { prev_price_ = price; has_prev_ = true; return; }
        double change = price - prev_price_;
        prev_price_ = price;
        double gain = change > 0 ? change : 0.0;
        double loss = change < 0 ? -change : 0.0;
        if (count_ < period_) {
            gain_sum_ += gain; loss_sum_ += loss; ++count_;
            if (count_ == period_) {
                avg_gain_ = gain_sum_ / period_;
                avg_loss_ = loss_sum_ / period_;
                initialized_ = true;
            }
        } else {
            avg_gain_ = (avg_gain_ * (period_ - 1) + gain) / period_;
            avg_loss_ = (avg_loss_ * (period_ - 1) + loss) / period_;
        }
    }
    void reset() {
        has_prev_ = false; initialized_ = false; prev_price_ = 0.0;
        gain_sum_ = loss_sum_ = avg_gain_ = avg_loss_ = 0.0; count_ = 0;
    }
    bool ready() const { return initialized_; }
    double value() const {
        if (!initialized_) return 50.0;
        if (avg_loss_ == 0.0) return 100.0;
        double rs = avg_gain_ / avg_loss_;
        return 100.0 - (100.0 / (1.0 + rs));
    }
private:
    int period_;
    bool has_prev_ = false, initialized_ = false;
    double prev_price_ = 0.0, gain_sum_ = 0.0, loss_sum_ = 0.0, avg_gain_ = 0.0, avg_loss_ = 0.0;
    int count_ = 0;
};

class RollingATR {
public:
    explicit RollingATR(int period = 14) : period_(period) {}
    void push(double price) {
        if (!has_prev_) { prev_price_ = price; has_prev_ = true; return; }
        double tr = std::fabs(price - prev_price_);
        prev_price_ = price;
        if (count_ < period_) {
            sum_ += tr; ++count_;
            if (count_ == period_) { atr_ = sum_ / period_; initialized_ = true; }
        } else {
            atr_ = (atr_ * (period_ - 1) + tr) / period_;
        }
    }
    void reset() {
        has_prev_ = false; initialized_ = false; prev_price_ = 0.0; sum_ = atr_ = 0.0; count_ = 0;
    }
    bool ready() const { return initialized_; }
    double value() const { return initialized_ ? atr_ : 0.0; }
private:
    int period_;
    bool has_prev_ = false, initialized_ = false;
    double prev_price_ = 0.0, sum_ = 0.0, atr_ = 0.0;
    int count_ = 0;
};

struct MeanReversionConfig {
    int bb_period = 20;
    double bb_stddev_mult = 2.0;
    int rsi_period = 14;
    double rsi_oversold = 30.0;
    double rsi_exit_overbought = 65.0;
    int atr_period = 14;
    int atr_avg_period = 50;
    double atr_regime_mult = 1.5;
    double lower_band_buffer = 0.0005;
    double trend_filter_atr_k = 3.0;
    double tp_atr_mult = 2.0; // Ավելացված է TP հեռավորությունը
    double sl_atr_mult = 1.5;
    int cooldown_ticks = 30;  // 30 tick դադար դիրքը փակելուց հետո
};

class MeanReversionStrategy : public AlphaStrategy {
public:
    enum class PositionState { FLAT, LONG };

    explicit MeanReversionStrategy(std::string symbol, MeanReversionConfig cfg = {})
        : symbol_(std::move(symbol)), cfg_(cfg),
          bb_(cfg.bb_period), rsi_(cfg.rsi_period),
          atr_(cfg.atr_period), atr_avg_(cfg.atr_avg_period) {}

    [[nodiscard]] std::string name() const override {
        return "MeanReversionStrategy_" + symbol_;
    }

    std::optional<Signal> on_tick(const data::MarketTick& tick) override {
        if (tick.symbol != symbol_) return std::nullopt;

        update_indicators(tick.price);
        if (!indicators_ready()) return std::nullopt;

        if (cooldown_counter_ > 0) {
            --cooldown_counter_;
            return std::nullopt;
        }

        if (state_ == PositionState::FLAT) {
            return evaluate_entry(tick);
        } else {
            return evaluate_exit(tick);
        }
    }

    void reset() override {
        state_ = PositionState::FLAT;
        entry_price_ = stop_loss_ = take_profit_ = 0.0;
        cooldown_counter_ = 0;
        bb_.reset(); rsi_.reset(); atr_.reset(); atr_avg_.reset();
    }

private:
    void update_indicators(double price) {
        bb_.push(price);
        rsi_.push(price);
        atr_.push(price);
        if (atr_.ready()) atr_avg_.push(atr_.value());
    }

    bool indicators_ready() const {
        return bb_.ready() && rsi_.ready() && atr_.ready() && atr_avg_.ready();
    }

    std::optional<Signal> evaluate_entry(const data::MarketTick& tick) {
        const double mid = bb_.mean();
        const double lower = mid - cfg_.bb_stddev_mult * bb_.stddev();
        const double rsi = rsi_.value();
        const double atr = atr_.value();
        const double atrAvg = atr_avg_.mean();

        bool cond_band = tick.price <= lower * (1.0 + cfg_.lower_band_buffer);
        bool cond_rsi = rsi <= cfg_.rsi_oversold;
        bool cond_vol = atr <= cfg_.atr_regime_mult * atrAvg;
        bool cond_trend = tick.price >= mid - cfg_.trend_filter_atr_k * atr;

        if (cond_band && cond_rsi && cond_vol && cond_trend) {
            state_ = PositionState::LONG;
            entry_price_ = tick.price;

            // TP-ն առնվազն Entry + 0.3% պետք է լինի fee-ն ծածկելու համար
            double min_tp = entry_price_ * 1.003;
            take_profit_ = std::max({mid, entry_price_ + cfg_.tp_atr_mult * atr, min_tp});
            stop_loss_   = entry_price_ - cfg_.sl_atr_mult * atr;

            Signal signal;
            signal.symbol = tick.symbol;
            signal.type = SignalType::BUY;
            signal.price = tick.price;
            signal.strength = 0.85;
            signal.strategy_name = name();
            return signal;
        }
        return std::nullopt;
    }

    std::optional<Signal> evaluate_exit(const data::MarketTick& tick) {
        const double rsi = rsi_.value();
        bool hit_tp = tick.price >= take_profit_;
        bool hit_sl = tick.price <= stop_loss_;
        bool rsi_exit = rsi >= cfg_.rsi_exit_overbought;

        if (hit_tp || hit_sl || rsi_exit) {
            state_ = PositionState::FLAT;
            cooldown_counter_ = cfg_.cooldown_ticks; // Սկսում ենք Cooldown-ը

            Signal signal;
            signal.symbol = tick.symbol;
            signal.type = SignalType::SELL;
            signal.price = tick.price;
            signal.strength = 1.0;
            signal.strategy_name = name();

            entry_price_ = stop_loss_ = take_profit_ = 0.0;
            return signal;
        }
        return std::nullopt;
    }

    std::string symbol_;
    MeanReversionConfig cfg_;
    PositionState state_ = PositionState::FLAT;
    int cooldown_counter_ = 0;

    double entry_price_ = 0.0;
    double stop_loss_   = 0.0;
    double take_profit_ = 0.0;

    RollingSMA bb_;
    RollingRSI rsi_;
    RollingATR atr_;
    RollingSMA atr_avg_;
};

} // namespace QuantXCore::alpha

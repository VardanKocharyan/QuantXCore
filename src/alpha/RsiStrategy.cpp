#include "QuantXCore/alpha/RsiStrategy.hpp"
#include <algorithm>

namespace QuantXCore::alpha {

RsiStrategy::RsiStrategy(std::string symbol,
                         int period,
                         double overbought,
                         double oversold,
                         double cooldown_seconds,
                         double profit_threshold)
    : symbol_(std::move(symbol)),
      period_(period),
      overbought_(overbought),
      oversold_(oversold),
      cooldown_seconds_(cooldown_seconds),
      profit_threshold_(profit_threshold) {}

void RsiStrategy::reset() {
  std::lock_guard<std::mutex> lock(candle_mutex_);
  close_prices_.clear();
  current_candle_ = Candle{};
  current_rsi_ = 50.0;
  last_signal_time_.store(0);
}

std::optional<Signal> RsiStrategy::on_tick(const QuantXCore::data::MarketTick& tick) {
  // Ignore ticks from other symbols if specified
  if (!symbol_.empty() && tick.symbol != symbol_) {
    return std::nullopt;
  }

  uint64_t now = tick.timestamp;

  {
    std::lock_guard<std::mutex> lock(candle_mutex_);
    if (current_candle_.open == 0.0) {
      current_candle_ = {tick.price, tick.price, tick.price, tick.price, now};
    } else {
      current_candle_.high = std::max(current_candle_.high, tick.price);
      current_candle_.low = std::min(current_candle_.low, tick.price);
      current_candle_.close = tick.price;
    }

    // 1-Minute Candle Aggregation (60,000 ms)
    if (now - current_candle_.start_time >= 60000) {
      close_prices_.push_back(current_candle_.close);
      if (close_prices_.size() > static_cast<std::size_t>(period_ + 1)) {
        close_prices_.erase(close_prices_.begin());
      }
      current_candle_ = {tick.price, tick.price, tick.price, tick.price, now};
    }
  }

  // Cooldown check
  uint64_t last = last_signal_time_.load();
  if (last != 0 && (now - last) < static_cast<uint64_t>(cooldown_seconds_ * 1000.0)) {
    return std::nullopt;
  }

  // Check warm-up status
  std::lock_guard<std::mutex> lock(candle_mutex_);
  if (close_prices_.size() <= static_cast<std::size_t>(period_)) {
    return std::nullopt;
  }

  current_rsi_ = compute_rsi();

  // BUY Signal: RSI < oversold + Break-Even Profit Filter
  if (current_rsi_ < oversold_) {
    double highest = *std::max_element(close_prices_.begin(), close_prices_.end());
    double profit_potential = (highest - tick.price) / tick.price;

    if (profit_potential < profit_threshold_) {
      return std::nullopt; // Filter out noise
    }

    last_signal_time_.store(now);

    Signal sig;
    sig.symbol = tick.symbol;
    sig.type = SignalType::BUY;
    sig.price = tick.price;
    sig.timestamp = tick.timestamp;
    sig.strategy_name = name();
    return sig;
  }

  // SELL Signal: RSI > overbought
  if (current_rsi_ > overbought_) {
    last_signal_time_.store(now);
  
    Signal sig;
    sig.symbol = tick.symbol;
    sig.type = SignalType::SELL;
    sig.price = tick.price;
    sig.timestamp = tick.timestamp;
    sig.strategy_name = name();
    return sig;
  }

  return std::nullopt;
}

double RsiStrategy::compute_rsi() const {
  double gain_sum = 0.0, loss_sum = 0.0;
  for (std::size_t i = 1; i < close_prices_.size(); ++i) {
    double diff = close_prices_[i] - close_prices_[i - 1];
    if (diff > 0) gain_sum += diff;
    else loss_sum -= diff;
  }

  double avg_gain = gain_sum / static_cast<double>(period_);
  double avg_loss = loss_sum / static_cast<double>(period_);
  if (avg_loss == 0.0) return 100.0;

  double rs = avg_gain / avg_loss;
  return 100.0 - (100.0 / (1.0 + rs));
}

}  // namespace QuantXCore::alpha

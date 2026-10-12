#include "QuantXCore/alpha/CompositeAlphaStrategy.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace QuantXCore::alpha {

CompositeAlphaStrategy::CompositeAlphaStrategy(std::string symbol)
    : symbol_(std::move(symbol)) {}

void CompositeAlphaStrategy::update_indicators(double price, double volume, double high, double low) {
  // 1. Update EMAs (9, 21, 50)
  constexpr double k_fast = 2.0 / (9.0 + 1.0);
  constexpr double k_mid  = 2.0 / (21.0 + 1.0);
  constexpr double k_long = 2.0 / (50.0 + 1.0);

  ema_fast_ = (ema_fast_ == 0.0) ? price : (price * k_fast) + (ema_fast_ * (1.0 - k_fast));
  ema_mid_  = (ema_mid_  == 0.0) ? price : (price * k_mid)  + (ema_mid_  * (1.0 - k_mid));
  ema_long_ = (ema_long_ == 0.0) ? price : (price * k_long) + (ema_long_ * (1.0 - k_long));

  // 2. Price history for RSI
  price_history_.push_back(price);
  if (price_history_.size() > kRsiPeriod + 1) {
    price_history_.pop_front();
  }

  // 3. Volume history for 20-period SMA
  volume_history_.push_back(volume);
  if (volume_history_.size() > kVolumeSmaPeriod) {
    volume_history_.pop_front();
  }

  // 4. Calculate True Range (TR)
  double tr = high - low;
  if (prev_close_ > 0.0) {
    tr = std::max({high - low, std::abs(high - prev_close_), std::abs(low - prev_close_)});
  }
  prev_close_ = price;

  tr_history_.push_back(tr);
  if (tr_history_.size() > kAtrPeriod) {
    tr_history_.pop_front();
  }
}

double CompositeAlphaStrategy::calculate_rsi() const {
  if (price_history_.size() < kRsiPeriod + 1) return 50.0;

  double gains = 0.0;
  double losses = 0.0;

  for (std::size_t i = 1; i < price_history_.size(); ++i) {
    double change = price_history_[i] - price_history_[i - 1];
    if (change > 0) gains += change;
    else losses -= change;
  }

  if (losses == 0.0) return 100.0;
  double rs = (gains / kRsiPeriod) / (losses / kRsiPeriod);
  return 100.0 - (100.0 / (1.0 + rs));
}

double CompositeAlphaStrategy::calculate_atr() const {
  if (tr_history_.empty()) return 0.0;
  double sum = std::accumulate(tr_history_.begin(), tr_history_.end(), 0.0);
  return sum / tr_history_.size();
}

void CompositeAlphaStrategy::warmup(const std::vector<HistoricalCandle>& candles) {
  for (const auto& c : candles) {
    update_indicators(c.close, c.volume, c.high, c.low);
  }
  is_warmed_up_ = true;
}

std::optional<Signal> CompositeAlphaStrategy::on_tick(const QuantXCore::data::MarketTick& tick) {
  if (tick.symbol != symbol_ || tick.price <= 0.0) return std::nullopt;

  const std::uint64_t bucket = (tick.timestamp / kCandleIntervalMs) * kCandleIntervalMs;

  if (!bucket_active_) {
    current_bucket_start_ = bucket;
    bucket_open_ = bucket_high_ = bucket_low_ = bucket_close_ = tick.price;
    bucket_volume_ = tick.volume;
    bucket_active_ = true;
    return std::nullopt;
  }

  // Intra-bar tick update
  if (bucket == current_bucket_start_) {
    bucket_high_   = std::max(bucket_high_, tick.price);
    bucket_low_    = std::min(bucket_low_, tick.price);
    bucket_close_  = tick.price;
    bucket_volume_ += tick.volume;
    return std::nullopt;
  }

  // Bar closed: Execute indicator updates on OHLCV bar
  update_indicators(bucket_close_, bucket_volume_, bucket_high_, bucket_low_);

  // Reset bucket for new incoming bar
  current_bucket_start_ = bucket;
  bucket_open_ = bucket_high_ = bucket_low_ = bucket_close_ = tick.price;
  bucket_volume_ = tick.volume;

  if (price_history_.size() < kRsiPeriod + 1 ||
      volume_history_.size() < kVolumeSmaPeriod ||
      tr_history_.size() < kAtrPeriod) {
    return std::nullopt;
  }

  const double rsi = calculate_rsi();
  const double atr = calculate_atr();
  const double avg_volume = std::accumulate(volume_history_.begin(), volume_history_.end(), 0.0) / volume_history_.size();

  const bool ema_trend_confirm = (ema_fast_ > ema_mid_) && (tick.price > ema_long_);
  const bool rsi_confirm       = (rsi >= 50.0) && (rsi <= 70.0);
  const bool volume_confirm    = (bucket_volume_ >= avg_volume * 1.5);

  if (ema_trend_confirm && rsi_confirm && volume_confirm) {
    Signal signal;
    signal.symbol = tick.symbol;
    signal.price = tick.price;
    signal.strategy_name = name();
    signal.timestamp = tick.timestamp;
    signal.type = SignalType::BUY;
    signal.strength = 1.0;
    signal.atr = atr;

    const double sl_dist = std::max(1.5 * atr, tick.price * 0.005);
    const double tp_dist = std::max(2.5 * atr, tick.price * 0.010);

    signal.stop_loss = tick.price - sl_dist;
    signal.take_profit = tick.price + tp_dist;

    return signal;
  }

  return std::nullopt;
}

void CompositeAlphaStrategy::reset() {
  ema_fast_ = 0.0;
  ema_mid_  = 0.0;
  ema_long_ = 0.0;
  prev_close_ = 0.0;
  price_history_.clear();
  volume_history_.clear();
  tr_history_.clear();
  is_warmed_up_ = false;
  bucket_active_ = false;
}

}  // namespace QuantXCore::alpha

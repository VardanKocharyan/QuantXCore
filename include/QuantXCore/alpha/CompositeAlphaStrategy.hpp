#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "QuantXCore/alpha/AlphaStrategy.hpp"
#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/data/MarketTick.hpp"

namespace QuantXCore::alpha {

struct HistoricalCandle {
  double open{0.0};
  double high{0.0};
  double low{0.0};
  double close{0.0};
  double volume{0.0};
};

class CompositeAlphaStrategy final : public AlphaStrategy {
 public:
  explicit CompositeAlphaStrategy(std::string symbol = "BTCUSDT");
  ~CompositeAlphaStrategy() override = default;

  [[nodiscard]] std::string name() const override { return "CompositeAlphaStrategy"; }
  std::optional<Signal> on_tick(const QuantXCore::data::MarketTick& tick) override;
  void reset() override;

  void warmup(const std::vector<HistoricalCandle>& candles);

 private:
  void update_indicators(double price, double volume, double high, double low);
  double calculate_rsi() const;
  double calculate_atr() const;

  std::string symbol_;

  static constexpr std::size_t kRsiPeriod = 14;
  static constexpr std::size_t kVolumeSmaPeriod = 20;
  static constexpr std::size_t kAtrPeriod = 14;

  static constexpr std::uint64_t kCandleIntervalMs = 60'000; // 1-minute Candle Aggregation

  std::uint64_t current_bucket_start_{0};
  double bucket_open_{0.0};
  double bucket_high_{0.0};
  double bucket_low_{0.0};
  double bucket_close_{0.0};
  double bucket_volume_{0.0};
  bool bucket_active_{false};

  double ema_fast_{0.0};  // EMA 9
  double ema_mid_{0.0};   // EMA 21
  double ema_long_{0.0};  // EMA 50
  double prev_close_{0.0};

  std::deque<double> price_history_;
  std::deque<double> volume_history_;
  std::deque<double> tr_history_;

  bool is_warmed_up_{false};
};

}  // namespace QuantXCore::alpha

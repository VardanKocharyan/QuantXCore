#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "QuantXCore/alpha/AlphaStrategy.hpp"
#include "QuantXCore/data/MarketTick.hpp"

namespace QuantXCore::alpha {

struct Candle {
  double open{0.0};
  double high{0.0};
  double low{0.0};
  double close{0.0};
  uint64_t start_time{0};
};

class RsiStrategy : public AlphaStrategy {
 public:
  explicit RsiStrategy(std::string symbol = "BTCUSDT",
                       int period = 14,
                       double overbought = 70.0,
                       double oversold = 30.0,
                       double cooldown_seconds = 10.0,
                       double profit_threshold = 0.0035);

  [[nodiscard]] std::string name() const override { return "RsiStrategy"; }
  std::optional<Signal> on_tick(const QuantXCore::data::MarketTick& tick) override;
  void reset() override;

  // Preserved original getters
  [[nodiscard]] std::string get_symbol() const { return symbol_; }
  [[nodiscard]] bool is_warmed_up() const { return close_prices_.size() >= static_cast<std::size_t>(period_); }
  [[nodiscard]] double get_rsi() const { return current_rsi_; }

 private:
  double compute_rsi() const;

  std::string symbol_;
  int period_;
  double overbought_;
  double oversold_;
  double cooldown_seconds_;
  double profit_threshold_;

  double current_rsi_{50.0};
  std::atomic<uint64_t> last_signal_time_{0};
  mutable std::mutex candle_mutex_;
  Candle current_candle_;
  std::vector<double> close_prices_;
};

}  // namespace QuantXCore::alpha

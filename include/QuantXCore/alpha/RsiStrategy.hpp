#pragma once

#include "QuantXCore/alpha/AlphaStrategy.hpp"
#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/data/MarketTick.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace QuantXCore::alpha {

class RsiStrategy final : public AlphaStrategy {
public:
    explicit RsiStrategy(
        std::string symbol,
        std::size_t period = 14,
        double overbought = 70.0,
        double oversold = 30.0
    );

    [[nodiscard]] std::string name() const override { return "RSI_Strategy"; }
    [[nodiscard]] std::string_view get_symbol() const noexcept { return symbol_; }

    std::optional<Signal> on_tick(const QuantXCore::data::MarketTick& tick) override;
    void reset() override;

    [[nodiscard]] double get_rsi() const noexcept { return current_rsi_; }
    [[nodiscard]] bool is_warmed_up() const noexcept { return is_warmed_up_; }

private:
    std::string symbol_;
    std::size_t period_;
    double overbought_;
    double oversold_;

    double prev_price_{0.0};
    double avg_gain_{0.0};
    double avg_loss_{0.0};
    double current_rsi_{50.0};
    
    std::size_t tick_count_{0};
    bool is_warmed_up_{false};
    
    static constexpr std::size_t MAX_WARMUP_PERIOD = 200;
    std::array<double, MAX_WARMUP_PERIOD> price_changes_{};
    std::size_t change_count_{0};
};

} // namespace QuantXCore::alpha

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include "QuantXCore/alpha/AlphaStrategy.hpp"
#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/data/MarketTick.hpp"

namespace QuantXCore::alpha {

namespace detail {

template <std::size_t Capacity>
class SmaRingBuffer {
    static_assert(Capacity > 0, "SmaRingBuffer capacity must be > 0");

public:
    void push(double value) noexcept {
        if (count_ < Capacity) {
            buffer_[head_] = value;
            sum_ += value;
            ++count_;
        } else {
            sum_ -= buffer_[head_];
            buffer_[head_] = value;
            sum_ += value;
        }
        head_ = (head_ + 1) % Capacity;
    }

    [[nodiscard]] bool is_full() const noexcept { return count_ == Capacity; }

    [[nodiscard]] double average() const noexcept {
        return count_ == 0 ? 0.0 : sum_ / static_cast<double>(count_);
    }

    void reset() noexcept {
        buffer_.fill(0.0);
        head_ = 0;
        count_ = 0;
        sum_ = 0.0;
    }

private:
    std::array<double, Capacity> buffer_{};
    std::size_t head_{0};
    std::size_t count_{0};
    double sum_{0.0};
};

}  // namespace detail

class MovingAverageCrossover final : public AlphaStrategy {
public:
    static constexpr std::size_t kFastPeriod = 5;
    static constexpr std::size_t kSlowPeriod = 20;

    explicit MovingAverageCrossover(std::string symbol);

    [[nodiscard]] std::string name() const override;
    std::optional<Signal> on_tick(const QuantXCore::data::MarketTick& tick) override;
    void reset() override;

private:
    std::string symbol_;

    detail::SmaRingBuffer<kFastPeriod> fast_ma_;
    detail::SmaRingBuffer<kSlowPeriod> slow_ma_;

    double prev_fast_{0.0};
    double prev_slow_{0.0};
    bool has_prev_{false};
};

}  // namespace QuantXCore::alpha

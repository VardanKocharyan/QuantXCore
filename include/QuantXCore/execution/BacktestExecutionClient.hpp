#pragma once
// ============================================================================
// QuantXCore::execution::BacktestExecutionClient
// ============================================================================

#include <cstdint>
#include <string>

#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/execution/IExecutionClient.hpp"
#include "QuantXCore/execution/Order.hpp"

namespace QuantXCore::execution {

struct BacktestExecutionConfig {
    double slippage_bps{1.0};    // basis points of adverse price movement per fill
    double commission_bps{2.0};  // basis points of notional charged as commission
};

class BacktestExecutionClient final : public IExecutionClient {
public:
    explicit BacktestExecutionClient(BacktestExecutionConfig config) noexcept
        : config_(config) {}

    bool submit_order(Order& order) override {
        if (order.quantity <= 0.0 || order.price <= 0.0) {
            order.status = OrderStatus::REJECTED;
            return false;
        }

        const double slippage_factor = config_.slippage_bps / 10'000.0;
        const double fill_price = (order.side == alpha::SignalType::BUY)
            ? order.price * (1.0 + slippage_factor)
            : order.price * (1.0 - slippage_factor);

        const double fill_qty = (order.filled_quantity > 0.0) ? order.filled_quantity : order.quantity;
        const double notional = fill_price * fill_qty;
        const double commission = notional * (config_.commission_bps / 10'000.0);

        order.exchange_order_id = make_exchange_order_id();
        order.avg_fill_price = fill_price;
        if (order.filled_quantity <= 0.0) {
            order.filled_quantity = order.quantity;
        }
        order.status = OrderStatus::FILLED;

        cumulative_commission_ += commission;
        cumulative_notional_ += notional;
        ++orders_filled_;

        return true;
    }

    bool cancel_order(const std::string& /*client_order_id*/) override {
        return false;
    }

    [[nodiscard]] double total_commission_paid() const noexcept { return cumulative_commission_; }
    [[nodiscard]] double total_notional_traded() const noexcept { return cumulative_notional_; }
    [[nodiscard]] std::uint64_t total_orders_filled() const noexcept { return orders_filled_; }

    void reset() noexcept {
        cumulative_commission_ = 0.0;
        cumulative_notional_ = 0.0;
        orders_filled_ = 0;
        next_exchange_order_id_ = 1;
    }

    [[nodiscard]] const BacktestExecutionConfig& config() const noexcept { return config_; }

private:
    [[nodiscard]] std::string make_exchange_order_id() {
        return "BT-" + std::to_string(next_exchange_order_id_++);
    }

    BacktestExecutionConfig config_;
    double        cumulative_commission_{0.0};
    double        cumulative_notional_{0.0};
    std::uint64_t orders_filled_{0};
    std::uint64_t next_exchange_order_id_{1};
};

}  // namespace QuantXCore::execution

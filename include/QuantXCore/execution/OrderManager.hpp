#pragma once

#include "QuantXCore/execution/Order.hpp"
#include "QuantXCore/execution/IExecutionClient.hpp"
#include "QuantXCore/risk/RiskEngine.hpp"
#include <unordered_map>
#include <memory>
#include <atomic>

namespace QuantXCore::execution {

class OrderManager {
public:
    explicit OrderManager(std::shared_ptr<IExecutionClient> client)
        : execution_client_(std::move(client)) {}

    std::optional<Order> execute_approved_request(const risk::OrderRequest& req) {
        Order order;
        order.client_order_id = "ORD_" + std::to_string(++order_id_seq_);
        order.symbol = req.symbol;
        order.side = req.side;
        order.price = req.price;
        order.quantity = req.quantity;
        order.strategy_name = req.strategy_name;
        order.status = OrderStatus::PENDING_SUBMIT;

        if (execution_client_->submit_order(order)) {
            active_orders_[order.client_order_id] = order;
            return order;
        }

        order.status = OrderStatus::REJECTED;
        return std::nullopt;
    }

    const std::unordered_map<std::string, Order>& get_orders() const {
        return active_orders_;
    }

private:
    std::shared_ptr<IExecutionClient> execution_client_;
    std::unordered_map<std::string, Order> active_orders_;
    std::atomic<std::uint64_t> order_id_seq_{0};
};

} // namespace QuantXCore::execution

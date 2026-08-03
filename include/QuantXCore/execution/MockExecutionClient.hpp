#pragma once

#include "QuantXCore/execution/IExecutionClient.hpp"
#include <iostream>
#include <atomic>

namespace QuantXCore::execution {

class MockExecutionClient : public IExecutionClient {
public:
    bool submit_order(Order& order) override {
        // Սիմուլյացիա ենք անում բորսայի արագ արձագանքը
        order.exchange_order_id = "EXCH_" + std::to_string(++id_counter_);
        order.status = OrderStatus::FILLED;
        order.filled_quantity = order.quantity;
        order.avg_fill_price = order.price;

        std::cout << "  [MOCK EXCH EXECUTE] Order " << order.client_order_id 
                  << " -> " << order.exchange_order_id 
                  << " FILLED @ $" << order.avg_fill_price << "\n";
        return true;
    }

    bool cancel_order(const std::string& client_order_id) override {
        (void)client_order_id;
        return true;
    }

private:
    std::atomic<std::uint64_t> id_counter_{1000};
};

} // namespace QuantXCore::execution

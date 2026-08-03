#pragma once

#include <string>
#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/execution/Order.hpp"

namespace QuantXCore::execution {

class IExecutionClient {
public:
    virtual ~IExecutionClient() = default;
    virtual bool submit_order(Order& order) = 0;
    virtual bool cancel_order(const std::string& client_order_id) = 0;
};

} // namespace QuantXCore::execution

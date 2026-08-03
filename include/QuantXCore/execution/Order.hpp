#pragma once

#include <string>
#include <cstdint>
#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/risk/RiskEngine.hpp"

namespace QuantXCore::execution {

enum class OrderStatus {
    PENDING_SUBMIT,
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELLED,
    REJECTED
};

inline std::string status_to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::PENDING_SUBMIT:  return "PENDING_SUBMIT";
        case OrderStatus::NEW:             return "NEW";
        case OrderStatus::PARTIALLY_FILLED:return "PARTIALLY_FILLED";
        case OrderStatus::FILLED:          return "FILLED";
        case OrderStatus::CANCELLED:       return "CANCELLED";
        case OrderStatus::REJECTED:        return "REJECTED";
        default:                           return "UNKNOWN";
    }
}

struct Order {
    std::string client_order_id;   // Ծրագրի ներսի unqieu ID (օր. "ORD_1001")
    std::string exchange_order_id; // Բորսայի տված ID-ն
    std::string symbol;
    alpha::SignalType side;
    double price{0.0};
    double quantity{0.0};
    double filled_quantity{0.0};
    double avg_fill_price{0.0};
    OrderStatus status{OrderStatus::PENDING_SUBMIT};
    std::string strategy_name;
    std::uint64_t timestamp{0};
};

} // namespace QuantXCore::execution

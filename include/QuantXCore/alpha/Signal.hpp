#pragma once

#include <cstdint>
#include <string>

namespace QuantXCore::alpha {

/// Directional intent produced by an alpha strategy.
enum class SignalType {
    BUY,
    SELL,
    HOLD
};

/// A single alpha signal emitted by a strategy in response to market data.
struct Signal {
    std::string symbol;
    SignalType type{SignalType::HOLD};
    double price{0.0};
    double strength{0.0};       ///< Confidence in [0.0, 1.0]
    double atr{0.0};            ///< ATR value for dynamic SL/TP
    double stop_loss{0.0};
    double take_profit{0.0};
    std::uint64_t timestamp{0};  ///< Epoch time (ns or exchange-native units)
    std::string strategy_name;
};

/// Returns a human-readable string representation of a SignalType.
[[nodiscard]] inline std::string type_to_string(SignalType type) {
    switch (type) {
        case SignalType::BUY:  return "BUY";
        case SignalType::SELL: return "SELL";
        case SignalType::HOLD: return "HOLD";
    }
    return "UNKNOWN";
}

}  // namespace QuantXCore::alpha

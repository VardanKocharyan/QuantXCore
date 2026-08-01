#pragma once

#include <optional>
#include <string>

#include "QuantXCore/alpha/Signal.hpp"

namespace QuantXCore::data {
struct MarketTick;
}  // namespace QuantXCore::data

namespace QuantXCore::alpha {

/// Abstract base class for all alpha-generating strategies.
class AlphaStrategy {
public:
    virtual ~AlphaStrategy() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    virtual std::optional<Signal> on_tick(const QuantXCore::data::MarketTick& tick) = 0;
    virtual void reset() = 0;

protected:
    AlphaStrategy() = default;
    AlphaStrategy(const AlphaStrategy&) = default;
    AlphaStrategy& operator=(const AlphaStrategy&) = default;
    AlphaStrategy(AlphaStrategy&&) = default;
    AlphaStrategy& operator=(AlphaStrategy&&) = default;
};

}  // namespace QuantXCore::alpha

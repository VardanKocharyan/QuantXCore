#pragma once

#include "QuantXCore/alpha/AlphaStrategy.hpp"
#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/data/MarketTick.hpp"
#include <memory>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <unordered_map>

namespace QuantXCore::alpha {

class AlphaEngine {
public:
    AlphaEngine() = default;
    ~AlphaEngine() = default;

    AlphaEngine(const AlphaEngine&) = delete;
    AlphaEngine& operator=(const AlphaEngine&) = delete;
    AlphaEngine(AlphaEngine&&) = delete;
    AlphaEngine& operator=(AlphaEngine&&) = delete;

    bool register_strategy(std::shared_ptr<AlphaStrategy> strategy);
    bool unregister_strategy(const std::string& strategy_name);

    [[nodiscard]] std::vector<Signal> on_tick(const QuantXCore::data::MarketTick& tick);

    [[nodiscard]] std::size_t strategy_count() const noexcept;
    void clear_strategies();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<AlphaStrategy>> strategies_;
};

} // namespace QuantXCore::alpha

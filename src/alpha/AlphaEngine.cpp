#include "QuantXCore/alpha/AlphaEngine.hpp"

namespace QuantXCore::alpha {

bool AlphaEngine::register_strategy(std::shared_ptr<AlphaStrategy> strategy) {
    if (!strategy) return false;

    std::unique_lock lock(mutex_);
    std::string strat_name = strategy->name();
    if (strategies_.find(strat_name) != strategies_.end()) {
        return false; // Already registered
    }

    strategies_[strat_name] = std::move(strategy);
    return true;
}

bool AlphaEngine::unregister_strategy(const std::string& strategy_name) {
    std::unique_lock lock(mutex_);
    return strategies_.erase(strategy_name) > 0;
}

std::vector<Signal> AlphaEngine::on_tick(const QuantXCore::data::MarketTick& tick) {
    std::shared_lock lock(mutex_);
    std::vector<Signal> active_signals;
    active_signals.reserve(strategies_.size());

    for (const auto& [strat_name, strategy] : strategies_) {
        if (strategy) {
            auto signal_opt = strategy->on_tick(tick);
            if (signal_opt.has_value()) {
                active_signals.push_back(signal_opt.value());
            }
        }
    }

    return active_signals;
}

std::size_t AlphaEngine::strategy_count() const noexcept {
    std::shared_lock lock(mutex_);
    return strategies_.size();
}

void AlphaEngine::clear_strategies() {
    std::unique_lock lock(mutex_);
    strategies_.clear();
}

} // namespace QuantXCore::alpha

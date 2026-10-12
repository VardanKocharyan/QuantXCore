#pragma once

#include <cmath>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "QuantXCore/data/MarketTick.hpp"

namespace QuantXCore::execution {

struct PositionBracket {
    std::string symbol;
    double entry_price{0.0};
    double quantity{0.0};
    double stop_loss_price{0.0};
    double take_profit_price{0.0};
    bool trailing_enabled{false};
    double trailing_pct{0.0};
    double peak_price{0.0};
};

enum class TriggerType { NONE, STOP_LOSS, TAKE_PROFIT, TRAILING_STOP };

struct TriggerResult {
    TriggerType type{TriggerType::NONE};
    std::string symbol;
    double close_quantity{0.0};
    double trigger_price{0.0};
};

class BracketOrderManager {
public:
    void set_bracket(const std::string& symbol,
                     double entry_price,
                     double quantity,
                     double sl_pct,
                     double tp_pct,
                     double trailing_pct = 0.0) {
        std::lock_guard<std::mutex> lock(mutex_);

        PositionBracket bracket;
        bracket.symbol = symbol;
        bracket.entry_price = entry_price;
        bracket.quantity = quantity;
        bracket.peak_price = entry_price;

        bool is_long = (quantity > 0);

        if (is_long) {
            bracket.stop_loss_price = entry_price * (1.0 - sl_pct);
            bracket.take_profit_price = entry_price * (1.0 + tp_pct);
        } else {
            bracket.stop_loss_price = entry_price * (1.0 + sl_pct);
            bracket.take_profit_price = entry_price * (1.0 - tp_pct);
        }

        if (trailing_pct > 0.0) {
            bracket.trailing_enabled = true;
            bracket.trailing_pct = trailing_pct;
        }

        brackets_[symbol] = bracket;

        std::cout << "[BracketManager] SL/TP active for " << symbol
                  << " | Entry: $" << entry_price
                  << " | SL: $" << bracket.stop_loss_price
                  << " | TP: $" << bracket.take_profit_price
                  << (bracket.trailing_enabled ? " | Trailing Active" : "") << std::endl;
    }

    void set_bracket_exact(const std::string& symbol,
                           double entry_price,
                           double quantity,
                           double stop_loss_price,
                           double take_profit_price,
                           double trailing_pct = 0.0) {
        std::lock_guard<std::mutex> lock(mutex_);

        PositionBracket bracket;
        bracket.symbol = symbol;
        bracket.entry_price = entry_price;
        bracket.quantity = quantity;
        bracket.peak_price = entry_price;
        bracket.stop_loss_price = stop_loss_price;
        bracket.take_profit_price = take_profit_price;

        if (trailing_pct > 0.0) {
            bracket.trailing_enabled = true;
            bracket.trailing_pct = trailing_pct;
        }

        brackets_[symbol] = bracket;

        std::cout << "[BracketManager] SL/TP active for " << symbol
                  << " | Entry: $" << entry_price
                  << " | SL: $" << bracket.stop_loss_price
                  << " | TP: $" << bracket.take_profit_price
                  << (bracket.trailing_enabled ? " | Trailing Active" : "") << std::endl;
    }

    TriggerResult on_tick(const data::MarketTick& tick) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = brackets_.find(tick.symbol);
        if (it == brackets_.end()) {
            return {TriggerType::NONE, "", 0.0, 0.0};
        }

        auto& bracket = it->second;
        bool is_long = (bracket.quantity > 0);
        double current_price = tick.price;

        if (bracket.trailing_enabled) {
            if (is_long && current_price > bracket.peak_price) {
                bracket.peak_price = current_price;
                double new_sl = current_price * (1.0 - bracket.trailing_pct);
                if (new_sl > bracket.stop_loss_price) {
                    bracket.stop_loss_price = new_sl;
                    std::cout << "[BracketManager] Trailing SL updated for " << tick.symbol
                              << " -> $" << new_sl << std::endl;
                }
            } else if (!is_long && current_price < bracket.peak_price) {
                bracket.peak_price = current_price;
                double new_sl = current_price * (1.0 + bracket.trailing_pct);
                if (new_sl < bracket.stop_loss_price) {
                    bracket.stop_loss_price = new_sl;
                    std::cout << "[BracketManager] Trailing SL updated for " << tick.symbol
                              << " -> $" << new_sl << std::endl;
                }
            }
        }

        if ((is_long && current_price <= bracket.stop_loss_price) ||
            (!is_long && current_price >= bracket.stop_loss_price)) {
            TriggerResult res{
                bracket.trailing_enabled ? TriggerType::TRAILING_STOP : TriggerType::STOP_LOSS,
                tick.symbol,
                -bracket.quantity,
                current_price
            };
            brackets_.erase(it);
            return res;
        }

        if ((is_long && current_price >= bracket.take_profit_price) ||
            (!is_long && current_price <= bracket.take_profit_price)) {
            TriggerResult res{
                TriggerType::TAKE_PROFIT,
                tick.symbol,
                -bracket.quantity,
                current_price
            };
            brackets_.erase(it);
            return res;
        }

        return {TriggerType::NONE, "", 0.0, 0.0};
    }

    void remove_bracket(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        brackets_.erase(symbol);
    }

    bool has_bracket(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return brackets_.find(symbol) != brackets_.end();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, PositionBracket> brackets_;
};

} // namespace QuantXCore::execution

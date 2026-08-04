#include "QuantXCore/data/OrderBook.hpp"

#include <algorithm>
#include <mutex>

namespace QuantXCore::data {

namespace {
// Quantities at or below this are treated as "zero" for level-removal
// purposes, guarding against floating point noise from upstream parsers.
constexpr double kZeroQuantityEpsilon = 1e-9;
} // namespace

void OrderBook::update_level(Side side, double price, double quantity) {
    std::unique_lock lock(mutex_);

    if (side == Side::Buy) {
        if (quantity <= kZeroQuantityEpsilon) {
            bids_.erase(price);
        } else {
            bids_[price] = quantity;
        }
    } else {
        if (quantity <= kZeroQuantityEpsilon) {
            asks_.erase(price);
        } else {
            asks_[price] = quantity;
        }
    }
}

void OrderBook::clear() {
    std::unique_lock lock(mutex_);
    bids_.clear();
    asks_.clear();
}

std::optional<PriceLevel> OrderBook::best_bid_locked() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    const auto& [price, qty] = *bids_.begin();
    return PriceLevel{price, qty};
}

std::optional<PriceLevel> OrderBook::best_ask_locked() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    const auto& [price, qty] = *asks_.begin();
    return PriceLevel{price, qty};
}

std::optional<PriceLevel> OrderBook::get_best_bid() const {
    std::shared_lock lock(mutex_);
    return best_bid_locked();
}

std::optional<PriceLevel> OrderBook::get_best_ask() const {
    std::shared_lock lock(mutex_);
    return best_ask_locked();
}

std::optional<double> OrderBook::get_spread() const {
    std::shared_lock lock(mutex_);
    const auto bid = best_bid_locked();
    const auto ask = best_ask_locked();
    if (!bid || !ask) {
        return std::nullopt;
    }
    return ask->price - bid->price;
}

std::optional<double> OrderBook::get_mid_price() const {
    std::shared_lock lock(mutex_);
    const auto bid = best_bid_locked();
    const auto ask = best_ask_locked();
    if (!bid || !ask) {
        return std::nullopt;
    }
    return (ask->price + bid->price) / 2.0;
}

std::optional<double> OrderBook::get_micro_price() const {
    std::shared_lock lock(mutex_);
    const auto bid = best_bid_locked();
    const auto ask = best_ask_locked();
    if (!bid || !ask) {
        return std::nullopt;
    }

    const double denom = bid->quantity + ask->quantity;
    if (denom <= 0.0) {
        return std::nullopt;
    }

    return (bid->price * ask->quantity + ask->price * bid->quantity) / denom;
}

double OrderBook::get_imbalance(std::size_t depth) const {
    std::shared_lock lock(mutex_);

    double bid_volume = 0.0;
    std::size_t count = 0;
    for (const auto& [price, qty] : bids_) {
        if (count >= depth) break;
        bid_volume += qty;
        ++count;
    }

    double ask_volume = 0.0;
    count = 0;
    for (const auto& [price, qty] : asks_) {
        if (count >= depth) break;
        ask_volume += qty;
        ++count;
    }

    const double denom = bid_volume + ask_volume;
    if (denom <= 0.0) {
        return 0.0;
    }

    return (bid_volume - ask_volume) / denom;
}

std::vector<PriceLevel> OrderBook::get_bids(std::size_t depth) const {
    std::shared_lock lock(mutex_);

    std::vector<PriceLevel> result;
    result.reserve(std::min(depth, bids_.size()));

    std::size_t count = 0;
    for (const auto& [price, qty] : bids_) {
        if (count >= depth) break;
        result.push_back(PriceLevel{price, qty});
        ++count;
    }
    return result;
}

std::vector<PriceLevel> OrderBook::get_asks(std::size_t depth) const {
    std::shared_lock lock(mutex_);

    std::vector<PriceLevel> result;
    result.reserve(std::min(depth, asks_.size()));

    std::size_t count = 0;
    for (const auto& [price, qty] : asks_) {
        if (count >= depth) break;
        result.push_back(PriceLevel{price, qty});
        ++count;
    }
    return result;
}

std::size_t OrderBook::bid_level_count() const {
    std::shared_lock lock(mutex_);
    return bids_.size();
}

std::size_t OrderBook::ask_level_count() const {
    std::shared_lock lock(mutex_);
    return asks_.size();
}

} // namespace QuantXCore::data

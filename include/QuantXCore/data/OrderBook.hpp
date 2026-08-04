#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <shared_mutex>
#include <vector>

namespace QuantXCore::data {

/**
 * @brief Side of an order book update / trade.
 *
 * NOTE: This does not currently exist in MarketTick.hpp (which only exposes
 * a boolean `isBuyerMaker`). Defined here as the canonical Side type for the
 * order book engine; consolidate into a shared Types.hpp if/when one exists.
 */
enum class Side : std::uint8_t {
    Buy,
    Sell
};

/**
 * @brief A single price/quantity level in the book.
 */
struct PriceLevel {
    double price{0.0};
    double quantity{0.0};

    bool operator==(const PriceLevel& other) const noexcept {
        return price == other.price && quantity == other.quantity;
    }
};

/**
 * @brief High-performance, thread-safe, in-memory limit order book.
 *
 * Bids are maintained in descending price order (best bid first).
 * Asks are maintained in ascending price order (best ask first).
 *
 * Thread safety: a std::shared_mutex allows any number of concurrent
 * readers (e.g. strategy threads querying depth/prices) alongside a
 * single writer (e.g. the exchange streamer applying diff updates).
 */
class OrderBook {
public:
    OrderBook() = default;

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    /**
     * @brief Insert, update, or remove a price level.
     *
     * If quantity is <= 0 (within epsilon), the level at `price` is removed
     * from the relevant side. Otherwise the level is inserted/overwritten.
     */
    void update_level(Side side, double price, double quantity);

    /**
     * @brief Remove all bid and ask levels, resetting the book to empty.
     */
    void clear();

    [[nodiscard]] std::optional<PriceLevel> get_best_bid() const;
    [[nodiscard]] std::optional<PriceLevel> get_best_ask() const;

    /// Ask - Bid. nullopt if either side is empty.
    [[nodiscard]] std::optional<double> get_spread() const;

    /// (Ask + Bid) / 2. nullopt if either side is empty.
    [[nodiscard]] std::optional<double> get_mid_price() const;

    /**
     * @brief Volume-weighted top-of-book price.
     *
     * MicroPrice = (BidPrice * AskVolume + AskPrice * BidVolume)
     *              / (BidVolume + AskVolume)
     *
     * nullopt if either side is empty or combined top-of-book volume is 0.
     */
    [[nodiscard]] std::optional<double> get_micro_price() const;

    /**
     * @brief Volume imbalance across the top `depth` levels of each side.
     *
     * Imbalance = (BidVol - AskVol) / (BidVol + AskVol)
     *
     * Returns a value in [-1.0, +1.0]. Returns 0.0 if the book is empty or
     * combined volume across the requested depth is 0.
     */
    [[nodiscard]] double get_imbalance(std::size_t depth = 5) const;

    [[nodiscard]] std::vector<PriceLevel> get_bids(std::size_t depth = 10) const;
    [[nodiscard]] std::vector<PriceLevel> get_asks(std::size_t depth = 10) const;

    /// Number of distinct bid price levels currently held.
    [[nodiscard]] std::size_t bid_level_count() const;

    /// Number of distinct ask price levels currently held.
    [[nodiscard]] std::size_t ask_level_count() const;

private:
    // Assumes caller already holds at least a shared lock on mutex_.
    [[nodiscard]] std::optional<PriceLevel> best_bid_locked() const;
    [[nodiscard]] std::optional<PriceLevel> best_ask_locked() const;

    mutable std::shared_mutex mutex_;

    // Descending: begin() is the highest (best) bid.
    std::map<double, double, std::greater<double>> bids_;
    // Ascending: begin() is the lowest (best) ask.
    std::map<double, double> asks_;
};

} // namespace QuantXCore::data

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "QuantXCore/data/OrderBook.hpp"

namespace QuantXCore::data {

/**
 * @brief Represents a single price-quantity delta entry from a Binance depth update event.
 */
struct DepthLevel {
    double price{0.0};
    double quantity{0.0};
};

/**
 * @brief Represents a raw Binance L2 WebSocket depth update event (@depth / @depth100ms).
 */
struct DepthEvent {
    std::string event_type;  ///< "depthUpdate"
    std::int64_t event_time{0};
    std::string symbol;
    std::uint64_t first_update_id{0};  ///< 'U' in Binance payload
    std::uint64_t final_update_id{0};  ///< 'u' in Binance payload
    std::uint64_t pu_update_id{0};     ///< 'pu' in Binance payload (previous final update ID)
    std::vector<DepthLevel> bids;
    std::vector<DepthLevel> asks;
};

/**
 * @brief Represents a full L2 OrderBook snapshot fetched from Binance REST API.
 */
struct DepthSnapshot {
    std::uint64_t last_update_id{0};
    std::vector<DepthLevel> bids;
    std::vector<DepthLevel> asks;
};

enum class SyncState {
    Buffering,
    InSync,
    OutOfSync
};

/**
 * @brief Manages strict order-book state synchronization following the official Binance L2 specification.
 *
 * Binance Order Book Sync Protocol:
 * 1. Buffer incoming WebSocket depth update events.
 * 2. Apply a full REST snapshot (containing last_update_id).
 * 3. Drop buffered events where event.final_update_id <= snapshot.last_update_id.
 * 4. Apply the first event where U <= last_update_id + 1 and u >= last_update_id + 1.
 * 5. In Live mode, strictly verify sequence continuity: event.U == previous_event.u + 1.
 *    If a gap is detected, state drops to OutOfSync and forces a re-buffering cycle.
 */
class OrderBookSync {
public:
    using SnapshotFetcher = std::function<std::optional<DepthSnapshot>(const std::string& symbol)>;

    explicit OrderBookSync(std::string symbol, OrderBook& target_book);
    ~OrderBookSync() = default;

    OrderBookSync(const OrderBookSync&) = delete;
    OrderBookSync& operator=(const OrderBookSync&) = delete;

    /// Ingests a JSON payload or parsed event originating from Binance WebSocket streamer.
    void process_depth_event(const DepthEvent& event);

    /// Ingests raw JSON payload string directly from WebSocket streamer.
    bool process_json_payload(const std::string& json_str);

    /// Applies a newly fetched REST snapshot to the internal state and syncs the book.
    bool apply_snapshot(const DepthSnapshot& snapshot);

    /// Triggers a reset of sync state, clearing the target book and buffer.
    void reset();

    [[nodiscard]] SyncState state() const noexcept;
    [[nodiscard]] bool is_in_sync() const noexcept;
    [[nodiscard]] std::uint64_t last_applied_update_id() const noexcept;

private:
    void apply_event_to_book(const DepthEvent& event);

    std::string symbol_;
    OrderBook& book_;

    mutable std::mutex sync_mutex_;
    SyncState state_{SyncState::Buffering};
    std::uint64_t last_applied_update_id_{0};

    std::deque<DepthEvent> buffer_;
    static constexpr std::size_t kMaxBufferSize = 5000;
};

} // namespace QuantXCore::data

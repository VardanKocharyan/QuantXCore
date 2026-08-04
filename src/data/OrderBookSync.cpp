#include "QuantXCore/data/OrderBookSync.hpp"

#include <nlohmann/json.hpp>
#include <iostream>

namespace QuantXCore::data {

using json = nlohmann::json;

OrderBookSync::OrderBookSync(std::string symbol, OrderBook& target_book)
    : symbol_(std::move(symbol)), book_(target_book) {}

void OrderBookSync::process_depth_event(const DepthEvent& event) {
    std::lock_guard<std::mutex> lock(sync_mutex_);

    if (state_ == SyncState::Buffering) {
        if (buffer_.size() >= kMaxBufferSize) {
            buffer_.pop_front();
        }
        buffer_.push_back(event);
        return;
    }

    if (state_ == SyncState::InSync) {
        // Enforce sequence validation
        if (event.first_update_id > last_applied_update_id_ + 1) {
            // Sequence gap detected -> Drop to OutOfSync to force resync
            state_ = SyncState::OutOfSync;
            book_.clear();
            buffer_.clear();
            return;
        }

        if (event.final_update_id <= last_applied_update_id_) {
            // Stale event, ignore
            return;
        }

        apply_event_to_book(event);
    }
}

bool OrderBookSync::process_json_payload(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);

        // Support both combined stream wrapper and direct raw payloads
        if (j.contains("data")) {
            j = j["data"];
        }

        if (!j.contains("e") || j["e"] != "depthUpdate") {
            return false;
        }

        DepthEvent event;
        event.event_type = j["e"].get<std::string>();
        event.event_time = j["E"].get<std::int64_t>();
        event.symbol = j["s"].get<std::string>();
        event.first_update_id = j["U"].get<std::uint64_t>();
        event.final_update_id = j["u"].get<std::uint64_t>();
        if (j.contains("pu")) {
            event.pu_update_id = j["pu"].get<std::uint64_t>();
        }

        for (const auto& item : j["b"]) {
            event.bids.push_back({
                std::stod(item[0].get<std::string>()),
                std::stod(item[1].get<std::string>())
            });
        }

        for (const auto& item : j["a"]) {
            event.asks.push_back({
                std::stod(item[0].get<std::string>()),
                std::stod(item[1].get<std::string>())
            });
        }

        process_depth_event(event);
        return true;
    } catch (...) {
        return false;
    }
}

bool OrderBookSync::apply_snapshot(const DepthSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(sync_mutex_);

    book_.clear();

    // Populate fresh snapshot into internal order book
    for (const auto& bid : snapshot.bids) {
        book_.update_level(Side::Buy, bid.price, bid.quantity);
    }
    for (const auto& ask : snapshot.asks) {
        book_.update_level(Side::Sell, ask.price, ask.quantity);
    }

    last_applied_update_id_ = snapshot.last_update_id;

    // Drain and apply buffered events (Binance depth sync rule)
    while (!buffer_.empty()) {
        const auto& ev = buffer_.front();

        // 1. Drop stale events
        if (ev.final_update_id <= last_applied_update_id_) {
            buffer_.pop_front();
            continue;
        }

        // 2. First valid update must span across snapshot's lastUpdateId
        if (ev.first_update_id <= last_applied_update_id_ + 1 &&
            ev.final_update_id >= last_applied_update_id_ + 1) {
            apply_event_to_book(ev);
            buffer_.pop_front();
            break;
        }

        // If buffer has a gap before reaching valid snapshot span
        if (ev.first_update_id > last_applied_update_id_ + 1) {
            state_ = SyncState::OutOfSync;
            book_.clear();
            buffer_.clear();
            return false;
        }

        buffer_.pop_front();
    }

    // Apply remaining buffered updates
    while (!buffer_.empty()) {
        const auto& ev = buffer_.front();
        if (ev.first_update_id <= last_applied_update_id_ + 1) {
            apply_event_to_book(ev);
        } else {
            state_ = SyncState::OutOfSync;
            book_.clear();
            buffer_.clear();
            return false;
        }
        buffer_.pop_front();
    }

    state_ = SyncState::InSync;
    return true;
}

void OrderBookSync::reset() {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    state_ = SyncState::Buffering;
    last_applied_update_id_ = 0;
    buffer_.clear();
    book_.clear();
}

SyncState OrderBookSync::state() const noexcept {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    return state_;
}

bool OrderBookSync::is_in_sync() const noexcept {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    return state_ == SyncState::InSync;
}

std::uint64_t OrderBookSync::last_applied_update_id() const noexcept {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    return last_applied_update_id_;
}

void OrderBookSync::apply_event_to_book(const DepthEvent& event) {
    for (const auto& bid : event.bids) {
        book_.update_level(Side::Buy, bid.price, bid.quantity);
    }
    for (const auto& ask : event.asks) {
        book_.update_level(Side::Sell, ask.price, ask.quantity);
    }
    last_applied_update_id_ = event.final_update_id;
}

} // namespace QuantXCore::data

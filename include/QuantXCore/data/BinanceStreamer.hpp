#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "QuantXCore/data/MarketTick.hpp"

namespace QuantXCore::data {

enum class BinanceMarket {
    Spot,
    UsdMFutures
};

enum class BinanceStreamKind {
    AggTrade,
    BookTicker,
    Depth
};

struct StreamSubscription {
    std::string symbol;       ///< e.g. "BTCUSDT" (case-insensitive, normalized internally)
    BinanceStreamKind kind;
};

struct BinanceStreamerConfig {
    BinanceMarket market{BinanceMarket::Spot};
    std::vector<StreamSubscription> subscriptions;

    std::chrono::milliseconds initial_backoff{std::chrono::milliseconds(500)};
    std::chrono::milliseconds max_backoff{std::chrono::milliseconds(30'000)};
    std::chrono::seconds read_timeout{std::chrono::seconds(20)};

    std::size_t queue_capacity{1u << 16};
};

/**
 * @brief Bounded, thread-safe queue used to hand MarketTick objects from the
 *        network I/O thread to consumer thread(s) without forcing callback
 *        execution on the hot I/O path.
 *
 * Overflow policy: drops the oldest element to bound memory and keep the
 * producer (network thread) non-blocking. Always populated regardless of
 * whether a callback is also registered on BinanceStreamer.
 */
class MarketTickQueue {
public:
    explicit MarketTickQueue(std::size_t capacity);

    void push(MarketTick tick);
    bool try_pop(MarketTick& out);
    bool wait_pop(MarketTick& out, std::chrono::milliseconds timeout);

    [[nodiscard]] std::size_t size() const noexcept;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<MarketTick> buffer_;
    std::size_t capacity_;
};

/**
 * @brief Asynchronous, auto-reconnecting Binance WebSocket market data
 *        streamer. Converts aggTrade / bookTicker / depth payloads into
 *        QuantXCore::data::MarketTick objects.
 *
 * All networking (TLS + WebSocket handshake, read loop, reconnect with
 * exponential backoff) runs on a single internal background thread. The
 * public API is safe to call from any thread. Boost/Beast/Asio types are
 * confined to the .cpp via pimpl to keep this header lightweight for
 * downstream consumers.
 */
class BinanceStreamer {
public:
    using TickCallback = std::function<void(const MarketTick&)>;

    explicit BinanceStreamer(BinanceStreamerConfig config);
    ~BinanceStreamer();

    BinanceStreamer(const BinanceStreamer&) = delete;
    BinanceStreamer& operator=(const BinanceStreamer&) = delete;
    BinanceStreamer(BinanceStreamer&&) = delete;
    BinanceStreamer& operator=(BinanceStreamer&&) = delete;

    /// Invoked on the I/O thread for every parsed tick. Keep this fast and
    /// non-blocking (e.g. push to your own SPSC ring / lock-free queue) --
    /// it runs inline in the read loop.
    void set_tick_callback(TickCallback callback);

    /// Pull-based alternative (or complement) to the callback.
    MarketTickQueue& queue() noexcept;

    void start();
    void stop();

    [[nodiscard]] bool is_connected() const noexcept;
    [[nodiscard]] bool is_running() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace QuantXCore::data

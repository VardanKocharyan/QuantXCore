#include "QuantXCore/data/BinanceStreamer.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <nlohmann/json.hpp>
#include <openssl/ssl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <random>
#include <sstream>

namespace QuantXCore::data {

namespace {

namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;
using WsStream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string stream_kind_suffix(BinanceStreamKind kind) {
    switch (kind) {
        case BinanceStreamKind::AggTrade:   return "aggTrade";
        case BinanceStreamKind::BookTicker: return "bookTicker";
        case BinanceStreamKind::Depth:      return "depth";
    }
    return "aggTrade";
}

struct Endpoint {
    std::string host;
    std::string port;
};

Endpoint endpoint_for(BinanceMarket market) {
    switch (market) {
        case BinanceMarket::Spot:        return {"stream.binance.com", "9443"};
        case BinanceMarket::UsdMFutures: return {"fstream.binance.com", "443"};
    }
    return {"stream.binance.com", "9443"};
}

std::string build_combined_path(const std::vector<StreamSubscription>& subs) {
    std::ostringstream oss;
    oss << "/stream?streams=";
    for (std::size_t i = 0; i < subs.size(); ++i) {
        if (i > 0) oss << '/';
        oss << to_lower(subs[i].symbol) << '@' << stream_kind_suffix(subs[i].kind);
    }
    return oss.str();
}

double parse_double(const json& j) {
    if (j.is_string()) return std::stod(j.get<std::string>());
    if (j.is_number()) return j.get<double>();
    return 0.0;
}

uint64_t now_millis() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// --- Binance payload -> MarketTick mapping -------------------------------
//
// Only aggTrade is a genuine trade event with a real trade_id/side/volume.
// bookTicker and depth are quote/order-book snapshots, not trades; they are
// mapped onto MarketTick using a synthetic mid-price / top-of-book volume so
// they can flow through the same downstream pipeline. isBuyerMaker has no
// meaning for those two and is left false.

std::optional<MarketTick> map_agg_trade(const json& d) {
    if (!d.contains("p") || !d.contains("q") || !d.contains("s")) return std::nullopt;
    MarketTick tick;
    tick.symbol = d.value("s", std::string{});
    tick.price = parse_double(d.at("p"));
    tick.volume = parse_double(d.at("q"));
    tick.timestamp = d.value("T", now_millis());
    tick.isBuyerMaker = d.value("m", false);
    tick.trade_id = d.value("a", static_cast<uint64_t>(0));
    return tick;
}

std::optional<MarketTick> map_book_ticker(const json& d) {
    if (!d.contains("b") || !d.contains("a") || !d.contains("s")) return std::nullopt;
    MarketTick tick;
    tick.symbol = d.value("s", std::string{});
    const double bid = parse_double(d.at("b"));
    const double ask = parse_double(d.at("a"));
    tick.price = (bid + ask) / 2.0;
    tick.volume = parse_double(d.value("B", json(0.0))) + parse_double(d.value("A", json(0.0)));
    tick.timestamp = d.value("E", now_millis());
    tick.isBuyerMaker = false;
    tick.trade_id = d.value("u", static_cast<uint64_t>(0));
    return tick;
}

std::optional<MarketTick> map_depth(const json& d) {
    if (!d.contains("b") || !d.contains("a") || !d.contains("s")) return std::nullopt;
    const auto& bids = d.at("b");
    const auto& asks = d.at("a");
    if (bids.empty() || asks.empty()) return std::nullopt;

    MarketTick tick;
    tick.symbol = d.value("s", std::string{});
    const double best_bid = parse_double(bids.at(0).at(0));
    const double best_bid_qty = parse_double(bids.at(0).at(1));
    const double best_ask = parse_double(asks.at(0).at(0));
    const double best_ask_qty = parse_double(asks.at(0).at(1));
    tick.price = (best_bid + best_ask) / 2.0;
    tick.volume = best_bid_qty + best_ask_qty;
    tick.timestamp = d.value("E", now_millis());
    tick.isBuyerMaker = false;
    tick.trade_id = d.value("u", static_cast<uint64_t>(0));
    return tick;
}

std::optional<MarketTick> map_payload(const std::string& stream_name, const json& data) {
    if (stream_name.ends_with("@aggTrade")) return map_agg_trade(data);
    if (stream_name.ends_with("@bookTicker")) return map_book_ticker(data);
    if (stream_name.ends_with("@depth") || stream_name.find("@depth@") != std::string::npos) {
        return map_depth(data);
    }
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------
// MarketTickQueue
// ---------------------------------------------------------------------

MarketTickQueue::MarketTickQueue(std::size_t capacity) : capacity_(capacity) {}

void MarketTickQueue::push(MarketTick tick) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.size() >= capacity_) {
            buffer_.pop_front(); // drop oldest: bound memory, keep producer non-blocking
        }
        buffer_.push_back(std::move(tick));
    }
    cv_.notify_one();
}

bool MarketTickQueue::try_pop(MarketTick& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (buffer_.empty()) return false;
    out = std::move(buffer_.front());
    buffer_.pop_front();
    return true;
}

bool MarketTickQueue::wait_pop(MarketTick& out, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return !buffer_.empty(); })) {
        return false;
    }
    out = std::move(buffer_.front());
    buffer_.pop_front();
    return true;
}

std::size_t MarketTickQueue::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

// ---------------------------------------------------------------------
// BinanceStreamer::Impl
//
// Threading model: exactly one thread (io_thread) ever calls ioc.run(), and
// every single touch of `ws`, `resolver`, or `backoff_timer` happens from a
// handler running on that thread -- including the teardown handler that
// stop() schedules. Nothing outside this class ever calls a Beast/Asio
// method directly on these objects. That single-writer confinement is what
// makes shutdown safe: there is no "external thread closes socket while I/O
// thread is mid-read" race left to have, because the external thread never
// touches the socket at all -- it only ever posts a request to the thread
// that owns it.
// ---------------------------------------------------------------------

struct BinanceStreamer::Impl {
    explicit Impl(BinanceStreamerConfig cfg)
        : config(std::move(cfg)),
          tick_queue(config.queue_capacity),
          resolver(ioc),
          backoff_timer(ioc),
          rng(std::random_device{}()) {}

    ~Impl() { stop(); }

    // ---- lifecycle (called from any external/control thread) ----

    void start() {
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true)) {
            return; // already running
        }

        if (io_thread.joinable()) {
            io_thread.join(); // tidy up a prior stop() cycle before restarting
        }
        ioc.restart(); // required before reusing an io_context after run() drained

        current_backoff = config.initial_backoff;
        work_guard.emplace(asio::make_work_guard(ioc));

        io_thread = std::thread([this] { ioc.run(); });

        // Kick off the first connection attempt on the io_thread itself.
        asio::post(ioc, [this] { do_connect(); });
    }

    void stop() {
        bool expected = true;
        if (!running.compare_exchange_strong(expected, false)) {
            return; // never started, or already stopped
        }

        // Post the actual teardown onto the io_context so it runs serialized
        // with (and never concurrently to) any in-flight async handler. This
        // is the piece that was missing before: previously the equivalent
        // work ran directly on the calling thread while the io_thread could
        // simultaneously be inside a blocking read on the same socket.
        asio::post(ioc, [this] {
            boost::system::error_code ec;
            resolver.cancel();
            backoff_timer.cancel();
            if (ws) {
                beast::get_lowest_layer(*ws).cancel();
                if (beast::get_lowest_layer(*ws).socket().is_open()) {
                    auto self_ws = ws; // keep the stream alive until async_close completes
                    self_ws->async_close(websocket::close_code::normal,
                                         [self_ws](beast::error_code /*close_ec*/) {
                                             // Best-effort; nothing to do either way.
                                         });
                }
            }
            connected.store(false);
        });

        // Dropping the work guard lets io_context::run() return once there is
        // no outstanding work left -- and the async_close posted above still
        // counts as outstanding work, so run() will naturally wait for it to
        // complete instead of tearing the loop down mid-operation.
        work_guard.reset();

        if (io_thread.joinable()) {
            io_thread.join();
        }

        // By the time join() returns, io_thread has exited ioc.run() and will
        // touch nothing else, so it's safe to clear this from here.
        ws.reset();
        connected.store(false);
    }

    // ---- connection state machine (every method below runs exclusively on io_thread) ----

    void do_connect() {
        if (!running.load()) return;

        ws = std::make_shared<WsStream>(ioc, ssl_ctx);

        const auto endpoint = endpoint_for(config.market);
        if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), endpoint.host.c_str())) {
            schedule_retry();
            return;
        }

        beast::get_lowest_layer(*ws).expires_after(config.read_timeout);
        resolver.async_resolve(
            endpoint.host, endpoint.port,
            [this](beast::error_code ec, tcp::resolver::results_type results) {
                on_resolve(ec, results);
            });
    }

    void on_resolve(beast::error_code ec, const tcp::resolver::results_type& results) {
        if (ec || !running.load()) {
            schedule_retry();
            return;
        }
        beast::get_lowest_layer(*ws).async_connect(
            results, [this](beast::error_code ec2, const tcp::resolver::results_type::endpoint_type&) {
                on_connect(ec2);
            });
    }

    void on_connect(beast::error_code ec) {
        if (ec || !running.load()) {
            schedule_retry();
            return;
        }
        ws->next_layer().async_handshake(ssl::stream_base::client,
                                          [this](beast::error_code ec2) { on_ssl_handshake(ec2); });
    }

    void on_ssl_handshake(beast::error_code ec) {
        if (ec || !running.load()) {
            schedule_retry();
            return;
        }

        beast::get_lowest_layer(*ws).expires_never();
        ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
            req.set(http::field::user_agent, "QuantXCore-BinanceStreamer/1.0");
        }));

        const auto endpoint = endpoint_for(config.market);
        const std::string target = build_combined_path(config.subscriptions);
        ws->async_handshake(endpoint.host, target,
                             [this](beast::error_code ec2) { on_ws_handshake(ec2); });
    }

    void on_ws_handshake(beast::error_code ec) {
        if (ec || !running.load()) {
            schedule_retry();
            return;
        }
        connected.store(true);
        current_backoff = config.initial_backoff; // a real session started: reset backoff
        start_read();
    }

    void start_read() {
        if (!running.load() || !ws) return;
        beast::get_lowest_layer(*ws).expires_after(config.read_timeout);
        read_buffer.clear();
        ws->async_read(read_buffer, [this](beast::error_code ec, std::size_t) { on_read(ec); });
    }

    void on_read(beast::error_code ec) {
        if (ec) {
            connected.store(false);
            ws.reset();
            schedule_retry();
            return;
        }
        handle_message(beast::buffers_to_string(read_buffer.data()));
        start_read();
    }

    void schedule_retry() {
        connected.store(false);
        ws.reset();
        if (!running.load()) return;

        std::uniform_int_distribution<int> jitter(0, static_cast<int>(current_backoff.count() / 4 + 1));
        const auto delay = current_backoff + std::chrono::milliseconds(jitter(rng));
        current_backoff = std::min(current_backoff * 2, config.max_backoff);

        backoff_timer.expires_after(delay);
        backoff_timer.async_wait([this](beast::error_code ec) {
            if (!ec && running.load()) {
                do_connect();
            }
        });
    }

    void handle_message(const std::string& payload) {
        try {
            const json root = json::parse(payload);
            if (!root.contains("stream") || !root.contains("data")) return;

            const std::string stream_name = root.at("stream").get<std::string>();
            const json& data = root.at("data");

            auto tick = map_payload(stream_name, data);
            if (!tick) return;

            TickCallback cb;
            {
                std::lock_guard<std::mutex> lock(cb_mutex);
                cb = callback;
            }
            if (cb) {
                cb(*tick);
            }
            tick_queue.push(*tick);
        } catch (const json::exception&) {
            // Malformed / unexpected payload shape: skip and keep streaming.
        }
    }

    // ---- state ----

    BinanceStreamerConfig config;
    MarketTickQueue tick_queue;

    asio::io_context ioc;
    ssl::context ssl_ctx{ssl::context::tlsv12_client};
    tcp::resolver resolver;
    asio::steady_timer backoff_timer;
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard;

    // Touched exclusively from handlers running on io_thread (see class comment above).
    std::shared_ptr<WsStream> ws;
    beast::flat_buffer read_buffer;
    std::chrono::milliseconds current_backoff{0};
    std::mt19937 rng;

    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};

    std::thread io_thread;

    std::mutex cb_mutex;
    TickCallback callback;
};

// ---------------------------------------------------------------------
// BinanceStreamer
// ---------------------------------------------------------------------

BinanceStreamer::BinanceStreamer(BinanceStreamerConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

BinanceStreamer::~BinanceStreamer() = default;

void BinanceStreamer::set_tick_callback(TickCallback callback) {
    std::lock_guard<std::mutex> lock(impl_->cb_mutex);
    impl_->callback = std::move(callback);
}

MarketTickQueue& BinanceStreamer::queue() noexcept {
    return impl_->tick_queue;
}

void BinanceStreamer::start() { impl_->start(); }
void BinanceStreamer::stop() { impl_->stop(); }

bool BinanceStreamer::is_connected() const noexcept { return impl_->connected.load(); }
bool BinanceStreamer::is_running() const noexcept { return impl_->running.load(); }

} // namespace QuantXCore::data

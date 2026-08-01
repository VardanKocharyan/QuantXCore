#include "QuantXCore/data/RedisStreamer.hpp"
#include "QuantXCore/data/MarketTick.hpp"

#include <sw/redis++/redis++.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace QuantXCore::data {

RedisStreamer::RedisStreamer()
    : connected_(false), streaming_(false), should_stop_(false) {
}

RedisStreamer::~RedisStreamer() {
    stop();
}

bool RedisStreamer::connect(const std::string& host, int port) {
    if (connected_.load(std::memory_order_acquire)) {
        stop();
    }

    host_ = host;
    port_ = port;

    try {
        sw::redis::ConnectionOptions opts;
        opts.host = host;
        opts.port = port;
        opts.socket_timeout = std::chrono::milliseconds(5000);
        opts.connect_timeout = std::chrono::milliseconds(3000);
        opts.keep_alive = true;

        redis_ = std::make_unique<sw::redis::Redis>(opts);

        auto reply = redis_->ping();
        if (reply != "PONG") {
            std::cerr << "RedisStreamer: Ping failed: " << reply << std::endl;
            redis_.reset();
            connected_.store(false, std::memory_order_release);
            return false;
        }

        connected_.store(true, std::memory_order_release);
        std::cout << "RedisStreamer: Connected to " << host << ":" << port << std::endl;
        return true;
    } catch (const sw::redis::Error& e) {
        std::cerr << "RedisStreamer: Connection error: " << e.what() << std::endl;
        connected_.store(false, std::memory_order_release);
        return false;
    } catch (const std::exception& e) {
        std::cerr << "RedisStreamer: Unexpected error: " << e.what() << std::endl;
        connected_.store(false, std::memory_order_release);
        return false;
    }
}

void RedisStreamer::subscribe(const std::string& channel, TickCallback callback) {
    if (!connected_.load(std::memory_order_acquire)) {
        std::cerr << "RedisStreamer: Cannot subscribe, not connected" << std::endl;
        return;
    }

    if (streaming_.load(std::memory_order_acquire)) {
        stop();
    }

    channel_ = channel;
    callback_ = std::move(callback);
    should_stop_.store(false, std::memory_order_release);

    try {
        subscriber_ = std::make_unique<sw::redis::Subscriber>(redis_->subscriber());

        // Ուղղված է chan անունով (-Wshadow warning-ից խուսափելու համար)
        subscriber_->on_message([this](std::string chan, std::string msg) {
            handle_redis_message(chan, msg);
        });

        subscriber_->subscribe(channel_);

        worker_thread_ = std::make_unique<std::thread>(&RedisStreamer::run_streaming_loop, this);

        streaming_.store(true, std::memory_order_release);
        std::cout << "RedisStreamer: Subscribed to channel '" << channel_ << "'" << std::endl;
    } catch (const sw::redis::Error& e) {
        std::cerr << "RedisStreamer: Subscribe error: " << e.what() << std::endl;
        streaming_.store(false, std::memory_order_release);
        subscriber_.reset();
    }
}

void RedisStreamer::stop() {
    if (!streaming_.load(std::memory_order_acquire) && !should_stop_.load(std::memory_order_acquire)) {
        return;
    }

    std::cout << "RedisStreamer: Stopping stream..." << std::endl;

    should_stop_.store(true, std::memory_order_release);

    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }

    worker_thread_.reset();
    subscriber_.reset();
    streaming_.store(false, std::memory_order_release);

    std::cout << "RedisStreamer: Stream stopped" << std::endl;
}

void RedisStreamer::run_streaming_loop() {
    int reconnect_attempts = 0;

    while (!should_stop_.load(std::memory_order_acquire)) {
        try {
            if (!subscriber_) {
                if (!reconnect()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
                    ++reconnect_attempts;
                    if (reconnect_attempts >= MAX_RECONNECT_ATTEMPTS) {
                        std::cerr << "RedisStreamer: Max reconnect attempts reached, stopping" << std::endl;
                        streaming_.store(false, std::memory_order_release);
                        break;
                    }
                    continue;
                }
                reconnect_attempts = 0;
            }

            subscriber_->consume();

        } catch (const sw::redis::TimeoutError&) {
            continue;
        } catch (const sw::redis::Error& e) {
            if (should_stop_.load(std::memory_order_acquire)) {
                break;
            }
            std::cerr << "RedisStreamer: Subscriber error: " << e.what() << std::endl;
            subscriber_.reset();
            std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
        } catch (const std::exception& e) {
            if (should_stop_.load(std::memory_order_acquire)) {
                break;
            }
            std::cerr << "RedisStreamer: Loop error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
        }
    }

    streaming_.store(false, std::memory_order_release);
    std::cout << "RedisStreamer: Streaming loop ended" << std::endl;
}

void RedisStreamer::handle_redis_message(const std::string& channel, const std::string& message) {
    auto tick_opt = parse_binance_tick(message);
    if (!tick_opt.has_value()) {
        std::string preview = message.size() > 100 ? message.substr(0, 100) + "..." : message;
        std::cerr << "RedisStreamer: Failed to parse tick from channel '" << channel 
                  << "': " << preview << std::endl;
        return;
    }

    if (callback_) {
        try {
            callback_(tick_opt.value());
        } catch (const std::exception& e) {
            std::cerr << "RedisStreamer: Callback error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "RedisStreamer: Unknown callback error" << std::endl;
        }
    }
}

bool RedisStreamer::reconnect() {
    std::cout << "RedisStreamer: Attempting to reconnect..." << std::endl;

    subscriber_.reset();

    if (connect(host_, port_)) {
        try {
            subscriber_ = std::make_unique<sw::redis::Subscriber>(redis_->subscriber());
            subscriber_->on_message([this](std::string chan, std::string msg) {
                handle_redis_message(chan, msg);
            });
            subscriber_->subscribe(channel_);

            streaming_.store(true, std::memory_order_release);
            std::cout << "RedisStreamer: Reconnected successfully" << std::endl;
            return true;
        } catch (const sw::redis::Error& e) {
            std::cerr << "RedisStreamer: Reconnect subscribe error: " << e.what() << std::endl;
            subscriber_.reset();
            return false;
        }
    }

    return false;
}

} // namespace QuantXCore::data

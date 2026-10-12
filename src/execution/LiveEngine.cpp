#include "QuantXCore/execution/LiveEngine.hpp"
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace QuantXCore::execution {

namespace data = QuantXCore::data;
namespace alpha = QuantXCore::alpha;
namespace risk = QuantXCore::risk;
namespace persistence = QuantXCore::persistence;

using BinanceOmsClient = quantxcore::execution::BinanceOmsClient;
using OrderRequest = quantxcore::execution::OrderRequest;
using OrderSide = quantxcore::execution::OrderSide;
using OrderType = quantxcore::execution::OrderType;

namespace {

namespace Color {
    constexpr const char* RESET          = "\033[0m";
    constexpr const char* BOLD           = "\033[1m";
    constexpr const char* DIM            = "\033[2m";

    constexpr const char* RED            = "\033[31m";
    constexpr const char* GREEN          = "\033[32m";
    constexpr const char* YELLOW         = "\033[33m";
    constexpr const char* BLUE           = "\033[34m";
    constexpr const char* MAGENTA        = "\033[35m";
    constexpr const char* CYAN           = "\033[36m";
    constexpr const char* WHITE          = "\033[37m";

    constexpr const char* BG_GREEN_BLACK = "\033[42m\033[30m\033[1m";
    constexpr const char* BG_RED_WHITE   = "\033[41m\033[37m\033[1m";
    constexpr const char* BG_BLUE_WHITE  = "\033[44m\033[37m\033[1m";
    constexpr const char* BG_CYAN_BLACK  = "\033[46m\033[30m\033[1m";
    constexpr const char* BG_YELLOW_BLACK= "\033[43m\033[30m\033[1m";

    constexpr double BINANCE_SPOT_FEE_RATE = 0.001; // 0.1% Fee
}

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::ostringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string fmt_val(double val, int precision = 4) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << val;
    return oss.str();
}

} // namespace

LiveEngine::LiveEngine(data::BinanceStreamer& streamer,
                       alpha::AlphaEngine& alpha_engine,
                       risk::RiskEngine& risk_engine,
                       std::shared_ptr<BinanceOmsClient> oms,
                       LiveEngineMode mode,
                       double risk_fraction_per_signal)
    : streamer_(streamer),
      alpha_engine_(alpha_engine),
      risk_engine_(risk_engine),
      oms_(oms),
      mode_(mode),
      risk_fraction_per_signal_(risk_fraction_per_signal) {}

LiveEngine::~LiveEngine() {
    stop();
}

void LiveEngine::start() {
    if (is_running_.exchange(true)) {
        return;
    }

    std::cout << Color::CYAN << Color::BOLD
              << "\n[SYSTEM START] Starting QuantXCore Live Trading Engine..."
              << Color::RESET << std::endl;

    recover_positions();

    streamer_.start();
    worker_thread_ = std::thread(&LiveEngine::process_ticks_loop, this);
}

void LiveEngine::stop() {
    if (!is_running_.exchange(false)) {
        return;
    }

    std::cout << Color::YELLOW << Color::BOLD
              << "\n[SYSTEM STOP] Shutting down Live Engine..."
              << Color::RESET << std::endl;

    streamer_.stop();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void LiveEngine::process_ticks_loop() {
    while (is_running_.load()) {
        data::MarketTick tick;
        if (!streamer_.queue().try_pop(tick)) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        handle_tick(tick);
    }
}

void LiveEngine::stop_and_close_all_positions() {
    if (!is_running_.load()) return;

    std::cout << Color::BG_RED_WHITE << " [EMERGENCY] CLOSING ALL ACTIVE POSITIONS " << Color::RESET << std::endl;

    if (db_manager_) {
        auto open_positions = db_manager_->load_open_positions();
        for (const auto& pos : open_positions) {
            if (mode_ == LiveEngineMode::LIVE_TRADING && oms_) {
                OrderRequest oms_req;
                oms_req.symbol = pos.symbol;
                oms_req.side = (pos.side == "BUY") ? OrderSide::SELL : OrderSide::BUY;
                oms_req.type = OrderType::MARKET;
                oms_req.quantity = pos.quantity;

                auto res = oms_->place_order(oms_req);
                if (!res.success) {
                    std::cerr << Color::RED << "[LiveEngine] Failed to close position for "
                              << pos.symbol << ": " << res.raw_response << Color::RESET << std::endl;
                } else {
                    std::cout << Color::GREEN << "[LiveEngine] Successfully closed position for "
                              << pos.symbol << Color::RESET << std::endl;
                }
            }
            db_manager_->delete_position(pos.symbol);
        }
    }

    stop();
}

void LiveEngine::recover_positions() {
    if (!db_manager_) {
        return;
    }

    auto open_positions = db_manager_->load_open_positions();
    if (open_positions.empty()) {
        std::cout << Color::DIM << "[RECOVERY] No open positions found in database." << Color::RESET << std::endl;
        return;
    }

    std::cout << Color::BG_BLUE_WHITE << " [RECOVERY] RESTORING ACTIVE POSITIONS " << Color::RESET << std::endl;

    for (const auto& pos : open_positions) {
        alpha::SignalType side = (pos.side == "BUY") ? alpha::SignalType::BUY : alpha::SignalType::SELL;
        risk_engine_.register_position(pos.symbol, side, pos.entry_price, pos.quantity);

        double qty = (pos.side == "BUY") ? pos.quantity : -pos.quantity;
        double sl_pct = std::abs(pos.entry_price - pos.stop_loss) / pos.entry_price;
        double tp_pct = std::abs(pos.take_profit - pos.entry_price) / pos.entry_price;

        bracket_manager_.set_bracket(
            pos.symbol, pos.entry_price, qty, sl_pct, tp_pct, pos.trailing_pct
        );

        std::cout << Color::CYAN << "  └─ RESTORED: " << Color::BOLD << pos.symbol
                  << Color::RESET << " | Side: " << (pos.side == "BUY" ? Color::GREEN : Color::RED) << pos.side
                  << Color::RESET << " | Entry: $" << fmt_val(pos.entry_price, 2)
                  << " | Qty: " << fmt_val(pos.quantity, 6) << std::endl;
    }
}

constexpr double BINANCE_SPOT_FEE_RATE = 0.001;

void LiveEngine::handle_tick(const data::MarketTick& tick) {
    uint64_t count = ticks_processed_.fetch_add(1, std::memory_order_relaxed) + 1;

    // 1. Periodic Stream Log
    if (count % 50 == 0) {
        std::cout << Color::DIM << "[" << get_timestamp() << "] " << Color::RESET
                  << Color::CYAN << Color::BOLD << "[TICK] " << Color::RESET
                  << std::left << std::setw(8) << tick.symbol
                  << " Price: " << Color::YELLOW << Color::BOLD << "$" << std::right << std::setw(10) << fmt_val(tick.price, 2) << Color::RESET
                  << Color::DIM << " (Ticks: " << count << ")" << Color::RESET << std::endl;
    }

    // 2. Periodic Dashboard
    if (count % 200 == 0) {
        double current_bal = (mode_ == LiveEngineMode::LIVE_TRADING && oms_)
                              ? oms_->get_account_balance("USDT")
                              : risk_engine_.account_state().balance;
        auto acc_state = risk_engine_.account_state();

        std::cout << "\n" << Color::BLUE << "┌──────────────────────────────────────────────────────────────────────┐\n"
                  << "│ " << Color::BOLD << Color::WHITE << "                    QUANTXCORE LIVE DASHBOARD                        " << Color::RESET << Color::BLUE << "│\n"
                  << "├──────────────────────────────────────────────────────────────────────┤\n"
                  << "│ " << Color::RESET << "Mode: " << (mode_ == LiveEngineMode::LIVE_TRADING ? Color::GREEN + std::string("LIVE TRADING") : Color::YELLOW + std::string("PAPER TRADING")) << Color::RESET
                  << " | Market: " << (is_spot_mode_ ? Color::CYAN + std::string("SPOT") : Color::MAGENTA + std::string("FUTURES")) << Color::RESET
                  << " | Balance: " << Color::GREEN << Color::BOLD << "$" << fmt_val(current_bal, 2) << " USDT" << Color::RESET << "\n"
                  << "│ " << Color::RESET << "Active Pos: " << Color::CYAN << acc_state.open_positions_count << Color::RESET
                  << " | Processed Ticks: " << Color::WHITE << count << Color::RESET
                  << " | Last Price: " << Color::YELLOW << "$" << fmt_val(tick.price, 2) << Color::RESET << Color::BLUE << " │\n"
                  << "└──────────────────────────────────────────────────────────────────────┘" << Color::RESET << "\n\n";
    }

    // 3. Bracket Manager Trigger Exit
    auto trigger = bracket_manager_.on_tick(tick);
    if (trigger.type != TriggerType::NONE) {
        auto pos_opt = risk_engine_.get_position(tick.symbol);
        double entry_price = pos_opt.has_value() ? pos_opt->entry_price : trigger.trigger_price;
        double qty = std::abs(trigger.close_quantity);

        bool is_short = pos_opt.has_value() && (pos_opt->side == alpha::SignalType::SELL);
        double gross_pnl = is_short ? (entry_price - trigger.trigger_price) * qty
                                    : (trigger.trigger_price - entry_price) * qty;
        double exit_fee = (trigger.trigger_price * qty) * BINANCE_SPOT_FEE_RATE;
        double net_pnl = gross_pnl - exit_fee;

        bool exit_confirmed = (mode_ != LiveEngineMode::LIVE_TRADING);

        if (mode_ == LiveEngineMode::LIVE_TRADING && oms_) {
            OrderRequest oms_req;
            oms_req.symbol = trigger.symbol;
            oms_req.side = (trigger.close_quantity > 0) ? OrderSide::BUY : OrderSide::SELL;
            oms_req.type = OrderType::MARKET; // Guaranteed execution
            oms_req.quantity = qty;

            auto res = oms_->place_order(oms_req);
            if (!res.success) {
                std::cerr << Color::RED << "[OMS ERROR] Bracket exit order failed: " << res.raw_response
                          << " -- Preserving position state." << Color::RESET << std::endl;
                exit_confirmed = false;
            } else {
                double updated_bal = oms_->get_account_balance("USDT");
                risk_engine_.update_balance(updated_bal);
                exit_confirmed = true;
            }
        } else if (mode_ == LiveEngineMode::PAPER_TRADING) {
            double old_bal = risk_engine_.account_state().balance;
            risk_engine_.update_balance(old_bal + net_pnl);
        }

        if (exit_confirmed) {
            double current_balance = (mode_ == LiveEngineMode::LIVE_TRADING && oms_)
                                      ? oms_->get_account_balance("USDT")
                                      : risk_engine_.account_state().balance;

            std::string trig_type_str = (trigger.type == TriggerType::STOP_LOSS ? "STOP_LOSS" :
                                        trigger.type == TriggerType::TAKE_PROFIT ? "TAKE_PROFIT" : "TRAILING_STOP");
            const char* bg_color = (trigger.type == TriggerType::TAKE_PROFIT) ? Color::BG_GREEN_BLACK : Color::BG_RED_WHITE;

            std::cout << "\n" << bg_color << " [TRADE CLOSED] BRACKET TRIGGERED (" << trig_type_str << ") " << Color::RESET << "\n"
                      << Color::BOLD << " Symbol            : " << Color::CYAN << trigger.symbol << Color::RESET << "\n"
                      << Color::BOLD << " Exit Price        : " << Color::YELLOW << "$" << fmt_val(trigger.trigger_price, 2) << Color::RESET << "\n"
                      << Color::BOLD << " Closed Quantity   : " << fmt_val(qty, 6) << "\n"
                      << Color::BOLD << " Exit Fee (0.1%)   : " << Color::RED << "-$" << fmt_val(exit_fee, 4) << Color::RESET << "\n"
                      << Color::BOLD << " Net Realized PnL  : " << (net_pnl >= 0 ? Color::GREEN : Color::RED) << "$" << fmt_val(net_pnl, 2) << Color::RESET << "\n"
                      << Color::BOLD << " Current Balance   : " << Color::GREEN << "$" << fmt_val(current_balance, 2) << " USDT" << Color::RESET << "\n"
                      << Color::DIM << "----------------------------------------------------------------------" << Color::RESET << std::endl;

            if (db_manager_) {
                db_manager_->delete_position(trigger.symbol);
                persistence::TradeRecord trade{
                    .id = 0,
                    .symbol = trigger.symbol,
                    .side = (trigger.close_quantity > 0 ? "BUY" : "SELL"),
                    .price = trigger.trigger_price,
                    .quantity = qty,
                    .realized_pnl = net_pnl,
                    .timestamp = static_cast<uint64_t>(std::time(nullptr))
                };
                db_manager_->save_trade(trade);
            }

            risk_engine_.unregister_position(tick.symbol);
            risk_engine_.decrement_open_positions();
            risk_engine_.mark_symbol_cooldown(tick.symbol, tick.timestamp);
        }
        return;
    }

    // 4. Risk Engine Dynamic Trailing Exit
    auto exit_req = risk_engine_.on_tick(
        tick.symbol, tick.price, static_cast<std::uint64_t>(tick.timestamp));
    if (exit_req.has_value()) {
        auto pos_opt = risk_engine_.get_position(tick.symbol);
        double entry_price = pos_opt.has_value() ? pos_opt->entry_price : tick.price;
        double exit_price = exit_req->price > 0.0 ? exit_req->price : tick.price;
        double qty = exit_req->quantity;

        bool is_short = (exit_req->side == alpha::SignalType::BUY);
        double gross_pnl = is_short ? (entry_price - exit_price) * qty
                                    : (exit_price - entry_price) * qty;
        double exit_fee = (exit_price * qty) * BINANCE_SPOT_FEE_RATE;
        double net_pnl = gross_pnl - exit_fee;

        bool exit_confirmed = (mode_ != LiveEngineMode::LIVE_TRADING);

        if (mode_ == LiveEngineMode::LIVE_TRADING && oms_) {
            OrderRequest oms_req;
            oms_req.symbol = exit_req->symbol;
            oms_req.side = (exit_req->side == alpha::SignalType::BUY) ? OrderSide::BUY : OrderSide::SELL;
            oms_req.type = OrderType::MARKET; // Force MARKET order
            oms_req.quantity = qty;

            auto res = oms_->place_order(oms_req);
            if (!res.success) {
                std::cerr << Color::RED << "[OMS ERROR] Risk exit order failed: " << res.raw_response
                          << " -- Preserving position state." << Color::RESET << std::endl;
                exit_confirmed = false;
            } else {
                double updated_bal = oms_->get_account_balance("USDT");
                risk_engine_.update_balance(updated_bal);
                exit_confirmed = true;
            }
        } else if (mode_ == LiveEngineMode::PAPER_TRADING) {
            double old_bal = risk_engine_.account_state().balance;
            risk_engine_.update_balance(old_bal + net_pnl);
        }

        if (exit_confirmed) {
            double current_balance = (mode_ == LiveEngineMode::LIVE_TRADING && oms_)
                                      ? oms_->get_account_balance("USDT")
                                      : risk_engine_.account_state().balance;

            std::cout << "\n" << Color::BG_YELLOW_BLACK << " [TRADE CLOSED] RISK ENGINE TRAILING EXIT " << Color::RESET << "\n"
                      << Color::BOLD << " Symbol            : " << Color::CYAN << exit_req->symbol << Color::RESET << "\n"
                      << Color::BOLD << " Exit Price        : " << Color::YELLOW << "$" << fmt_val(exit_price, 2) << Color::RESET << "\n"
                      << Color::BOLD << " Exit Quantity     : " << fmt_val(qty, 6) << "\n"
                      << Color::BOLD << " Net Realized PnL  : " << (net_pnl >= 0 ? Color::GREEN : Color::RED) << "$" << fmt_val(net_pnl, 2) << Color::RESET << "\n"
                      << Color::BOLD << " Current Balance   : " << Color::GREEN << "$" << fmt_val(current_balance, 2) << " USDT" << Color::RESET << "\n"
                      << Color::BOLD << " Reason/Strategy   : " << Color::MAGENTA << exit_req->strategy_name << Color::RESET << "\n"
                      << Color::DIM << "----------------------------------------------------------------------" << Color::RESET << std::endl;

            if (db_manager_) {
                db_manager_->delete_position(exit_req->symbol);
                persistence::TradeRecord trade{
                    .id = 0,
                    .symbol = exit_req->symbol,
                    .side = (exit_req->side == alpha::SignalType::BUY ? "BUY" : "SELL"),
                    .price = exit_price,
                    .quantity = qty,
                    .realized_pnl = net_pnl,
                    .timestamp = static_cast<uint64_t>(std::time(nullptr))
                };
                db_manager_->save_trade(trade);
            }

            bracket_manager_.remove_bracket(tick.symbol);
            risk_engine_.unregister_position(tick.symbol);
            risk_engine_.decrement_open_positions();
            risk_engine_.mark_symbol_cooldown(tick.symbol, tick.timestamp);
        }
        return;
    }

    // 5. Strategy Alpha Signals Processing
    auto signals = alpha_engine_.on_tick(tick);
    for (const auto& signal : signals) {
        if (signal.type == alpha::SignalType::HOLD) {
            continue;
        }

        bool has_active_pos = risk_engine_.has_position(tick.symbol);

        // SPOT Rules
        if (is_spot_mode_) {
            if (signal.type == alpha::SignalType::SELL && !has_active_pos) {
                continue;
            }

            // Rule B: Manual Signal Exit
            if (signal.type == alpha::SignalType::SELL && has_active_pos) {
                auto pos_opt = risk_engine_.get_position(tick.symbol);
                if (pos_opt.has_value()) {
                    uint64_t now_sec = static_cast<uint64_t>(std::time(nullptr));
                    uint64_t pos_duration = (now_sec > pos_opt->opened_timestamp) ? (now_sec - pos_opt->opened_timestamp) : 0;

                    if (pos_duration < 60) {
                        continue;
                    }

                    double entry_price = pos_opt->entry_price;
                    double qty = pos_opt->quantity;
                    double gross_pnl = (tick.price - entry_price) * qty;
                    double exit_fee = (tick.price * qty) * BINANCE_SPOT_FEE_RATE;
                    double net_pnl = gross_pnl - exit_fee;

                    bool spot_exit_confirmed = (mode_ != LiveEngineMode::LIVE_TRADING);

                    if (mode_ == LiveEngineMode::LIVE_TRADING && oms_) {
                        OrderRequest oms_req;
                        oms_req.symbol = tick.symbol;
                        oms_req.side = OrderSide::SELL;
                        oms_req.type = OrderType::MARKET;
                        oms_req.quantity = qty;

                        auto res = oms_->place_order(oms_req);
                        if (res.success) {
                            double updated_bal = oms_->get_account_balance("USDT");
                            risk_engine_.update_balance(updated_bal);
                            spot_exit_confirmed = true;
                        } else {
                            std::cerr << Color::RED << "[OMS ERROR] Spot signal exit failed for " << tick.symbol
                                      << ": " << res.raw_response << Color::RESET << std::endl;
                        }
                    } else if (mode_ == LiveEngineMode::PAPER_TRADING) {
                        double old_bal = risk_engine_.account_state().balance;
                        risk_engine_.update_balance(old_bal + net_pnl);
                    }

                    if (spot_exit_confirmed) {
                        bracket_manager_.remove_bracket(tick.symbol);
                        risk_engine_.unregister_position(tick.symbol);
                        risk_engine_.decrement_open_positions();
                        risk_engine_.mark_symbol_cooldown(tick.symbol, tick.timestamp);

                        if (db_manager_) {
                            db_manager_->delete_position(tick.symbol);
                            persistence::TradeRecord trade{
                                .id = 0,
                                .symbol = tick.symbol,
                                .side = "SELL",
                                .price = tick.price,
                                .quantity = qty,
                                .realized_pnl = net_pnl,
                                .timestamp = static_cast<uint64_t>(std::time(nullptr))
                            };
                            db_manager_->save_trade(trade);
                        }

                        std::cout << "\n" << Color::BG_YELLOW_BLACK << " [SPOT SIGNAL EXIT] CLOSED POSITION ON STRATEGY SELL " << Color::RESET << "\n"
                            << Color::BOLD << " Symbol            : " << Color::CYAN << tick.symbol << Color::RESET << "\n"
                            << Color::BOLD << " Exit Price        : $" << fmt_val(tick.price, 2) << "\n"
                            << Color::BOLD << " Realized PnL      : " << (net_pnl >= 0 ? Color::GREEN : Color::RED) << "$" << fmt_val(net_pnl, 2) << Color::RESET << "\n"
                            << Color::BOLD << " New Balance       : $" << fmt_val(risk_engine_.account_state().balance, 2) << " USDT\n"
                            << Color::DIM << "----------------------------------------------------------------------" << Color::RESET << std::endl;
                    }
                }
                continue;
            }
        }

        // Live Balance Sync
        if (mode_ == LiveEngineMode::LIVE_TRADING && oms_) {
            double live_bal = oms_->get_account_balance("USDT");
            if (live_bal > 0.0) {
                risk_engine_.update_balance(live_bal);
            }
        }

        // Standard Entry Processing
        auto [check_res, order_req] = risk_engine_.process_signal(signal, risk_fraction_per_signal_);
        if (check_res == risk::RiskCheckResult::APPROVED && order_req.has_value()) {
            double cur_balance = risk_engine_.account_state().balance;
            double notional_usd = order_req->quantity * tick.price;
            double entry_fee = notional_usd * BINANCE_SPOT_FEE_RATE;

            if (mode_ == LiveEngineMode::PAPER_TRADING) {
                risk_engine_.update_balance(cur_balance - entry_fee);
                cur_balance = risk_engine_.account_state().balance;
            }

            const char* side_bg = (signal.type == alpha::SignalType::BUY) ? Color::BG_GREEN_BLACK : Color::BG_RED_WHITE;

            std::cout << "\n" << side_bg << " [SIGNAL APPROVED & EXECUTED] >>> " << alpha::type_to_string(signal.type) << " <<< " << Color::RESET << "\n"
                      << Color::BOLD << " Symbol            : " << Color::CYAN << tick.symbol << Color::RESET << "\n"
                      << Color::BOLD << " Account Balance   : " << Color::GREEN << "$" << fmt_val(cur_balance, 2) << " USDT" << Color::RESET << "\n"
                      << Color::BOLD << " Risk Fraction     : " << fmt_val(risk_fraction_per_signal_ * 100.0, 1) << "%\n"
                      << Color::BOLD << " Position Size USD : " << Color::YELLOW << "$" << fmt_val(notional_usd, 2) << " USDT" << Color::RESET << "\n"
                      << Color::BOLD << " Entry Fee (0.1%)   : $" << fmt_val(entry_fee, 4) << "\n"
                      << Color::BOLD << " Entry Price       : $" << fmt_val(tick.price, 2) << "\n"
                      << Color::BOLD << " Calculated Qty    : " << fmt_val(order_req->quantity, 6) << "\n"
                      << Color::BOLD << " Target Stop Loss  : " << Color::RED << "$" << fmt_val(order_req->stop_loss_price, 2) << Color::RESET << "\n"
                      << Color::BOLD << " Target Take Profit: " << Color::GREEN << "$" << fmt_val(order_req->take_profit_price, 2) << Color::RESET << "\n"
                      << Color::BOLD << " Signal Strength   : " << fmt_val(signal.strength, 2) << "\n"
                      << Color::BOLD << " Strategy Name     : " << Color::MAGENTA << signal.strategy_name << Color::RESET << "\n"
                      << Color::DIM << "----------------------------------------------------------------------" << Color::RESET << std::endl;

            if (mode_ == LiveEngineMode::LIVE_TRADING && oms_) {
                OrderRequest oms_req;
                oms_req.symbol = order_req->symbol;
                oms_req.side = (order_req->side == alpha::SignalType::BUY) ? OrderSide::BUY : OrderSide::SELL;
                oms_req.type = (order_req->price > 0.0) ? OrderType::LIMIT : OrderType::MARKET;
                oms_req.quantity = order_req->quantity;
                oms_req.price = order_req->price;

                auto res = oms_->place_order(oms_req);
                if (!res.success) {
                    std::cerr << Color::RED << "[OMS ERROR] Order execution failed on Binance: " << res.raw_response << Color::RESET << std::endl;
                    continue;
                } else {
                    double new_bal = oms_->get_account_balance("USDT");
                    risk_engine_.update_balance(new_bal);
                }
            }

            risk_engine_.register_position(tick.symbol, signal.type, tick.price, order_req->quantity);
            risk_engine_.increment_open_positions();

            double qty = (signal.type == alpha::SignalType::BUY) ? order_req->quantity : -order_req->quantity;

            bracket_manager_.set_bracket_exact(
                tick.symbol,
                tick.price,
                qty,
                order_req->stop_loss_price,
                order_req->take_profit_price,
                default_trailing_pct_
            );

            if (db_manager_) {
                persistence::PositionRecord pos{
                    .symbol = tick.symbol,
                    .side = (signal.type == alpha::SignalType::BUY ? "BUY" : "SELL"),
                    .entry_price = tick.price,
                    .quantity = order_req->quantity,
                    .stop_loss = order_req->stop_loss_price,
                    .take_profit = order_req->take_profit_price,
                    .peak_price = tick.price,
                    .trailing_pct = default_trailing_pct_
                };
                db_manager_->save_position(pos);
            }
        }
    }
}

} // namespace QuantXCore::execution

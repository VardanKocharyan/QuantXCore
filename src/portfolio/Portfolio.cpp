#include "QuantXCore/portfolio/Portfolio.hpp"

#include <algorithm>
#include <cmath>

#include "QuantXCore/alpha/Signal.hpp"        // alpha::SignalType
#include "QuantXCore/data/MarketTick.hpp"     // data::MarketTick
#include "QuantXCore/execution/PositionTracker.hpp"  // execution::Position (quantity)

namespace QuantXCore::portfolio {

namespace {
// Positions with |quantity| below this are treated as flat/closed for the
// purposes of open-position counting, to absorb floating-point noise from
// repeated partial fills.
constexpr double kFlatPositionEpsilon = 1e-12;
} // namespace

Portfolio::Portfolio(double initial_balance, double leverage)
    : initial_balance_(initial_balance),
      current_cash_(initial_balance),
      leverage_(leverage),
      peak_equity_(initial_balance),
      daily_start_balance_(initial_balance) {}

void Portfolio::process_fill(const execution::Order& filled_order, double commission_pct) {
    if (filled_order.status != execution::OrderStatus::FILLED &&
        filled_order.status != execution::OrderStatus::PARTIALLY_FILLED) {
        return;
    }

    const double trade_val = filled_order.filled_quantity * filled_order.avg_fill_price;
    const double comm = trade_val * commission_pct;

    std::lock_guard<std::mutex> lock(mutex_);

    total_commissions_paid_ += comm;

    switch (filled_order.side) {
        case alpha::SignalType::BUY:
            current_cash_ -= (trade_val + comm);
            break;
        case alpha::SignalType::SELL:
            current_cash_ += (trade_val - comm);
            break;
        default:
            // Non-directional signal types have no direct cash impact.
            break;
    }

    position_tracker_.on_fill(filled_order);
    update_open_positions_locked(filled_order.symbol);
    update_peak_equity_locked();
}

void Portfolio::on_tick(const data::MarketTick& tick) {
    std::lock_guard<std::mutex> lock(mutex_);
    position_tracker_.on_tick(tick);
    update_peak_equity_locked();
}

double Portfolio::get_cash() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Վերադարձնում է իրական ազատ կանխիկը (եթե բացասական է, վերադարձնում է 0)
    return std::max(0.0, current_cash_);
}

double Portfolio::get_realized_pnl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return position_tracker_.get_total_realized_pnl();
}

double Portfolio::get_unrealized_pnl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return position_tracker_.get_total_unrealized_pnl();
}

double Portfolio::get_total_equity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return unlocked_total_equity();
}

double Portfolio::get_buying_power() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // FIX: SPOT market-ում (leverage <= 1.0) Buying Power-ը ՍԱՀՄԱՆԱՓԱԿՎԱԾ Է
    // ՄԻԱՅՆ ազատ կանխիկով (current_cash_):
    // Գնված ակտիվի արժեքը չի կարող օգտագործվել նոր SPOT գնում անելու համար:
    if (leverage_ <= 1.0) {
        return std::max(0.0, current_cash_);
    }

    // Futures/Margin ռեժիմում հաշվի է առնվում Total Equity * Leverage
    return std::max(0.0, unlocked_total_equity() * leverage_);
}

double Portfolio::get_total_commissions_paid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_commissions_paid_;
}

double Portfolio::get_leverage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return leverage_;
}

double Portfolio::get_initial_balance() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initial_balance_;
}

std::size_t Portfolio::get_open_positions_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_symbols_.size();
}

risk::AccountState Portfolio::get_account_state() const {
    std::lock_guard<std::mutex> lock(mutex_);

    risk::AccountState state;
    state.balance = unlocked_total_equity();
    state.peak_balance = peak_equity_;
    state.daily_start_balance = daily_start_balance_;
    state.open_positions_count = open_symbols_.size();
    return state;
}

void Portfolio::reset(double initial_balance) {
    std::lock_guard<std::mutex> lock(mutex_);

    initial_balance_ = initial_balance;
    current_cash_ = initial_balance;
    total_commissions_paid_ = 0.0;
    peak_equity_ = initial_balance;
    daily_start_balance_ = initial_balance;
    open_symbols_.clear();
    position_tracker_.reset();
}

double Portfolio::unlocked_total_equity() const {
    return current_cash_ + position_tracker_.get_total_unrealized_pnl();
}

void Portfolio::update_peak_equity_locked() {
    const double equity = unlocked_total_equity();
    peak_equity_ = std::max(peak_equity_, equity);
}

void Portfolio::update_open_positions_locked(const std::string& symbol) {
    const execution::Position pos = position_tracker_.get_position(symbol);
    if (std::abs(pos.quantity) > kFlatPositionEpsilon) {
        open_symbols_.insert(symbol);
    } else {
        open_symbols_.erase(symbol);
    }
}

} // namespace QuantXCore::portfolio

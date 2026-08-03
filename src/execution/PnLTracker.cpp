#include "QuantXCore/execution/PnLTracker.hpp"

namespace QuantXCore::execution {

PnLTracker::PnLTracker(double initial_balance) : initial_balance_(initial_balance) {
    history_.reserve(kDefaultHistoryReserve);
}

void PnLTracker::reset(double initial_balance) {
    initial_balance_ = initial_balance;
    history_.clear();
    history_.reserve(kDefaultHistoryReserve);
}

void PnLTracker::record_snapshot(std::uint64_t timestamp, double realized_pnl, double unrealized_pnl) {
    const double total_pnl = realized_pnl + unrealized_pnl;
    history_.push_back(PnLSnapshot{
        .timestamp = timestamp,
        .realized_pnl = realized_pnl,
        .unrealized_pnl = unrealized_pnl,
        .total_pnl = total_pnl,
        .equity = initial_balance_ + total_pnl,
    });
}

const std::vector<PnLSnapshot>& PnLTracker::history() const noexcept {
    return history_;
}

double PnLTracker::initial_balance() const noexcept {
    return initial_balance_;
}

double PnLTracker::current_equity() const noexcept {
    return history_.empty() ? initial_balance_ : history_.back().equity;
}

}  // namespace QuantXCore::execution

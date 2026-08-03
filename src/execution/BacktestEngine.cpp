#include "QuantXCore/execution/BacktestEngine.hpp"

#include <chrono>
#include <cstdint>

namespace QuantXCore::execution {

BacktestEngine::BacktestEngine(alpha::AlphaEngine& alpha_engine,
                                risk::RiskEngine& risk_engine,
                                BacktestExecutionConfig config,
                                double risk_fraction_per_signal)
    : alpha_engine_(alpha_engine),
      risk_engine_(risk_engine),
      risk_fraction_per_signal_(risk_fraction_per_signal),
      execution_client_(std::make_shared<BacktestExecutionClient>(config)),
      order_manager_(execution_client_),
      position_tracker_(),
      pnl_tracker_(0.0) {}

BacktestResult BacktestEngine::run(const std::vector<data::MarketTick>& ticks,
                                    double initial_balance) {
    using clock = std::chrono::steady_clock;
    const auto start_time = clock::now();

    execution_client_->reset();

    position_tracker_.reset();

    pnl_tracker_.reset(initial_balance);

    risk_engine_.reset(initial_balance);

    BacktestResult result;
    result.initial_balance = initial_balance;
    result.total_ticks = ticks.size();

    for (const auto& tick : ticks) {
        // 1. Mark-to-market: update position mark price / unrealized PnL.
        position_tracker_.on_tick(tick);

        // 2. Feed the tick to alpha and collect signals.
        std::vector<alpha::Signal> signals = alpha_engine_.on_tick(tick);
        result.total_signals += signals.size();

        for (const auto& signal : signals) {
            if (signal.type == alpha::SignalType::HOLD) {
                continue;
            }

            // 3. Risk-check / size the signal into a concrete order request.
            auto [check_result, request_opt] =
                risk_engine_.process_signal(signal, risk_fraction_per_signal_);
            if (check_result != risk::RiskCheckResult::APPROVED || !request_opt.has_value()) {
                continue;  // rejected by risk engine
            }

            // 4. Execute the approved request through the simulated venue.
            std::optional<Order> order_opt =
                order_manager_.execute_approved_request(*request_opt);
            if (!order_opt.has_value() || order_opt->status != OrderStatus::FILLED) {
                continue;
            }

            // 5. Wire the fill back into position/PnL tracking.
            const Order& order = *order_opt;
            position_tracker_.on_fill(order);
            ++result.total_orders_executed;

        }
        
        pnl_tracker_.record_snapshot(
            static_cast<std::uint64_t>(tick.timestamp),
            position_tracker_.get_total_realized_pnl(),
            position_tracker_.get_total_unrealized_pnl()
        );
    }

    const auto end_time = clock::now();
    result.execution_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();

    result.gross_realized_pnl = position_tracker_.get_total_realized_pnl();
    result.total_commission_paid = execution_client_->total_commission_paid();
    result.net_realized_pnl = result.gross_realized_pnl - result.total_commission_paid;
    result.final_balance = initial_balance + result.net_realized_pnl;
    result.pnl_history = pnl_tracker_.history();

    return result;
}

BacktestResult BacktestEngine::run_from_csv(const std::string& csv_file_path,
                                             data::CsvTickParser::Format format,
                                             double initial_balance) {
    data::CsvTickParser parser(csv_file_path, format);
    const std::vector<data::MarketTick> ticks = parser.parse_all();
    return run(ticks, initial_balance);
}

}  // namespace QuantXCore::execution

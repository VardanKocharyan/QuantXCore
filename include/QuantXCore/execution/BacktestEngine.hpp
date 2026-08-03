#pragma once
// ============================================================================
// QuantXCore::execution::BacktestEngine
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "QuantXCore/alpha/AlphaEngine.hpp"
#include "QuantXCore/data/CsvTickParser.hpp"
#include "QuantXCore/data/MarketTick.hpp"
#include "QuantXCore/execution/BacktestExecutionClient.hpp"
#include "QuantXCore/execution/OrderManager.hpp"
#include "QuantXCore/execution/PositionTracker.hpp"
#include "QuantXCore/execution/PnLTracker.hpp"
#include "QuantXCore/risk/RiskEngine.hpp"

namespace QuantXCore::execution {

struct BacktestResult {
    std::size_t total_ticks{};
    std::size_t total_signals{};
    std::size_t total_orders_executed{};

    double initial_balance{};
    double final_balance{};
    double gross_realized_pnl{};   // realized PnL before commission
    double net_realized_pnl{};     // realized PnL after commission
    double total_commission_paid{};

    double execution_time_ms{};

    std::vector<PnLSnapshot> pnl_history;
};

class BacktestEngine {
public:
    BacktestEngine(alpha::AlphaEngine& alpha_engine,
                   risk::RiskEngine& risk_engine,
                   BacktestExecutionConfig config,
                   double risk_fraction_per_signal = 0.02);

    [[nodiscard]] BacktestResult run(const std::vector<data::MarketTick>& ticks,
                                      double initial_balance = 100'000.0);

    [[nodiscard]] BacktestResult run_from_csv(const std::string& csv_file_path,
                                               data::CsvTickParser::Format format,
                                               double initial_balance = 100'000.0);

    [[nodiscard]] const PositionTracker& position_tracker() const noexcept { return position_tracker_; }
    [[nodiscard]] const PnLTracker& pnl_tracker() const noexcept { return pnl_tracker_; }
    [[nodiscard]] const BacktestExecutionClient& execution_client() const noexcept { return *execution_client_; }

private:
    alpha::AlphaEngine& alpha_engine_;
    risk::RiskEngine&   risk_engine_;
    double              risk_fraction_per_signal_;

    std::shared_ptr<BacktestExecutionClient> execution_client_;
    OrderManager                             order_manager_;
    PositionTracker                          position_tracker_;
    PnLTracker                               pnl_tracker_;
};

}  // namespace QuantXCore::execution

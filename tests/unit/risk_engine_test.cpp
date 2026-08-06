#include "QuantXCore/risk/RiskEngine.hpp"

#include <gtest/gtest.h>

#include "QuantXCore/alpha/Signal.hpp"
#include "QuantXCore/risk/PositionSizer.hpp"
#include "QuantXCore/risk/RiskLimits.hpp"

namespace QuantXCore::risk {
namespace {

using alpha::Signal;
using alpha::SignalType;

// Builds a well-formed BUY signal for AAPL at $150, ready for the engine.
Signal MakeBuySignal(double price = 150.0, double strength = 0.8,
                      std::uint64_t timestamp = 1000) {
  Signal s;
  s.symbol = "AAPL";
  s.type = SignalType::BUY;
  s.price = price;
  s.strength = strength;
  s.timestamp = timestamp;
  s.strategy_name = "unit_test_strategy";
  return s;
}

AccountState MakeAccountState(double balance) {
  AccountState state;
  state.balance = balance;
  state.peak_balance = balance;
  state.daily_start_balance = balance;
  state.open_positions_count = 0;
  return state;
}

TEST(RiskEngineTest, RejectsHoldSignal) {
  RiskEngine engine(RiskLimits{}, MakeAccountState(100000.0));
  Signal hold_signal = MakeBuySignal();
  hold_signal.type = SignalType::HOLD;

  const auto [result, order] = engine.process_signal(hold_signal);

  EXPECT_EQ(result, RiskCheckResult::REJECTED_INVALID_SIGNAL);
  EXPECT_FALSE(order.has_value());
}

TEST(RiskEngineTest, RejectsInvalidSignalEmptySymbol) {
  RiskEngine engine(RiskLimits{}, MakeAccountState(100000.0));
  Signal invalid_signal = MakeBuySignal();
  invalid_signal.symbol.clear();

  const auto [result, order] = engine.process_signal(invalid_signal);

  EXPECT_EQ(result, RiskCheckResult::REJECTED_INVALID_SIGNAL);
  EXPECT_FALSE(order.has_value());
}

TEST(RiskEngineTest, RejectsWhenDrawdownExceedsLimit) {
  RiskLimits limits;
  limits.max_drawdown_pct = 0.15;  // 15%.

  // Peak was 100k, balance dropped to 80k -> 20% drawdown > 15% limit.
  AccountState state = MakeAccountState(80000.0);
  state.peak_balance = 100000.0;

  RiskEngine engine(limits, state);
  const auto [result, order] = engine.process_signal(MakeBuySignal());

  EXPECT_EQ(result, RiskCheckResult::REJECTED_MAX_DRAWDOWN);
  EXPECT_FALSE(order.has_value());
}

TEST(RiskEngineTest, RejectsWhenDailyLossExceedsLimit) {
  RiskLimits limits;
  limits.max_daily_loss_pct = 0.05;  // 5%.

  // Day started at 100k, balance dropped to 93k -> 7% daily loss > 5%.
  AccountState state = MakeAccountState(93000.0);
  state.peak_balance = 100000.0;
  state.daily_start_balance = 100000.0;

  RiskEngine engine(limits, state);
  const auto [result, order] = engine.process_signal(MakeBuySignal());

  EXPECT_EQ(result, RiskCheckResult::REJECTED_DAILY_LOSS);
  EXPECT_FALSE(order.has_value());
}

TEST(RiskEngineTest, RejectsWhenMaxOpenPositionsReached) {
  RiskLimits limits;
  limits.max_open_positions = 3;

  RiskEngine engine(limits, MakeAccountState(100000.0));
  engine.increment_open_positions();
  engine.increment_open_positions();
  engine.increment_open_positions();  // Now at the limit (3).

  const auto [result, order] = engine.process_signal(MakeBuySignal());

  EXPECT_EQ(result, RiskCheckResult::REJECTED_MAX_OPEN_POSITIONS);
  EXPECT_FALSE(order.has_value());
}

TEST(RiskEngineTest, ApprovesValidBuySignalAndProducesSizedOrder) {
  RiskLimits limits;  // Defaults: 15% DD, 5% daily loss, $100k max, 5 pos.

  RiskEngine engine(limits, MakeAccountState(100000.0));
  const Signal signal = MakeBuySignal(/*price=*/150.0);

  const auto [result, maybe_order] =
      engine.process_signal(signal, /*account_risk_pct=*/0.02);

  ASSERT_EQ(result, RiskCheckResult::APPROVED);
  ASSERT_TRUE(maybe_order.has_value());

  const OrderRequest& order = *maybe_order;
  EXPECT_EQ(order.symbol, "AAPL");
  EXPECT_EQ(order.side, SignalType::BUY);
  EXPECT_EQ(order.strategy_name, "unit_test_strategy");
  EXPECT_DOUBLE_EQ(order.price, 150.0);

  // Default 2% SL below entry, 4% TP above entry for a BUY.
  EXPECT_NEAR(order.stop_loss_price, 150.0 * 0.98, 1e-9);
  EXPECT_NEAR(order.take_profit_price, 150.0 * 1.04, 1e-9);

  // Risk budget = 100000 * 0.02 = 2000. Per-unit risk = 150 - 147 = 3.
  // Raw quantity = 2000 / 3 ≈ 666.67. Notional = 666.67 * 150 ≈ 100000,
  // which sits right at the $100k cap, so it should be capped to exactly
  // max_position_size_usd / price.
  const double expected_quantity =
      limits.max_position_size_usd / signal.price;
  EXPECT_NEAR(order.quantity, expected_quantity, 1e-6);
  EXPECT_GT(order.quantity, 0.0);

  // Resulting notional must never exceed the configured cap.
  EXPECT_LE(order.quantity * order.price,
            limits.max_position_size_usd + 1e-6);
}

TEST(RiskEngineTest, ApprovesValidSellSignalWithInvertedStops) {
  RiskEngine engine(RiskLimits{}, MakeAccountState(50000.0));

  Signal sell_signal = MakeBuySignal(/*price=*/200.0);
  sell_signal.type = SignalType::SELL;

  const auto [result, maybe_order] = engine.process_signal(sell_signal);

  ASSERT_EQ(result, RiskCheckResult::APPROVED);
  ASSERT_TRUE(maybe_order.has_value());

  const OrderRequest& order = *maybe_order;
  EXPECT_EQ(order.side, SignalType::SELL);
  // For a SELL, stop-loss sits above entry and take-profit sits below.
  EXPECT_NEAR(order.stop_loss_price, 200.0 * 1.02, 1e-9);
  EXPECT_NEAR(order.take_profit_price, 200.0 * 0.96, 1e-9);
}

TEST(RiskEngineTest, PositionSizerCapsQuantityToNotionalLimit) {
  const double quantity = PositionSizer::ComputeSizedQuantity(
      /*account_balance=*/1000000.0, /*account_risk_pct=*/0.5,
      /*entry_price=*/100.0, /*stop_loss_price=*/90.0,
      /*max_position_size_usd=*/50000.0);

  // Uncapped risk-based quantity would be huge (0.5 * 1e6 / 10 = 50000
  // units), so the notional cap of $50k at $100/unit -> 500 units must
  // dominate.
  EXPECT_NEAR(quantity, 500.0, 1e-6);
}

TEST(RiskEngineTest, PositionSizerReturnsZeroOnDegenerateInputs) {
  EXPECT_DOUBLE_EQ(
      PositionSizer::ComputeSizedQuantity(0.0, 0.02, 100.0, 98.0, 50000.0),
      0.0);
  EXPECT_DOUBLE_EQ(
      PositionSizer::ComputeSizedQuantity(100000.0, 0.02, 100.0, 100.0,
                                           50000.0),
      0.0);
}

TEST(RiskEngineTest, AccountStateAccessorsReflectMutations) {
  RiskEngine engine(RiskLimits{}, MakeAccountState(100000.0));

  engine.update_balance(120000.0);
  engine.increment_open_positions();
  engine.increment_open_positions();
  engine.decrement_open_positions();

  const AccountState state = engine.account_state();
  EXPECT_DOUBLE_EQ(state.balance, 120000.0);
  EXPECT_DOUBLE_EQ(state.peak_balance, 120000.0);  // New high-water mark.
  EXPECT_EQ(state.open_positions_count, 1u);
}

TEST(RiskEngineTest, RateLimiterRejectsExceedingOrders) {
  QuantXCore::risk::RiskLimits limits;
  limits.max_orders_per_second = 2; // Maximum 2 orders per sec
  QuantXCore::risk::AccountState state{10000.0, 10000.0, 10000.0, 0};
  QuantXCore::risk::RiskEngine engine(limits, state);

  QuantXCore::alpha::Signal signal;
  signal.symbol = "BTCUSDT";
  signal.type = QuantXCore::alpha::SignalType::BUY;
  signal.price = 50000.0;
  signal.strength = 0.8;

  // First 2 orders should be approved
  auto [res1, order1] = engine.process_signal(signal);
  EXPECT_EQ(res1, QuantXCore::risk::RiskCheckResult::APPROVED);

  auto [res2, order2] = engine.process_signal(signal);
  EXPECT_EQ(res2, QuantXCore::risk::RiskCheckResult::APPROVED);

  // 3rd order in rapid succession should be rejected by rate limiter
  auto [res3, order3] = engine.process_signal(signal);
  EXPECT_EQ(res3, QuantXCore::risk::RiskCheckResult::REJECTED_RATE_LIMIT_EXCEEDED);
}

TEST(RiskEngineTest, TrailingStopTriggersOnLongPosition) {
  QuantXCore::risk::RiskLimits limits;
  limits.trailing_stop_pct = 0.02; // 2% trailing stop
  QuantXCore::risk::AccountState state{10000.0, 10000.0, 10000.0, 0};
  QuantXCore::risk::RiskEngine engine(limits, state);

  // Register BUY position at $100
  engine.register_position("BTCUSDT", QuantXCore::alpha::SignalType::BUY, 100.0);

  // Price rises to $110 (Extreme price becomes $110, dynamic stop set to 110 * 0.98 = $107.8)
  auto exit1 = engine.on_tick("BTCUSDT", 110.0, 1000);
  EXPECT_FALSE(exit1.has_value());

  // Price drops slightly to $108.5 (above stop price of $107.8)
  auto exit2 = engine.on_tick("BTCUSDT", 108.5, 1001);
  EXPECT_FALSE(exit2.has_value());

  // Price drops below stop price to $107.0 -> Dynamic Trailing Stop Triggered!
  auto exit3 = engine.on_tick("BTCUSDT", 107.0, 1002);
  ASSERT_TRUE(exit3.has_value());
  EXPECT_EQ(exit3->side, QuantXCore::alpha::SignalType::SELL);
  EXPECT_EQ(exit3->symbol, "BTCUSDT");
  EXPECT_DOUBLE_EQ(exit3->stop_loss_price, 107.8);
}

TEST(RiskEngineTest, FeeValidationRejectsHighFee) {
  QuantXCore::risk::RiskLimits limits;
  limits.max_fee_pct = 0.001; // 0.1% max fee limit
  QuantXCore::risk::AccountState state{10000.0, 10000.0, 10000.0, 0};
  QuantXCore::risk::RiskEngine engine(limits, state);

  EXPECT_TRUE(engine.validate_fee(0.0008));  // 0.08% fee is acceptable
  EXPECT_FALSE(engine.validate_fee(0.0015)); // 0.15% fee exceeds limit
}

}  // namespace
}  // namespace QuantXCore::risk

//===----------------------------------------------------------------------===//
// Copyright (c) 2026 QuantXCore Team.
// SPDX-License-Identifier: Proprietary.
//===----------------------------------------------------------------------===//

#include "QuantXCore/core/alpha_engine.hpp"
#include <gtest/gtest.h>

namespace quantxcore::core {
namespace {

constexpr double kTolerance = 1e-12;

TEST(StochasticAlphaEngineTest, MatchesJohnOracleVectorFirstTwoSteps) {
    AlphaParams params{};
    params.tau_x = 60.0;
    params.tau_i = 300.0;
    params.I_bar = 0.0;
    params.alpha_0 = 0.0;
    params.alpha_1 = 0.001;
    params.alpha_2 = -0.0005;
    params.sigma_x = 0.0002;
    params.sigma_i = 0.01;
    params.rho = -0.3;
    params.dt = 1.0;

    StochasticAlphaEngine engine(params, 0.0, 0.1);

    // Step 1 (seed = 42 raw independent normals)
    engine.step(0.4967141530112327, -0.13826430117118466);

    // Verified Oracle Values for t=1
    EXPECT_NEAR(engine.x(), 0.0001004086511919, kTolerance);
    EXPECT_NEAR(engine.i(), 0.0969598511100000, kTolerance);

    // Step 2 (seed = 42 next block)
    engine.step(0.6476885381006925, 1.5230298565416047);

    // Verified Oracle Values for t=2
    EXPECT_NEAR(engine.x(), 0.0002299802917032, kTolerance);
    EXPECT_NEAR(engine.i(), 0.1093215395221263, kTolerance);
}

} // namespace
} // namespace quantxcore::core


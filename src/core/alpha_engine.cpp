//===----------------------------------------------------------------------===//
// Copyright (c) 2026 QuantXCore Team.
// SPDX-License-Identifier: Proprietary.
//===----------------------------------------------------------------------===//

#include "QuantXCore/core/alpha_engine.hpp"
#include <cmath>
#include <stdexcept>

namespace quantxcore::core {

void AlphaParams::validate() const {
    if (!(tau_x > 0.0)) throw std::invalid_argument("AlphaParams::tau_x must be > 0");
    if (!(tau_i > 0.0)) throw std::invalid_argument("AlphaParams::tau_i must be > 0");
    if (!(sigma_x >= 0.0)) throw std::invalid_argument("AlphaParams::sigma_x must be >= 0");
    if (!(sigma_i >= 0.0)) throw std::invalid_argument("AlphaParams::sigma_i must be >= 0");
    if (!(rho >= -1.0 && rho <= 1.0)) throw std::invalid_argument("AlphaParams::rho must be in [-1, 1]");
    if (!(dt > 0.0)) throw std::invalid_argument("AlphaParams::dt must be > 0");
}

StochasticAlphaEngine::StochasticAlphaEngine(AlphaParams params, double x0, double i0)
    : params_(params), x_(x0), i_(i0) {
    params_.validate();

    theta_ = std::log(2.0) / params_.tau_x;
    kappa_ = std::log(2.0) / params_.tau_i;

    // Precompute Cholesky Factorization
    cholesky_[0][0] = 1.0;
    cholesky_[0][1] = 0.0;
    cholesky_[1][0] = params_.rho;
    cholesky_[1][1] = std::sqrt(1.0 - params_.rho * params_.rho);
}

double StochasticAlphaEngine::mu(double i) const noexcept {
    return params_.alpha_0 + params_.alpha_1 * i + params_.alpha_2 * (i * i);
}

StochasticAlphaEngine& StochasticAlphaEngine::step(double dw1, double dw2) noexcept {
    double dt = params_.dt;

    // Apply Cholesky transformation to get correlated Wiener increments (John's spec)
    // corr_dw1 = L[0][0] * dw1 + L[0][1] * dw2  -> (1.0 * dw1 + 0.0 * dw2)
    // corr_dw2 = L[1][0] * dw1 + L[1][1] * dw2  -> (rho * dw1 + sqrt(1-rho^2) * dw2)
    double corr_dw1 = cholesky_[0][0] * dw1 + cholesky_[0][1] * dw2;
    double corr_dw2 = cholesky_[1][0] * dw1 + cholesky_[1][1] * dw2;

    // 1. Update OFI (I_t) using correlated dw2
    double di = kappa_ * (params_.I_bar - i_) * dt + params_.sigma_i * std::sqrt(dt) * corr_dw2;
    i_ += di;

    // 2. Update Price Residual (X_t) using correlated dw1 and mean-reversion to mu(I)
    double current_mu = mu(i_);
    double dx = -theta_ * (x_ - current_mu) * dt + params_.sigma_x * std::sqrt(dt) * corr_dw1;
    x_ += dx;

    return *this;
}

double StochasticAlphaEngine::forecast_alpha(double horizon_h) const noexcept {
    double exp_theta = std::exp(-theta_ * horizon_h);
    double current_mu = mu(i_);
    return exp_theta * x_ + (1.0 - exp_theta) * current_mu;
}

} // namespace quantxcore::core


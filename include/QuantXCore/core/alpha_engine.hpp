//===----------------------------------------------------------------------===//
// Copyright (c) 2026 QuantXCore Team.
// SPDX-License-Identifier: Proprietary.
//===----------------------------------------------------------------------===//
#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantxcore::core {

struct AlphaParams {
    double tau_x;      
    double tau_i;      
    double I_bar;      
    double alpha_0;    
    double alpha_1;    
    double alpha_2;    
    double sigma_x;    
    double sigma_i;    
    double rho;        
    double dt;         

    void validate() const;
};

class StochasticAlphaEngine {
public:
    StochasticAlphaEngine(AlphaParams params, double x0, double i0);

    ~StochasticAlphaEngine() = default;
    StochasticAlphaEngine(const StochasticAlphaEngine&) = default;
    StochasticAlphaEngine(StochasticAlphaEngine&&) noexcept = default;
    StochasticAlphaEngine& operator=(const StochasticAlphaEngine&) = default;
    StochasticAlphaEngine& operator=(StochasticAlphaEngine&&) noexcept = default;

    StochasticAlphaEngine& step(double dw1, double dw2) noexcept;

    [[nodiscard]] double forecast_alpha(double horizon_h) const noexcept;

    [[nodiscard]] double x() const noexcept { return x_; }
    [[nodiscard]] double i() const noexcept { return i_; }

    [[nodiscard]] const AlphaParams& params() const noexcept { return params_; }

private:
    AlphaParams params_;
    double theta_;     
    double kappa_;     
    std::array<std::array<double, 2>, 2> cholesky_;
    double x_;  
    double i_;  

    [[nodiscard]] double mu(double i) const noexcept;
};

} // namespace quantxcore::core


#pragma once

// 1-euro filter (Casiez et al. 2012): heavy smoothing while the laser moves slowly (no jitter when aiming at one
// ship), little lag while it sweeps. Units are those of the signal (pixels here); dt in seconds.
#include <cmath>

namespace hw2vr {

class OneEuroFilter {
public:
    double min_cutoff = 1.0, beta = 0.007, derivative_cutoff = 1.0;

    double filter(double value, double dt) {
        if (!initialised_ || dt <= 0.0) {
            initialised_ = true;
            x_ = value;
            dx_ = 0.0;
            return value;
        }
        const double raw_dx = (value - x_) / dt;
        dx_ += alpha(derivative_cutoff, dt) * (raw_dx - dx_);
        x_ += alpha(min_cutoff + beta * std::fabs(dx_), dt) * (value - x_);
        return x_;
    }
    void reset() { initialised_ = false; }

private:
    static double alpha(double cutoff, double dt) {
        const double tau = 1.0 / (2.0 * 3.14159265358979 * cutoff);
        return 1.0 / (1.0 + tau / dt);
    }
    bool initialised_ = false;
    double x_ = 0.0, dx_ = 0.0;
};

}  // namespace hw2vr

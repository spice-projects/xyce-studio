#include "pchip_interpolation.h"

#include <stdexcept>

namespace fft
{
    std::vector<std::vector<double>> interpolate_pchip(std::span<const double> x_old, std::span<const std::span<const double>> y_old, std::span<const double> x_new) {
        // validate that x_old has at least 3 elements for boundary derivative computation
        if (x_old.size() < 3)
            throw std::invalid_argument("x_old must have at least 3 elements");
        // validate that x_new is not empty
        if (x_new.empty())
            throw std::invalid_argument("x_new must not be empty");
        // validate that all signals in y_old have matching size with x_old
        for (size_t s = 0; s < y_old.size(); ++s) {
            // compare sizes
            if (y_old[s].size() != x_old.size())
                throw std::invalid_argument("All signals in y_old must have the same size as x_old");
        }
        // number of points in the target grid and number of signals to interpolate
        const size_t num_targets = x_new.size();
        const size_t num_signals = y_old.size();
        const size_t n = x_old.size();
        // compute interval widths h_i = x_{i+1} - x_i once (shared across all signals)
        const size_t num_intervals = n - 1;
        auto h = std::vector<double>(num_intervals);
        for (size_t i = 0; i < num_intervals; ++i)
            h[i] = x_old[i + 1] - x_old[i];
        // preallocate indices and normalized coordinates for the scan
        auto indices = std::vector<size_t>(num_targets);
        auto t_vals = std::vector<double>(num_targets);
        // boundary values for clamping
        const double x_front = x_old.front();
        const double x_back = x_old.back();
        const size_t max_j = num_intervals - 1;
        // two-pointer scan over x_old and x_new to cache target intervals and t values
        size_t j = 0;
        for (size_t i = 0; i < num_targets; ++i) {
            // get the current target x value
            const double x_target = x_new[i];
            // handle boundary cases: clamp to the nearest interval
            if (x_target < x_front) {
                indices[i] = 0;
                t_vals[i] = 0.0;
            }
            else if (x_target > x_back) {
                indices[i] = max_j;
                t_vals[i] = 1.0;
            }
            else {
                // advance j to the right interval: x_old[j] <= x_target <= x_old[j + 1]
                while (x_target > x_old[j + 1])
                    ++j;
                // store the interval index and compute the normalized coordinate
                indices[i] = j;
                t_vals[i] = (x_target - x_old[j]) / h[j];
            }
        }
        // allocate results
        auto results = std::vector<std::vector<double>>(num_signals);
        for (size_t s = 0; s < num_signals; ++s)
            results[s] = std::vector<double>(num_targets);
        // perform PCHIP interpolation for each signal
        for (size_t s = 0; s < num_signals; ++s) {
            // get the current signal and its corresponding result vector
            const auto& y = y_old[s];
            auto& res = results[s];
            // compute local secant slopes δ_i = (y_{i+1} - y_i) / h_i
            auto delta = std::vector<double>(num_intervals);
            for (size_t i = 0; i < num_intervals; ++i)
                delta[i] = (y[i + 1] - y[i]) / h[i];
            // compute shape-preserving derivatives m_i at each node
            auto m = std::vector<double>(n);
            // left boundary derivative m_0
            m[0] = ((2.0 * h[0] + h[1]) * delta[0] - h[0] * delta[1]) / (h[0] + h[1]);
            if (m[0] * delta[0] < 0.0)
                m[0] = 0.0;
            if (delta[0] * delta[1] <= 0.0)
                m[0] = 3.0 * delta[0];
            // internal node derivatives m_i for 0 < i < n-1
            for (size_t i = 1; i <= max_j; ++i) {
                if (delta[i - 1] * delta[i] <= 0.0) {
                    m[i] = 0.0;
                }
                else {
                    const double w1 = 2.0 * h[i] + h[i - 1];
                    const double w2 = h[i] + 2.0 * h[i - 1];
                    m[i] = 3.0 * (h[i - 1] + h[i]) / (w1 / delta[i - 1] + w2 / delta[i]);
                }
            }
            // right boundary derivative m_{n-1}
            m[max_j + 1] = ((2.0 * h[max_j] + h[max_j - 1]) * delta[max_j] - h[max_j] * delta[max_j - 1]) / (h[max_j] + h[max_j - 1]);
            if (m[max_j + 1] * delta[max_j] < 0.0)
                m[max_j + 1] = 0.0;
            if (delta[max_j - 1] * delta[max_j] <= 0.0)
                m[max_j + 1] = 3.0 * delta[max_j];
            // evaluate the cubic hermite spline at each target point using cached indices and t values
            for (size_t i = 0; i < num_targets; ++i) {
                const size_t idx = indices[i];
                const double t = t_vals[i];
                // hermite basis functions
                const double tt = t * t;
                const double ttt = tt * t;
                const double h00 = 2.0 * ttt - 3.0 * tt + 1.0;
                const double h10 = ttt - 2.0 * tt + t;
                const double h01 = -2.0 * ttt + 3.0 * tt;
                const double h11 = ttt - tt;
                // evaluate the cubic hermite spline formula
                res[i] = h00 * y[idx] + h10 * h[idx] * m[idx] + h01 * y[idx + 1] + h11 * h[idx] * m[idx + 1];
            }
        }
        return results;
    }

    std::vector<double> interpolate_pchip(std::span<const double> x_old, std::span<const double> y_old, std::span<const double> x_new) {
        // wrap the single signal into a span of spans and call the multi-signal version
        const std::span<const double> y_spans[] = {y_old};
        // call the multi-signal version of PCHIP interpolation
        auto results = interpolate_pchip(x_old, std::span<const std::span<const double>>(y_spans), x_new);
        if (results.empty())
            return {};
        // return the first (and only) result vector for the single signal
        return std::move(results[0]);
    }
} // namespace fft

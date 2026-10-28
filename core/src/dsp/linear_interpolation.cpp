#include "linear_interpolation.h"

#include <stdexcept>

namespace fft
{
    std::vector<std::vector<double>> interpolate_linear(std::span<const double> x_old, std::span<const std::span<const double>> y_old, std::span<const double> x_new) {
        // validate that x_old has at least 2 elements
        if (x_old.size() < 2)
            throw std::invalid_argument("x_old must have at least 2 elements");
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
        // preallocate indices and weights for the scan
        auto indices = std::vector<size_t>(num_targets);
        auto weights = std::vector<double>(num_targets);
        // get the first and last values of x_old for boundary checks
        const double x_front = x_old.front();
        const double x_back = x_old.back();
        const size_t max_j = x_old.size() - 2;
        // initialize the pointer for the two-pointer scan
        size_t j = 0;
        // run the two-pointer scan once to compute indices and weights
        for (size_t i = 0; i < num_targets; ++i) {
            // get the current target x value
            const double x_target = x_new[i];
            // handle boundary cases: if x_target is outside the range of x_old, assign the nearest index and weight
            if (x_target < x_front) {
                indices[i] = 0;
                weights[i] = 0.0;
            }
            else if (x_target > x_back) {
                indices[i] = max_j;
                weights[i] = 1.0;
            }
            else {
                // advance j to the right interval: x_old[j] <= x_target <= x_old[j+1]
                while (x_target > x_old[j + 1]) {
                    ++j;
                }
                // store the index and compute the weight for linear interpolation
                indices[i] = j;
                const double dx = x_old[j + 1] - x_old[j];
                weights[i] = (x_target - x_old[j]) / dx;
            }
        }
        // allocate results
        auto results = std::vector<std::vector<double>>(num_signals);
        for (size_t s = 0; s < num_signals; ++s)
            results[s] = std::vector<double>(num_targets);
        // perform the interpolation for each signal
        for (size_t s = 0; s < num_signals; ++s) {
            // get the current signal and its corresponding result vector
            const auto& y = y_old[s];
            // reference to the result vector for the current signal
            auto& res = results[s];
            // loop signals
            for (size_t i = 0; i < num_targets; ++i) {
                // get the index and weight for the current target point
                const size_t idx = indices[i];
                const double t = weights[i];
                // linear interpolation formula: y_new = y[idx] + t * (y[idx + 1] - y[idx])
                res[i] = y[idx] + t * (y[idx + 1] - y[idx]);
            }
        }
        return results;
    }

    std::vector<double> interpolate_linear(std::span<const double> x_old, std::span<const double> y_old, std::span<const double> x_new) {
        // wrap the single signal into a span of spans and call the multi-signal version
        const std::span<const double> y_spans[] = {y_old};
        // call the multi-signal interpolation function
        auto results = interpolate_linear(x_old, std::span<const std::span<const double>>(y_spans), x_new);
        if (results.empty())
            return {};
        // return the first (and only) result
        return std::move(results[0]);
    }
} // namespace fft

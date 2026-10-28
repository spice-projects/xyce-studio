#include "cubic_spline_interpolation.h"

#include <stdexcept>

namespace fft
{
    std::vector<std::vector<double>> interpolate_cubic_spline(std::span<const double> x_old, std::span<const std::span<const double>> y_old, std::span<const double> x_new) {
        // validate that x_old has at least 2 elements
        if (x_old.size() < 2)
            throw std::invalid_argument("x_old must have at least 2 elements");
        // validate that x_new is not empty
        if (x_new.empty())
            throw std::invalid_argument("x_new must not be empty");
        // validate that all signals in y_old have matching size with x_old
        for (size_t s = 0; s < y_old.size(); ++s) {
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
            const double x_target = x_new[i];
            if (x_target < x_front) {
                indices[i] = 0;
                t_vals[i] = 0.0;
            }
            else if (x_target > x_back) {
                indices[i] = max_j;
                t_vals[i] = 1.0;
            }
            else {
                while (x_target > x_old[j + 1])
                    ++j;
                indices[i] = j;
                t_vals[i] = (x_target - x_old[j]) / h[j];
            }
        }
        // allocate results
        auto results = std::vector<std::vector<double>>(num_signals);
        for (size_t s = 0; s < num_signals; ++s)
            results[s] = std::vector<double>(num_targets);
        // natural cubic spline: M_0 = M_{n-1} = 0
        // the interior system has (n - 2) unknowns M_1 .. M_{n-2}
        const size_t N = (n > 2) ? (n - 2) : 0;
        // pre-compute Thomas forward-sweep coefficients (shared across all signals)
        // only needed when n > 3 (otherwise a single or no unknown)
        auto thomas_w = std::vector<double>();
        auto thomas_b_mod = std::vector<double>();
        if (N > 1) {
            thomas_w.resize(N - 1);
            thomas_b_mod.resize(N);
            // compute unmodified main diagonal b_i = 2 * (h_i + h_{i+1}) for i = 0 .. N-1
            for (size_t i = 0; i < N; ++i)
                thomas_b_mod[i] = 2.0 * (h[i] + h[i + 1]);
            // forward sweep: modify main diagonal and store w factors
            for (size_t i = 1; i < N; ++i) {
                // sub-diagonal a_i = h_i
                const double a_i = h[i];
                thomas_w[i - 1] = a_i / thomas_b_mod[i - 1];
                thomas_b_mod[i] -= thomas_w[i - 1] * h[i]; // c_{i-1} = h_i
            }
        }
        // perform cubic spline interpolation for each signal
        for (size_t s = 0; s < num_signals; ++s) {
            const auto& y = y_old[s];
            auto& res = results[s];
            // allocate second derivatives M_i for i = 0 .. n-1
            auto M = std::vector<double>(n, 0.0);
            if (N > 0) {
                // compute RHS_i = 6 * ((y[i+1]-y[i])/h[i] - (y[i]-y[i-1])/h[i-1]) for i = 1 .. n-2
                // mapped to interior index k = i-1 = 0 .. N-1
                // rhs_k = 6 * ((y[k+2] - y[k+1]) / h[k+1] - (y[k+1] - y[k]) / h[k])
                auto rhs = std::vector<double>(N);
                for (size_t k = 0; k < N; ++k)
                    rhs[k] = 6.0 * ((y[k + 2] - y[k + 1]) / h[k + 1] - (y[k + 1] - y[k]) / h[k]);
                if (N == 1) {
                    // single interior unknown: M_1 = RHS_0 / (2 * (h_0 + h_1))
                    M[1] = rhs[0] / (2.0 * (h[0] + h[1]));
                }
                else {
                    // forward sweep on RHS using pre-computed w and modified b
                    for (size_t i = 1; i < N; ++i)
                        rhs[i] -= thomas_w[i - 1] * rhs[i - 1];
                    // back substitution to solve for M_1 .. M_{n-2}
                    M[N] = rhs[N - 1] / thomas_b_mod[N - 1]; // M_{n-2} = rhs_{N-1} / b_mod_{N-1}
                    for (size_t i = N - 1; i > 0; --i)
                        M[i] = (rhs[i - 1] - h[i] * M[i + 1]) / thomas_b_mod[i - 1];
                    // M_1 = (rhs_0 - c_0 * M_2) / b_mod_0
                    // where c_0 = h_1, M_2 is M[2]
                    // but above loop starts from i = N-1 down to 1, and we need to assign M[i] for i = 1 .. N-2
                    // Actually M[i + 1] on the RHS corresponds to M_{i+1+1} = M_{i+2} in 1-based
                    // Let me re-derive...
                    // Interior unknown m_i = M_{i+1} for i = 0 .. N-1 (0-based interior index)
                    // Equation for m_i: h_i * m_{i-1} + 2*(h_i + h_{i+1}) * m_i + h_{i+1} * m_{i+1} = rhs_i
                    // Thomas: forward modifies rhs, then back-substitute
                    // m_{N-1} = rhs_{N-1} / b_mod_{N-1}
                    // m_i = (rhs_i - h_{i+1} * m_{i+1}) / b_mod_i
                    // so m_i -> M_{i+1} = (rhs_i - h_{i+1} * M_{i+2}) / b_mod_i
                    M[N] = rhs[N - 1] / thomas_b_mod[N - 1];
                    for (size_t i = N - 1; i > 0; --i)
                        M[i] = (rhs[i - 1] - h[i] * M[i + 1]) / thomas_b_mod[i - 1];
                }
            }
            // evaluate the natural cubic spline at each target point
            for (size_t i = 0; i < num_targets; ++i) {
                const size_t idx = indices[i];
                const double t = t_vals[i];
                const double tt = t * t;
                const double ttt = tt * t;
                const double A = 1.0 - t;
                const double AA = A * A;
                const double AAA = AA * A;
                const double h_sq = h[idx] * h[idx];
                res[i] = A * y[idx] + t * y[idx + 1] + (h_sq / 6.0) * ((AAA - A) * M[idx] + (ttt - t) * M[idx + 1]);
            }
        }
        return results;
    }

    std::vector<double> interpolate_cubic_spline(std::span<const double> x_old, std::span<const double> y_old, std::span<const double> x_new) {
        // wrap the single signal into a span of spans and call the multi-signal version
        const std::span<const double> y_spans[] = {y_old};
        auto results = interpolate_cubic_spline(x_old, std::span<const std::span<const double>>(y_spans), x_new);
        if (results.empty())
            return {};
        return std::move(results[0]);
    }
} // namespace fft

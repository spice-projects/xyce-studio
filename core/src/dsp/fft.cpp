#include "fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include "pocketfft_hdronly.h"

#include "cubic_spline_interpolation.h"
#include "linear_interpolation.h"
#include "pchip_interpolation.h"

namespace fft
{
    inline std::vector<std::vector<double>> interpolate(InterpolationAlgorithm algorithm, std::span<const double> x_old, std::span<const std::span<const double>> y_old, std::span<const double> x_new) {
        // choose the interpolation algorithm
        switch (algorithm) {
        case InterpolationAlgorithm::LINEAR:
            // execute piecewise linear interpolation
            return interpolate_linear(x_old, y_old, x_new);
        case InterpolationAlgorithm::PCHIP:
            // execute pchip interpolation
            return interpolate_pchip(x_old, y_old, x_new);
        case InterpolationAlgorithm::CUBIC_SPLINE:
            // execute natural cubic spline interpolation
            return interpolate_cubic_spline(x_old, y_old, x_new);
        default:
            // unknown algorithm fallback
            throw std::invalid_argument("Unknown InterpolationAlgorithm");
        };
    }

    FftResult compute_fft_many(std::span<const double> x, const std::vector<std::span<const double>>& y_matrix, size_t sample_count, WindowFunction window, FftFormat format, size_t x_left_index, std::optional<size_t> x_right_index, FftOutput output, bool keep_dc, InterpolationAlgorithm algo) {
        // validate that x has at least 2 elements
        if (x.size() < 2)
            throw std::invalid_argument("x must contain at least 2 elements");
        // validate that y_matrix is not empty
        if (y_matrix.empty())
            throw std::invalid_argument("y_matrix cannot be empty");
        // loop signals to validate sizes
        for (size_t s = 0; s < y_matrix.size(); ++s) {
            // compare size of each signal with x
            if (y_matrix[s].size() != x.size())
                throw std::invalid_argument("x and y_matrix row sizes must match");
        }
        // validate that sample_count meets the xyce minimum
        if (sample_count < 4)
            throw std::invalid_argument("sample_count must be at least 4");
        // validate that sample_count is a power of two
        if ((sample_count & (sample_count - 1)) != 0)
            throw std::invalid_argument("sample_count must be a power of two");
        // default the right interval index to the end of x
        const size_t left_index = x_left_index;
        const size_t right_index = x_right_index.value_or(x.size());
        // validate interval bounds
        if (left_index >= right_index || right_index > x.size())
            throw std::invalid_argument("invalid interval indices");
        // validate that selected interval has at least 2 samples
        if (right_index - left_index < 2)
            throw std::invalid_argument("selected interval must contain at least 2 samples");
        // validate that the selected interval is strictly increasing
        for (size_t i = left_index; i < right_index - 1; ++i) {
            // check adjacent values
            if (x[i + 1] <= x[i])
                throw std::invalid_argument("x values in selected interval must be strictly increasing");
        }
        // extract the selected interval range of x
        const std::span<const double> x_interval = x.subspan(left_index, right_index - left_index);
        // compute duration of the selected interval
        const double total_duration = x_interval.back() - x_interval.front();
        // compute the uniform time step over exactly sample_count points
        const double dt = total_duration / static_cast<double>(sample_count - 1);
        // validate that the time step is positive
        if (dt <= 0.0)
            throw std::invalid_argument("time step dt must be positive");
        // allocate the uniform grid
        auto x_uniform = std::vector<double>(sample_count);
        // compute uniform grid coordinates
        for (size_t i = 0; i < sample_count; ++i) {
            // linear grid interpolation
            x_uniform[i] = x_interval.front() + static_cast<double>(i) * dt;
        }
        // create a list of spans for the selected interval of signals
        auto y_old_spans = std::vector<std::span<const double>>();
        // reserve space in spans vector
        y_old_spans.reserve(y_matrix.size());
        // loop signals to slice interval
        for (const auto& row : y_matrix) {
            // slice individual signal row
            y_old_spans.push_back(row.subspan(left_index, right_index - left_index));
        }
        // interpolate the signals onto the uniform grid using the specified algorithm
        auto y_resampled = interpolate(algo, x_interval, y_old_spans, x_uniform);
        // remove dc component if keep_dc is false
        if (!keep_dc) {
            // loop each resampled signal row
            for (auto& row : y_resampled) {
                // accumulate the sum of all elements in the row
                double sum = 0.0;
                for (double val : row)
                    sum += val;
                // compute average/mean
                const double mean = sum / static_cast<double>(sample_count);
                // subtract mean from each element in the row
                for (double& val : row)
                    val -= mean;
            }
        }
        // generate window function weights
        auto win = std::vector<double>(sample_count, 1.0);
        // apply window calculations if not rectangular
        if (window != WindowFunction::RECTANGULAR) {
            // denominator for ratio calculations
            const double denom = static_cast<double>(sample_count - 1);
            // compute window weight for each sample
            for (size_t i = 0; i < sample_count; ++i) {
                // fraction index
                const double ratio = static_cast<double>(i) / denom;
                // handle specific window types
                if (window == WindowFunction::HAMMING) {
                    // hamming window coefficients
                    win[i] = 0.54 - 0.46 * std::cos(2.0 * std::numbers::pi * ratio);
                }
                else if (window == WindowFunction::HANNING) {
                    // hanning window coefficients
                    win[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * ratio);
                }
                else if (window == WindowFunction::BLACKMAN) {
                    // blackman window coefficients
                    win[i] = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * ratio) + 0.08 * std::cos(4.0 * std::numbers::pi * ratio);
                }
            }
        }
        // compute sum of window weights
        double win_sum = 0.0;
        for (double val : win)
            win_sum += val;
        // throw error on zero sum of weights
        if (win_sum == 0.0)
            throw std::runtime_error("sum of window weights is zero");
        // apply window weights to resampled signals
        for (auto& row : y_resampled) {
            // loop elements to apply windowing
            for (size_t i = 0; i < sample_count; ++i) {
                // multiply current element by window weight
                row[i] *= win[i];
            }
        }
        // transform length equals the requested sample count
        const size_t n_fft = sample_count;
        // compute frequency bins
        const size_t num_bins = n_fft / 2 + 1;
        auto frequencies = std::vector<double>(num_bins);
        // compute frequency value for each bin
        for (size_t i = 0; i < num_bins; ++i) {
            // frequency formula in hz
            frequencies[i] = static_cast<double>(i) / (static_cast<double>(n_fft) * dt);
        }
        // initialize pocketfft parameters
        auto shape_in = pocketfft::shape_t{n_fft};
        auto stride_in = pocketfft::stride_t{static_cast<ptrdiff_t>(sizeof(double))};
        auto stride_out = pocketfft::stride_t{static_cast<ptrdiff_t>(sizeof(std::complex<double>))};
        // allocate the output matrix
        auto output_values = std::vector<std::vector<double>>(y_resampled.size(), std::vector<double>(num_bins));
        // preallocate temporary complex spectrum buffer
        auto spectrum = std::vector<std::complex<double>>(num_bins);
        // loop signals to perform FFT and post-process
        for (size_t row_index = 0; row_index < y_resampled.size(); ++row_index) {
            // execute pocketfft real-to-complex transform
            pocketfft::r2c<double>(shape_in, stride_in, stride_out, 0, pocketfft::FORWARD, y_resampled[row_index].data(), spectrum.data(), 1.0);
            // check if output is phase
            if (output == FftOutput::PHASE) {
                // compute phase in degrees for each bin
                for (size_t i = 0; i < num_bins; ++i) {
                    // convert radians to degrees
                    output_values[row_index][i] = std::arg(spectrum[i]) * (180.0 / std::numbers::pi);
                }
            }
            else {
                // allocate magnitude array
                auto raw_magnitude = std::vector<double>(num_bins);
                // compute raw magnitudes
                for (size_t i = 0; i < num_bins; ++i) {
                    // absolute value of complex spectrum element
                    raw_magnitude[i] = std::abs(spectrum[i]);
                }
                // compute physical scaling factor for a one-sided spectrum
                const double scale = 2.0 / win_sum;
                // scale magnitudes
                for (double& val : raw_magnitude) {
                    val *= scale;
                }
                // halve the DC component which is not doubled
                raw_magnitude[0] /= 2.0;
                // halve the Nyquist component for even-length FFTs
                if (n_fft % 2 == 0)
                    raw_magnitude[num_bins - 1] /= 2.0;
                // normalize the complete one-sided spectrum by its peak for NORM
                if (format == FftFormat::NORM) {
                    // find the greatest magnitude
                    double peak = 0.0;
                    for (double val : raw_magnitude) {
                        peak = std::max(peak, val);
                    }
                    // avoid division by zero
                    if (peak > 0.0) {
                        // divide each bin by the peak
                        for (double& val : raw_magnitude) {
                            val /= peak;
                        }
                    }
                }
                // format output as DB or MAGNITUDE
                if (output == FftOutput::MAGNITUDE_DB) {
                    // compute decibels for each bin
                    for (size_t i = 0; i < num_bins; ++i) {
                        // clamp to avoid log of zero
                        const double clamped = std::max(raw_magnitude[i], 1e-300);
                        // convert to decibels
                        output_values[row_index][i] = 20.0 * std::log10(clamped);
                    }
                }
                else {
                    // store magnitude values directly
                    output_values[row_index] = std::move(raw_magnitude);
                }
            }
        }
        // return completed result
        return FftResult{.frequencies = std::move(frequencies), .values = std::move(output_values)};
    }
} // namespace fft

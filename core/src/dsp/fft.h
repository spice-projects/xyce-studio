#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace fft
{
    enum class WindowFunction
    {
        RECTANGULAR,
        HAMMING,
        HANNING,
        BLACKMAN
    };

    enum class FftFormat
    {
        NORM,
        UNORM
    };

    enum class FftOutput
    {
        MAGNITUDE,
        MAGNITUDE_DB,
        PHASE
    };

    enum class InterpolationAlgorithm
    {
        LINEAR,
        PCHIP,
        CUBIC_SPLINE
    };

    // configuration value type for an fft post-processing run, mirroring the Xyce .FFT surface
    struct FftParameters
    {
        // number of uniformly sampled fft points; both the interpolation grid size and the transform length
        size_t np{1024};
        // window function applied before the transform
        WindowFunction window{WindowFunction::HANNING};
        // output format; NORM divides the spectrum by its greatest magnitude, UNORM keeps physical magnitude
        FftFormat format{FftFormat::NORM};
        // start abscissa value of the selected interval
        double start{0.0};
        // stop abscissa value of the selected interval
        double stop{0.0};
        // output presentation chosen by the user
        FftOutput output{FftOutput::MAGNITUDE};
        // when false, per-signal mean is removed before the transform
        bool keep_dc{true};
    };

    struct FftResult
    {
        std::vector<double> frequencies;
        std::vector<std::vector<double>> values;
    };

    // computes fft for many real-valued signals that share the same time-domain abscissa;
    // sample_count must be a power of two and at least 4, and is both the uniform grid size and the transform length
    FftResult compute_fft_many(std::span<const double> x, const std::vector<std::span<const double>>& y_matrix, size_t sample_count, WindowFunction window = WindowFunction::RECTANGULAR, FftFormat format = FftFormat::NORM, size_t x_left_index = 0, std::optional<size_t> x_right_index = std::nullopt, FftOutput output = FftOutput::MAGNITUDE, bool keep_dc = true, InterpolationAlgorithm algo = InterpolationAlgorithm::LINEAR);
} // namespace fft

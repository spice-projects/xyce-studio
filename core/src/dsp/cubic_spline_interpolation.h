#pragma once

#include <span>
#include <vector>

namespace fft
{
    /**
     * @brief Interpolates multiple signals from a non-uniform grid to a uniform grid using Natural Cubic Spline interpolation.
     * @param x_old Non-uniform, strictly increasing abscissa values (e.g. time steps from SPICE).
     * @param y_old Vector of spans containing signal values, each matching x_old size.
     * @param x_new Target uniform grid abscissa values.
     * @return std::vector<std::vector<double>> Resampled signal values for each input signal.
     * @throws std::invalid_argument if inputs are invalid or dimensions do not match.
     */
    std::vector<std::vector<double>> interpolate_cubic_spline(std::span<const double> x_old, std::span<const std::span<const double>> y_old, std::span<const double> x_new);

    /**
     * @brief Interpolates a single signal from a non-uniform grid to a uniform grid using Natural Cubic Spline interpolation.
     * @param x_old Non-uniform, strictly increasing abscissa values (e.g. time steps from SPICE).
     * @param y_old Signal values corresponding to x_old.
     * @param x_new Target uniform grid abscissa values.
     * @return std::vector<double> Resampled signal values.
     * @throws std::invalid_argument if inputs are invalid or dimensions do not match.
     */
    std::vector<double> interpolate_cubic_spline(std::span<const double> x_old, std::span<const double> y_old, std::span<const double> x_new);
} // namespace fft

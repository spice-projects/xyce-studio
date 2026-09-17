#include <cmath>
#include <limits>
#include <numbers>

#include "smith_chart.h"

namespace smith
{
    std::complex<double> gamma_from_impedance(const std::complex<double>& z) {
        // gamma = (z - 1) / (z + 1); at the open circuit pole z -> -1 the
        // reflection grows without bound, mapped to infinity
        return (z - 1.0) / (z + 1.0);
    }

    std::complex<double> impedance_from_gamma(const std::complex<double>& gamma) {
        // inverse mapping z = (1 + gamma) / (1 - gamma); at the short circuit
        // pole gamma -> 1 the impedance grows without bound, mapped to infinity
        return (1.0 + gamma) / (1.0 - gamma);
    }

    std::complex<double> gamma_from_admittance(const std::complex<double>& y) {
        // gamma = (1 - y) / (1 + y), the mirrored impedance mapping
        return (1.0 - y) / (1.0 + y);
    }

    std::complex<double> admittance_from_gamma(const std::complex<double>& gamma) {
        // inverse mapping y = (1 - gamma) / (1 + gamma)
        return (1.0 - gamma) / (1.0 + gamma);
    }

    std::complex<double> reflection_coefficient(const std::string& parameter_type, const std::complex<double>& value, const double reference_impedance) {
        // guard against a degenerate reference impedance
        const double z0 = reference_impedance != 0.0 ? reference_impedance : 50.0;
        // s-parameters are reflection coefficients already
        if (parameter_type == "S")
            return value;
        // z-parameters normalize through z = Zii / Z0
        if (parameter_type == "Z")
            return gamma_from_impedance(value / z0);
        // y-parameters (and anything else) normalize through y = Yii * Z0
        return gamma_from_admittance(value * z0);
    }

    double vswr(const std::complex<double>& gamma) {
        // magnitude of the reflection coefficient
        const double magnitude = std::abs(gamma);
        // no finite vswr at or beyond the unit circle boundary
        if (magnitude >= 1.0)
            return std::numeric_limits<double>::infinity();
        // classic (1 + |gamma|) / (1 - |gamma|)
        return (1.0 + magnitude) / (1.0 - magnitude);
    }

    double return_loss_db(const std::complex<double>& gamma) {
        // magnitude of the reflection coefficient
        const double magnitude = std::abs(gamma);
        // no finite return loss at a perfect reflection
        if (magnitude == 0.0)
            return std::numeric_limits<double>::infinity();
        // -20 log10 |gamma|
        return -20.0 * std::log10(magnitude);
    }

    std::vector<GridPath> resistance_grid_paths(const std::vector<double>& r_values, const int samples_per_path) {
        // grid paths under construction
        std::vector<GridPath> paths;
        // reserve one path per requested resistance
        paths.reserve(r_values.size());
        // loop requested resistances
        for (const double r : r_values) {
            // constant resistance circle: center r/(1+r) on the real axis,
            // radius 1/(1+r); sampled as a full closed circle
            const double center = r / (1.0 + r);
            const double radius = 1.0 / (1.0 + r);
            // path for this resistance
            GridPath path;
            // reserve one extra sample so the closing segment is explicit
            path.reserve(static_cast<size_t>(samples_per_path) + 1);
            // loop samples around the circle
            for (int i = 0; i <= samples_per_path; ++i) {
                // angle around the circle
                const double angle = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(samples_per_path);
                // sample on the circle
                path.emplace_back(center + std::cos(angle) * radius, std::sin(angle) * radius);
            }
            // append the path
            paths.push_back(std::move(path));
        }
        // exit
        return paths;
    }

    std::vector<GridPath> reactance_grid_paths(const std::vector<double>& x_values, const int samples_per_path) {
        // grid paths under construction
        std::vector<GridPath> paths;
        // reserve one path per requested reactance
        paths.reserve(x_values.size());
        // loop requested reactances
        for (const double x : x_values) {
            // constant reactance arc: a circle through the open circuit point
            // (1, 0) centered at (1, 1/x) with radius |1/x|; the visible arc
            // runs from the unit circle boundary (z = jx at r = 0) to the open
            // circuit point (r -> inf), clipped by construction
            GridPath path;
            // reserve the sample count
            path.reserve(static_cast<size_t>(samples_per_path));
            // circle center and radius
            const std::complex<double> center(1.0, 1.0 / x);
            const double radius = std::abs(1.0 / x);
            // start angle: the boundary point z = jx sits on the unit circle
            const std::complex<double> boundary = gamma_from_impedance(std::complex<double>(0.0, x));
            const double start_angle = std::atan2(boundary.imag() - center.imag(), boundary.real() - center.real());
            // end angle: the open circuit point (1, 0) relative to the center
            const double end_angle = std::atan2(0.0 - center.imag(), 1.0 - center.real());
            // the visible arc sweeps toward the open circuit through the
            // inside of the unit circle: counterclockwise for positive
            // reactance, clockwise for negative reactance
            double sweep_end = end_angle;
            if (x > 0.0) {
                while (sweep_end < start_angle)
                    sweep_end += 2.0 * std::numbers::pi;
            }
            else {
                while (sweep_end > start_angle)
                    sweep_end -= 2.0 * std::numbers::pi;
            }
            // loop samples along the arc, uniform in angle so the curve
            // renders smooth at any chart size
            for (int i = 0; i < samples_per_path; ++i) {
                // angle of this sample
                const double angle = start_angle + (sweep_end - start_angle) * static_cast<double>(i) / static_cast<double>(samples_per_path - 1);
                // sample on the arc
                path.push_back(center + std::complex<double>(std::cos(angle) * radius, std::sin(angle) * radius));
            }
            // append the path
            paths.push_back(std::move(path));
        }
        // exit
        return paths;
    }

    std::vector<GridPath> grid_paths(const int samples_per_path) {
        // grid paths under construction
        std::vector<GridPath> paths;
        // standard grid levels per the issue spec
        const std::vector<double> levels = {0.2, 0.5, 1.0, 2.0, 5.0};
        // constant resistance circles; r = 0 renders the unit circle boundary
        // |gamma| = 1 that frames the chart
        auto resistance = resistance_grid_paths({0.0, 0.2, 0.5, 1.0, 2.0, 5.0}, samples_per_path);
        // append them
        paths.insert(paths.end(), std::make_move_iterator(resistance.begin()), std::make_move_iterator(resistance.end()));
        // mirrored reactance arcs, positive and negative sides
        auto reactance = reactance_grid_paths({-5.0, -2.0, -1.0, -0.5, -0.2, 0.2, 0.5, 1.0, 2.0, 5.0}, samples_per_path);
        // append them
        paths.insert(paths.end(), std::make_move_iterator(reactance.begin()), std::make_move_iterator(reactance.end()));
        // exit
        return paths;
    }
} // namespace smith

#pragma once

#include <complex>
#include <string>
#include <vector>

// smith chart math: reflection-coefficient plane conversions and grid path
// generation shared by the smith chart engine and the chart layout
namespace smith
{
    // reflection coefficient of the normalized impedance z = Z/Z0
    std::complex<double> gamma_from_impedance(const std::complex<double>& z);

    // normalized impedance z = Z/Z0 at the reflection coefficient
    std::complex<double> impedance_from_gamma(const std::complex<double>& gamma);

    // reflection coefficient of the normalized admittance y = Y*Z0
    std::complex<double> gamma_from_admittance(const std::complex<double>& y);

    // normalized admittance y = Y*Z0 at the reflection coefficient
    std::complex<double> admittance_from_gamma(const std::complex<double>& gamma);

    // reflection coefficient of an s/y/z parameter value against the given
    // reference impedance; s-parameters are reflection coefficients already,
    // z-parameters normalize through z = Zii/Z0 and y-parameters through
    // y = Yii*Z0
    std::complex<double> reflection_coefficient(const std::string& parameter_type, const std::complex<double>& value, double reference_impedance);

    // voltage standing wave ratio of the reflection coefficient,
    // (1+|gamma|)/(1-|gamma|); infinity at |gamma| >= 1
    double vswr(const std::complex<double>& gamma);

    // return loss in dB, -20 log10 |gamma|; infinity at a zero reflection
    double return_loss_db(const std::complex<double>& gamma);

    // one grid polyline in gamma-plane coordinates
    using GridPath = std::vector<std::complex<double>>;

    // constant normalized resistance circles sampled inside the unit circle
    std::vector<GridPath> resistance_grid_paths(const std::vector<double>& r_values, int samples_per_path);

    // constant normalized reactance arcs sampled inside the unit circle
    std::vector<GridPath> reactance_grid_paths(const std::vector<double>& x_values, int samples_per_path);

    // the standard smith grid: the unit circle boundary, constant resistance
    // circles and constant reactance arcs at 0.2, 0.5, 1, 2, 5
    std::vector<GridPath> grid_paths(int samples_per_path);
} // namespace smith

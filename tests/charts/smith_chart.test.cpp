#include <cmath>
#include <complex>
#include <limits>

#include <gtest/gtest.h>

#include "charts/smith_chart.h"

// ========================================================================================
// impedance to reflection coefficient conversions
// ========================================================================================

TEST(SmithChartMathChecks, z_load_matches_reference_impedance_maps_to_zero_gamma) {
    // arrange — a 50 ohm load against a 50 ohm reference
    const auto gamma = smith::gamma_from_impedance(1.0);
    // assert
    EXPECT_NEAR(gamma.real(), 0.0, 1e-12);
    EXPECT_NEAR(gamma.imag(), 0.0, 1e-12);
}

TEST(SmithChartMathChecks, short_circuit_maps_to_minus_one) {
    // arrange — a short load z = 0
    const auto gamma = smith::gamma_from_impedance(0.0);
    // assert
    EXPECT_NEAR(gamma.real(), -1.0, 1e-12);
    EXPECT_NEAR(gamma.imag(), 0.0, 1e-12);
}

TEST(SmithChartMathChecks, open_circuit_maps_to_plus_one) {
    // arrange — an open load z = 1e12 (normalized)
    const auto gamma = smith::gamma_from_impedance(1e12);
    // assert — approaches the open circuit point (1, 0)
    EXPECT_NEAR(gamma.real(), 1.0, 1e-9);
    EXPECT_NEAR(gamma.imag(), 0.0, 1e-12);
}

TEST(SmithChartMathChecks, mismatched_load_maps_to_positive_real_gamma) {
    // arrange — a 100 ohm load against a 50 ohm reference, z = 2
    const auto gamma = smith::gamma_from_impedance(2.0);
    // assert — gamma = (2 - 1) / (2 + 1) = 1/3
    EXPECT_NEAR(gamma.real(), 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(gamma.imag(), 0.0, 1e-12);
}

TEST(SmithChartMathChecks, reactive_load_maps_off_the_real_axis) {
    // arrange — z = 1 + j1
    const auto gamma = smith::gamma_from_impedance(std::complex<double>(1.0, 1.0));
    // assert — gamma = (j) / (2 + j) = (j(2 - j)) / 5 = (1 + 2j) / 5
    EXPECT_NEAR(gamma.real(), 0.2, 1e-12);
    EXPECT_NEAR(gamma.imag(), 0.4, 1e-12);
}

TEST(SmithChartMathChecks, impedance_round_trips_through_gamma) {
    // arrange — a set of normalized impedances covering the plane
    const std::vector<std::complex<double>> impedances = {
        {0.0, 0.0}, {0.5, -0.3}, {1.0, 0.0}, {2.0, 1.5}, {5.0, 10.0}, {100.0, 0.0},
    };
    // act / assert — z -> gamma -> z reproduces the impedance
    for (const auto& z : impedances) {
        const auto round_trip = smith::impedance_from_gamma(smith::gamma_from_impedance(z));
        EXPECT_NEAR(round_trip.real(), z.real(), 1e-9 * std::max(1.0, std::abs(z)));
        EXPECT_NEAR(round_trip.imag(), z.imag(), 1e-9 * std::max(1.0, std::abs(z)));
    }
}

TEST(SmithChartMathChecks, admittance_round_trips_through_gamma) {
    // arrange — a set of normalized admittances
    const std::vector<std::complex<double>> admittances = {
        {0.0, 0.0}, {0.5, -0.3}, {1.0, 0.0}, {2.0, 1.5}, {5.0, 10.0},
    };
    // act / assert — y -> gamma -> y reproduces the admittance
    for (const auto& y : admittances) {
        const auto round_trip = smith::admittance_from_gamma(smith::gamma_from_admittance(y));
        EXPECT_NEAR(round_trip.real(), y.real(), 1e-12);
        EXPECT_NEAR(round_trip.imag(), y.imag(), 1e-12);
    }
}

TEST(SmithChartMathChecks, matched_admittance_maps_to_zero_gamma) {
    // arrange — y = 1 (the matched admittance)
    const auto gamma = smith::gamma_from_admittance(1.0);
    // assert
    EXPECT_NEAR(gamma.real(), 0.0, 1e-12);
    EXPECT_NEAR(gamma.imag(), 0.0, 1e-12);
}

// ========================================================================================
// parameter type conversions
// ========================================================================================

TEST(SmithChartMathChecks, reflection_coefficient_passes_s_parameters_through) {
    // arrange — an s-parameter value and its reference impedance
    const std::complex<double> s(0.5, 0.25);
    // act
    const auto gamma = smith::reflection_coefficient("S", s, 50.0);
    // assert — s-parameters are already reflection coefficients
    EXPECT_DOUBLE_EQ(gamma.real(), s.real());
    EXPECT_DOUBLE_EQ(gamma.imag(), s.imag());
}

TEST(SmithChartMathChecks, reflection_coefficient_converts_z_parameters) {
    // arrange — a 100 ohm impedance against a 50 ohm reference
    const auto gamma = smith::reflection_coefficient("Z", std::complex<double>(100.0, 0.0), 50.0);
    // assert — z = 2 maps to gamma = 1/3
    EXPECT_NEAR(gamma.real(), 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(gamma.imag(), 0.0, 1e-12);
}

TEST(SmithChartMathChecks, reflection_coefficient_converts_y_parameters) {
    // arrange — a 0.01 S admittance against a 50 ohm reference (y = 0.5)
    const auto gamma = smith::reflection_coefficient("Y", std::complex<double>(0.01, 0.0), 50.0);
    // assert — gamma = (1 - y) / (1 + y) = 0.5 / 1.5 = 1/3
    EXPECT_NEAR(gamma.real(), 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(gamma.imag(), 0.0, 1e-12);
}

// ========================================================================================
// vswr and return loss
// ========================================================================================

TEST(SmithChartMathChecks, vswr_of_matched_load_is_one) {
    // arrange — a matched load
    // act / assert
    EXPECT_DOUBLE_EQ(smith::vswr(std::complex<double>(0.0, 0.0)), 1.0);
}

TEST(SmithChartMathChecks, vswr_of_mismatched_load) {
    // arrange — gamma magnitude 1/3
    const auto gamma = std::complex<double>(1.0 / 3.0, 0.0);
    // act / assert — (1 + 1/3) / (1 - 1/3) = 2
    EXPECT_NEAR(smith::vswr(gamma), 2.0, 1e-12);
}

TEST(SmithChartMathChecks, vswr_is_infinite_at_the_unit_circle) {
    // arrange — gamma on the unit circle
    const auto gamma = std::complex<double>(1.0, 0.0);
    // act / assert
    EXPECT_TRUE(std::isinf(smith::vswr(gamma)));
}

TEST(SmithChartMathChecks, vswr_is_infinite_beyond_the_unit_circle) {
    // arrange — a negative resistance load
    const auto gamma = std::complex<double>(1.5, 0.0);
    // act / assert
    EXPECT_TRUE(std::isinf(smith::vswr(gamma)));
}

TEST(SmithChartMathChecks, return_loss_of_matched_load_is_infinite) {
    // arrange — a matched load reflects nothing
    // act / assert
    EXPECT_TRUE(std::isinf(smith::return_loss_db(std::complex<double>(0.0, 0.0))));
}

TEST(SmithChartMathChecks, return_loss_of_half_magnitude_load) {
    // arrange — |gamma| = 0.5 reflects half the power
    const auto gamma = std::complex<double>(0.5, 0.0);
    // act / assert — -20 log10(0.5) = 6.02 dB
    EXPECT_NEAR(smith::return_loss_db(gamma), -20.0 * std::log10(0.5), 1e-12);
}

// ========================================================================================
// grid path generation
// ========================================================================================

TEST(SmithChartGridChecks, resistance_circles_stay_inside_the_unit_circle) {
    // arrange — the standard resistance levels with a dense sampling
    // act
    const auto paths = smith::resistance_grid_paths({0.2, 0.5, 1.0, 2.0, 5.0}, 128);
    // assert — every sample sits inside the unit circle (r >= 0)
    for (const auto& path : paths) {
        for (const auto& gamma : path) {
            EXPECT_LE(std::abs(gamma), 1.0 + 1e-12);
        }
    }
}

TEST(SmithChartMathChecks, resistance_circles_are_closed_and_constant_r) {
    // arrange — one circle at r = 2
    // act
    const auto paths = smith::resistance_grid_paths({2.0}, 64);
    // assert — the first and last samples coincide (closed circle)
    const auto& circle = paths[0];
    ASSERT_EQ(circle.size(), 65u);
    EXPECT_NEAR(circle.front().real(), circle.back().real(), 1e-12);
    EXPECT_NEAR(circle.front().imag(), circle.back().imag(), 1e-12);
    // every sample maps back to the constant resistance plane Re(z) = r
    // through the inverse transform; samples at the shared open-circuit point
    // (gamma = 1, where all resistance circles meet) are skipped as
    // numerically singular
    for (const auto& gamma : circle) {
        if (std::abs(gamma) > 0.99)
            continue;
        const auto z = smith::impedance_from_gamma(gamma);
        EXPECT_NEAR(z.real(), 2.0, 1e-7);
    }
}

TEST(SmithChartMathChecks, reactance_arcs_stay_inside_the_unit_circle) {
    // arrange — the standard reactance levels
    // act
    const auto paths = smith::reactance_grid_paths({-5.0, -2.0, -1.0, -0.5, -0.2, 0.2, 0.5, 1.0, 2.0, 5.0}, 128);
    // assert — every sample sits inside the unit circle
    for (const auto& path : paths) {
        for (const auto& gamma : path) {
            EXPECT_LE(std::abs(gamma), 1.0 + 1e-9);
        }
    }
}

TEST(SmithChartMathChecks, reactance_arcs_run_from_the_unit_circle_to_the_open_circuit) {
    // arrange — one arc at x = 1
    // act
    const auto paths = smith::reactance_grid_paths({1.0}, 128);
    const auto& arc = paths[0];
    // assert — the first sample sits on the unit circle, the last at the open
    // circuit point (1, 0)
    EXPECT_NEAR(std::abs(arc.front()), 1.0, 1e-8);
    EXPECT_NEAR(arc.back().real(), 1.0, 1e-8);
    EXPECT_NEAR(arc.back().imag(), 0.0, 1e-12);
    // every sample satisfies the constant reactance relation; samples at the
    // shared open circuit point (gamma = 1) are singular, skip them
    for (const auto& gamma : arc) {
        if (std::abs(gamma) > 0.99)
            continue;
        const auto z = smith::impedance_from_gamma(gamma);
        EXPECT_NEAR(z.imag(), 1.0, 1e-6 * std::max(1.0, std::abs(z)));
        EXPECT_GE(z.real(), 0.0);
    }
}

TEST(SmithChartMathChecks, reactance_arcs_sample_uniformly_along_the_arc) {
    // arrange — one arc at x = 1 with a dense sampling
    // act
    const auto paths = smith::reactance_grid_paths({1.0}, 128);
    const auto& arc = paths[0];
    // assert — uniform angle sampling makes every chord the same length, so
    // the polyline renders smooth without visible segments
    double first_chord = 0.0;
    for (size_t i = 1; i < arc.size(); ++i) {
        const double chord = std::abs(arc[i] - arc[i - 1]);
        if (i == 1)
            first_chord = chord;
        else
            EXPECT_NEAR(chord, first_chord, first_chord * 0.05);
    }
}

TEST(SmithChartMathChecks, grid_paths_covers_the_unit_circle_all_levels) {
    // arrange
    // act
    const auto paths = smith::grid_paths(64);
    // assert — 6 resistance circles (r = 0 included) plus 10 reactance arcs
    ASSERT_EQ(paths.size(), 16u);
    // the first path is the unit circle boundary
    EXPECT_NEAR(paths[0].front().real(), 1.0, 1e-12);
    EXPECT_NEAR(paths[0].front().imag(), 0.0, 1e-12);
    // every sample of every path stays inside the unit circle
    for (const auto& path : paths) {
        for (const auto& gamma : path) {
            EXPECT_LE(std::abs(gamma), 1.0 + 1e-9);
        }
    }
}

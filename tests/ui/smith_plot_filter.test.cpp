#include <complex>

#include <gtest/gtest.h>

#include "expression/expression.h"
#include "ui/smith_plot_filter.h"

TEST(SmithPlotFilterChecks, diagonal_s_parameter_entries_are_allowed) {
    // arrange — the diagonal entries of an s-parameter file
    // act / assert
    EXPECT_TRUE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("S11", std::vector<View<std::complex<double>>>{}, "", ""))));
    EXPECT_TRUE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("S22", std::vector<View<std::complex<double>>>{}, "", ""))));
}

TEST(SmithPlotFilterChecks, diagonal_y_and_z_entries_are_allowed) {
    // arrange — the diagonal entries of y and z parameter files
    // act / assert
    EXPECT_TRUE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("Y11", std::vector<View<std::complex<double>>>{}, "", ""))));
    EXPECT_TRUE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("Z33", std::vector<View<std::complex<double>>>{}, "", ""))));
}

TEST(SmithPlotFilterChecks, off_diagonal_entries_are_rejected) {
    // arrange — the transmission entries of a two-port file
    // act / assert
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("S12", std::vector<View<std::complex<double>>>{}, "", ""))));
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("S21", std::vector<View<std::complex<double>>>{}, "", ""))));
}

TEST(SmithPlotFilterChecks, real_expressions_are_rejected) {
    // arrange — a real expression cannot carry a gamma-plane value
    // act / assert
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<double>("frequency", std::vector<View<double>>{}, "", "Hz"))));
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<double>("db(S11)", std::vector<View<double>>{}, "", "dB"))));
}

TEST(SmithPlotFilterChecks, non_parameter_names_are_rejected) {
    // arrange — names that are not parameter entries
    // act / assert
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("V(1)", std::vector<View<std::complex<double>>>{}, "", ""))));
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("I(R1)", std::vector<View<std::complex<double>>>{}, "", ""))));
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("db(S11)", std::vector<View<std::complex<double>>>{}, "", ""))));
}

TEST(SmithPlotFilterChecks, malformed_parameter_names_are_rejected) {
    // arrange — names with a parameter prefix but malformed indices
    // act / assert — odd digit counts and mismatched halves are not diagonal
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("S1", std::vector<View<std::complex<double>>>{}, "", ""))));
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("S123", std::vector<View<std::complex<double>>>{}, "", ""))));
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("S12 3", std::vector<View<std::complex<double>>>{}, "", ""))));
}

TEST(SmithPlotFilterChecks, multi_digit_indices_must_repeat_as_wholes) {
    // arrange — a ten-port file names its entries s11..s1010; the diagonal
    // entry s1010 carries two identical two-digit indices
    // act / assert
    EXPECT_TRUE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("S1010", std::vector<View<std::complex<double>>>{}, "", ""))));
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("S1101", std::vector<View<std::complex<double>>>{}, "", ""))));
}

TEST(SmithPlotFilterChecks, non_numeric_digits_are_rejected) {
    // arrange — names with letters in the index positions
    // act / assert
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("Sxyxy", std::vector<View<std::complex<double>>>{}, "", ""))));
    EXPECT_FALSE(is_smith_plot_expression(AnyExpression(Expression<std::complex<double>>("Sx1x1", std::vector<View<std::complex<double>>>{}, "", ""))));
}

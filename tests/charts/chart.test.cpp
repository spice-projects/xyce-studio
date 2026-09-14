#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "charts/chart_engine.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"

TEST(ChartRatioTest, linear_ratio_interpolates_linearly) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 2.0, 4.0, 6.0, 8.0, 10.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 10.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // act
    const double value = chart.ratio_to_abscissa_value(0.5);
    // assert
    ASSERT_NEAR(value, 5.0, 1e-9);
}

TEST(ChartRatioTest, decade_ratio_interpolates_geometrically) {
    // arrange
    std::vector<double> abscissa_data = {1.0, 10.0, 100.0, 1000.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("sweep", std::move(abscissa_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"sweep"}, {{}}, {{1.0, 1000.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::DECADE, 1000);
    // act
    const double value = chart.ratio_to_abscissa_value(0.5);
    // assert
    ASSERT_NEAR(value, std::sqrt(1000.0), 1e-9);
}

TEST(ChartRatioTest, decade_ratio_endpoints_match_range) {
    // arrange
    std::vector<double> abscissa_data = {1.0, 10.0, 100.0, 1000.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("sweep", std::move(abscissa_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"sweep"}, {{}}, {{1.0, 1000.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::DECADE, 1000);
    // act
    const double left_value = chart.ratio_to_abscissa_value(0.0);
    const double right_value = chart.ratio_to_abscissa_value(1.0);
    // assert
    ASSERT_NEAR(left_value, 1.0, 1e-9);
    ASSERT_NEAR(right_value, 1000.0, 1e-9);
}

TEST(ChartRatioTest, decade_equal_ratios_produce_equal_log_ratios) {
    // arrange
    std::vector<double> abscissa_data = {1.0, 10.0, 100.0, 1000.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("sweep", std::move(abscissa_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"sweep"}, {{}}, {{1.0, 1000.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::DECADE, 1000);
    // act
    const double first = chart.ratio_to_abscissa_value(0.25) / chart.ratio_to_abscissa_value(0.0);
    const double second = chart.ratio_to_abscissa_value(0.5) / chart.ratio_to_abscissa_value(0.25);
    // assert
    ASSERT_NEAR(first, second, 1e-9);
}

TEST(ChartRatioTest, octave_ratio_interpolates_geometrically) {
    // arrange
    std::vector<double> abscissa_data = {1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 9}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("sweep", std::move(abscissa_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"sweep"}, {{}}, {{1.0, 256.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::OCTAVE, 1000);
    // act
    const double value = chart.ratio_to_abscissa_value(0.5);
    // assert
    ASSERT_NEAR(value, 16.0, 1e-9);
}

TEST(ChartRatioTest, logarithmic_ratio_with_non_positive_range_clamps_into_the_log_domain) {
    // arrange
    std::vector<double> abscissa_data = {-1.0, 0.0, 1.0, 10.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("sweep", std::move(abscissa_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"sweep"}, {{}}, {{-1.0, 10.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::DECADE, 1000);
    // act
    const double value = chart.ratio_to_abscissa_value(0.5);
    // assert — the non-positive bound is clamped to a fraction of the positive
    // bound like the layout, so interaction mapping matches the drawn plot
    ASSERT_TRUE(std::isfinite(value));
    ASSERT_NEAR(value, 1e-5 * std::pow(10.0 / 1e-5, 0.5), 1e-9);
}

TEST(ChartRatioTest, decade_plot_ratio_interpolates_geometrically_over_visible_range) {
    // arrange
    std::vector<double> abscissa_data = {1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("sweep", std::move(abscissa_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"sweep"}, {{}}, {{1.0, 100000.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::DECADE, 1000);
    // set the visible abscissa range as when a chart is first created
    chart.plot_series({});
    // act
    const double value_at_10 = chart.plot_ratio_to_abscissa_value(0.2);
    const double value_at_100 = chart.plot_ratio_to_abscissa_value(0.4);
    // assert
    ASSERT_NEAR(value_at_10, 10.0, 1e-9);
    ASSERT_NEAR(value_at_100, 100.0, 1e-9);
}

TEST(ChartRatioTest, linear_plot_ratio_interpolates_linearly_over_visible_range) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 2.0, 4.0, 6.0, 8.0, 10.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 10.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // set the visible abscissa range as when a chart is first created
    chart.plot_series({});
    // act
    const double value = chart.plot_ratio_to_abscissa_value(0.5);
    // assert
    ASSERT_NEAR(value, 5.0, 1e-9);
}

TEST(ChartRatioTest, hovered_series_text_avoids_scientific_prefix_overflow) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    // series is 1 mA, slightly below the milli divider so the mantissa would overflow to 1e+03uA
    std::vector<double> current_data = {0.0009999, 0.0009999, 0.0009999, 0.0009999};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("I(R1)", std::move(current_data), step_slices, "A"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // plot the current series
    chart.plot_series({expression_manager.expressions()[1]});
    // act
    const std::string text = chart.hovered_series_text(1.5);
    // assert
    EXPECT_NE(text.find("I(R1)=1 mA"), std::string::npos);
    EXPECT_EQ(text.find("1e+03"), std::string::npos);
}

TEST(ChartRatioTest, hovered_series_text_after_zoom) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> current_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(current_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    chart.plot_series({expression_manager.expressions()[1]});
    // act
    // zoom from 1.2s to 1.8s (ratios 0.24 to 0.36)
    chart.update_zoom_window(0.24, 0.36, 0.0, 1.0);
    const double hovered_x = chart.plot_ratio_to_abscissa_value(0.5);
    const std::string text = chart.hovered_series_text(hovered_x);
    // assert
    EXPECT_NE(text.find("V(out)="), std::string::npos);
}

TEST(ChartFormatTest, metric_places_space_before_unit) {
    // si format: value + space + prefix + unit
    EXPECT_EQ(ChartEngine::format_metric(0.001, "A"), "1 mA");
    EXPECT_EQ(ChartEngine::format_metric(1.5, "A"), "1.5 A");
    EXPECT_EQ(ChartEngine::format_metric(12.345, "A"), "12.3 A");
    EXPECT_EQ(ChartEngine::format_metric(123.4, "A"), "123 A");
    EXPECT_EQ(ChartEngine::format_metric(1000.0, "A"), "1 kA");
}

TEST(ChartFormatTest, metric_uses_three_significant_digits) {
    EXPECT_EQ(ChartEngine::format_metric(1.2333, "A"), "1.23 A");
    EXPECT_EQ(ChartEngine::format_metric(1.678, "A"), "1.68 A");
}

TEST(ChartFormatTest, metric_bumps_mantissa_to_next_prefix) {
    EXPECT_EQ(ChartEngine::format_metric(0.0009995, "A"), "1 mA");
    EXPECT_EQ(ChartEngine::format_metric(0.0009999, "A"), "1 mA");
    EXPECT_EQ(ChartEngine::format_metric(0.9999999, "V"), "1 V");
    // no scientific notation overflow like 1e+03uA for a mA value
    EXPECT_EQ(ChartEngine::format_metric(0.0009999, "A").find("e+"), std::string::npos);
}

TEST(ChartFormatTest, metric_zero_threshold_drops_prefix_and_separator) {
    // values below the 1e-12 threshold render as bare zero
    EXPECT_EQ(ChartEngine::format_metric(1e-13, "V"), "0V");
    EXPECT_EQ(ChartEngine::format_metric(1e-15, "V"), "0V");
    // the threshold itself still gets the pico prefix
    EXPECT_EQ(ChartEngine::format_metric(1e-12, "V"), "1 pV");
}

TEST(ChartFormatTest, metric_uses_micro_sign) {
    EXPECT_EQ(ChartEngine::format_metric(1e-6, "A"), "1 µA");
    // the former ASCII 'u' prefix is no longer emitted
    EXPECT_EQ(ChartEngine::format_metric(1e-6, "A").find("uA"), std::string::npos);
}

TEST(ChartFormatTest, metric_covers_all_si_scales) {
    EXPECT_EQ(ChartEngine::format_metric(1e9, "V"), "1 GV");
    EXPECT_EQ(ChartEngine::format_metric(2.5e6, "V"), "2.5 MV");
    EXPECT_EQ(ChartEngine::format_metric(1e-9, "V"), "1 nV");
    EXPECT_EQ(ChartEngine::format_metric(2.5e-12, "V"), "2.5 pV");
}

TEST(ChartFormatTest, metric_formats_negative_values) {
    EXPECT_EQ(ChartEngine::format_metric(-0.001, "A"), "-1 mA");
    EXPECT_EQ(ChartEngine::format_metric(-1500.0, "V"), "-1.5 kV");
}

TEST(ChartFormatTest, metric_bumps_kilo_mantissa_to_mega) {
    // 999.5 kV rounds up to the next prefix instead of overflowing the mantissa
    EXPECT_EQ(ChartEngine::format_metric(999500.0, "V"), "1 MV");
    EXPECT_EQ(ChartEngine::format_metric(1e10, "V"), "10 GV");
}

// ========================================================================================
// ratio clamping and zoom window handling
// ========================================================================================

TEST(ChartZoomTest, ratio_clamps_to_unit_interval) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 2.0, 4.0, 6.0, 8.0, 10.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 10.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // act
    const double below = chart.ratio_to_abscissa_value(-0.5);
    const double above = chart.ratio_to_abscissa_value(1.5);
    // assert — out-of-range ratios clamp to the range endpoints
    ASSERT_NEAR(below, 0.0, 1e-9);
    ASSERT_NEAR(above, 10.0, 1e-9);
}

TEST(ChartZoomTest, update_zoom_window_stores_ratios) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // act
    chart.update_zoom_window(0.2, 0.4, 0.3, 0.7);
    // assert
    const auto& zoom = chart.zoom_window();
    ASSERT_NEAR(std::get<0>(zoom), 0.2, 1e-9);
    ASSERT_NEAR(std::get<1>(zoom), 0.3, 1e-9);
    ASSERT_NEAR(std::get<2>(zoom), 0.4, 1e-9);
    ASSERT_NEAR(std::get<3>(zoom), 0.7, 1e-9);
}

TEST(ChartZoomTest, update_zoom_window_without_ratios_keeps_window) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    chart.update_zoom_window(0.2, 0.4, -1.0, -1.0);
    // act — a call without ratios leaves the stored window untouched
    chart.update_zoom_window(-1.0, -1.0, -1.0, -1.0);
    // assert
    const auto& zoom = chart.zoom_window();
    ASSERT_NEAR(std::get<0>(zoom), 0.2, 1e-9);
    ASSERT_NEAR(std::get<2>(zoom), 0.4, 1e-9);
}

TEST(ChartZoomTest, reset_zoom_window_restores_full_range) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> voltage_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    chart.plot_series({expression_manager.expressions()[1]});
    chart.update_zoom_window(0.2, 0.4, -1.0, -1.0);
    // act
    chart.reset_zoom_window(true, false);
    // assert — the visible range spans the full data again
    ASSERT_NEAR(chart.plot_ratio_to_abscissa_value(0.0), 0.0, 1e-9);
    ASSERT_NEAR(chart.plot_ratio_to_abscissa_value(1.0), 5.0, 1e-9);
}

// ========================================================================================
// series and step management
// ========================================================================================

TEST(ChartSeriesTest, selected_expressions_returns_plotted_expressions) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // act
    chart.plot_series({expression_manager.expressions()[1]});
    const auto selected = chart.selected_expressions();
    // assert
    ASSERT_EQ(selected.size(), 1);
    EXPECT_EQ(&std::get<Expression<double>>(*selected[0]), &std::get<Expression<double>>(*expression_manager.expressions()[1]));
}

TEST(ChartSeriesTest, plotting_empty_set_removes_existing_series) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    chart.plot_series({expression_manager.expressions()[1]});
    // act
    chart.plot_series({});
    // assert
    EXPECT_TRUE(chart.selected_expressions().empty());
}

TEST(ChartSeriesTest, clear_removes_all_series) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    chart.plot_series({expression_manager.expressions()[1]});
    // act
    chart.clear();
    // assert
    EXPECT_TRUE(chart.selected_expressions().empty());
}

TEST(ChartSeriesTest, selected_steps_default_to_all_steps) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}, {4, 8}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"R1"}, {{1000.0, 2000.0}}, {{0.0, 3.0}, {0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // act
    const auto& steps = chart.selected_steps();
    // assert — a fresh chart selects every available step
    ASSERT_EQ(steps.size(), 2);
    EXPECT_NE(steps.find(0u), steps.end());
    EXPECT_NE(steps.find(1u), steps.end());
}

TEST(ChartSeriesTest, update_preserves_step_selection_and_replots_from_new_data) {
    // arrange — two stepped runs, plot narrowed to the first step
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}, {4, 8}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"R1"}, {{1000.0, 2000.0}}, {{0.0, 3.0}, {0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    std::set<size_t> first_step = {0};
    chart.set_selected_steps(first_step);
    chart.plot_series({expression_manager.expressions()[1]});
    // act — simulate a re-run producing fresh data for the same signals
    std::vector<double> new_abscissa_data = {0.0, 1.0, 2.0, 3.0, 0.0, 1.0, 2.0, 3.0};
    std::vector<double> new_voltage_data = {10.0, 20.0, 30.0, 40.0, 20.0, 30.0, 40.0, 50.0};
    std::vector<std::pair<size_t, size_t>> new_step_slices = {{0, 4}, {4, 8}};
    std::vector<AnyExpression> new_expressions;
    new_expressions.emplace_back(Expression<double>("time", std::move(new_abscissa_data), new_step_slices, "s"));
    new_expressions.emplace_back(Expression<double>("V(out)", std::move(new_voltage_data), new_step_slices, "V"));
    ExpressionManager new_expression_manager(new_expressions, new_step_slices);
    StepInformation new_step_information({"R1"}, {{1000.0, 2000.0}}, {{0.0, 3.0}, {0.0, 3.0}});
    chart.update(&new_expression_manager, &new_step_information, AbscissaScale::LINEAR);
    // assert — the step selection survives the re-run
    ASSERT_EQ(chart.selected_steps().size(), 1);
    EXPECT_EQ(*chart.selected_steps().begin(), 0u);
    // the restored series points into the new data, not the previous run
    const auto restored = chart.selected_expressions();
    ASSERT_EQ(restored.size(), 1);
    EXPECT_EQ(&std::get<Expression<double>>(*restored[0]), &std::get<Expression<double>>(*new_expression_manager.expressions()[1]));
    // hovering reads values from the new run (step 0 interpolates to 25 V at 1.5 s)
    const std::string text = chart.hovered_series_text(1.5);
    EXPECT_NE(text.find("V(out)=25 V"), std::string::npos);
}

TEST(ChartSeriesTest, update_drops_series_missing_from_new_data) {
    // arrange — a chart with a plotted series
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    chart.plot_series({expression_manager.expressions()[1]});
    // act — the re-run produces data without the plotted signal
    std::vector<double> new_abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<std::pair<size_t, size_t>> new_step_slices = {{0, 4}};
    std::vector<AnyExpression> new_expressions;
    new_expressions.emplace_back(Expression<double>("time", std::move(new_abscissa_data), new_step_slices, "s"));
    ExpressionManager new_expression_manager(new_expressions, new_step_slices);
    StepInformation new_step_information({"time"}, {{}}, {{0.0, 3.0}});
    chart.update(&new_expression_manager, &new_step_information, AbscissaScale::LINEAR);
    // assert — the unresolvable series is gone without crashing
    EXPECT_TRUE(chart.selected_expressions().empty());
    EXPECT_EQ(chart.hovered_series_text(1.5), "");
}

TEST(ChartSeriesTest, set_selected_steps_drops_unselected_step_data) {
    // arrange — two stepped runs of the same expression
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}, {4, 8}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"R1"}, {{1000.0, 2000.0}}, {{0.0, 3.0}, {0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // plot both steps first
    std::set<size_t> both_steps = {0, 1};
    chart.set_selected_steps(both_steps);
    chart.plot_series({expression_manager.expressions()[1]});
    // act — narrow the selection to the second step
    std::set<size_t> second_step = {1};
    chart.set_selected_steps(second_step);
    // assert — the hover text groups a single step as a plain value, not a list
    const std::string text = chart.hovered_series_text(1.5);
    EXPECT_NE(text.find("V(out)="), std::string::npos);
    EXPECT_EQ(text.find("V(out)="), text.rfind("V(out)="));
    EXPECT_EQ(text.find("["), std::string::npos);
}

// ========================================================================================
// hover text edge cases
// ========================================================================================

TEST(ChartHoverTest, hovered_series_text_is_empty_without_series) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // act / assert
    EXPECT_EQ(chart.hovered_series_text(1.5), "");
}

TEST(ChartHoverTest, hovered_series_text_groups_multiple_steps) {
    // arrange — two stepped runs with different values at the same abscissa
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}, {4, 8}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"R1"}, {{1000.0, 2000.0}}, {{0.0, 3.0}, {0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    std::set<size_t> both_steps = {0, 1};
    chart.set_selected_steps(both_steps);
    chart.plot_series({expression_manager.expressions()[1]});
    // act
    const std::string text = chart.hovered_series_text(1.5);
    // assert — both step values are grouped in a bracketed list
    EXPECT_NE(text.find("V(out)=[2.5 V, 3.5 V]"), std::string::npos);
}

TEST(ChartZoomTest, new_chart_joins_the_shared_abscissa_zoom_window) {
    // arrange — a zoomed reference chart and a fresh chart added afterwards
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    ChartEngine zoomed(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    zoomed.plot_series({expression_manager.expressions()[1]});
    // zoom the reference chart horizontally to the [1, 3] quarter of the range
    zoomed.update_zoom_window(0.25, 0.75, -1, -1);
    // act — the new chart joins the shared horizontal zoom of the panel
    ChartEngine joined(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    const auto& shared = zoomed.zoom_window();
    joined.update_zoom_window(std::get<0>(shared), std::get<2>(shared), -1, -1);
    joined.plot_series({});
    // assert — identical abscissa range, vertical zoom stays unset in the new chart
    EXPECT_EQ(joined.abscissa_left_value(), zoomed.abscissa_left_value());
    EXPECT_EQ(joined.abscissa_right_value(), zoomed.abscissa_right_value());
    EXPECT_EQ(std::get<0>(joined.zoom_window()), std::get<0>(shared));
    EXPECT_EQ(std::get<2>(joined.zoom_window()), std::get<2>(shared));
    EXPECT_EQ(std::get<1>(joined.zoom_window()), -1.0);
    EXPECT_EQ(std::get<3>(joined.zoom_window()), -1.0);
}

TEST(ChartAxisTest, replotted_y1_series_does_not_increment_the_axis_refcount) {
    // arrange — a series assigned to the west axis (index 0)
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 3}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 2.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    chart.plot_series({expression_manager.expressions()[1]});
    ASSERT_EQ(chart.axes()[0].plots, 1);
    // act — re-plot the same expression: the Y1 assignment must be kept
    chart.plot_series({expression_manager.expressions()[1]});
    // assert — the y1 ref count does not grow on replots
    EXPECT_EQ(chart.axes()[0].plots, 1);
    // act — remove the series
    chart.plot_series({});
    // assert — the axis is released exactly once and back to unused
    EXPECT_EQ(chart.axes()[0].plots, 0);
}

TEST(ChartAxisTest, empty_step_spans_are_ignored_without_out_of_bounds_reads) {
    // arrange — one step whose slice covers zero samples
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 3}, {0, 0}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 2.0}, {0.0, 2.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // act — plot selecting the empty step must not read out of bounds
    std::set<size_t> both_steps = {0, 1};
    chart.set_selected_steps(both_steps);
    chart.plot_series({expression_manager.expressions()[1]});
    // assert — the plotted step keeps its range and the empty step leaves the
    // axis extrema untouched
    EXPECT_EQ(chart.axes()[0].min_value, 1.0);
    EXPECT_EQ(chart.axes()[0].max_value, 3.0);
    EXPECT_FALSE(chart.hovered_series_text(1.0).empty());
}

TEST(ChartExtremaTest, non_finite_samples_do_not_reach_the_axis_extrema) {
    // arrange — a series whose first sample is nan
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {std::nan(""), 3.0, 4.0, 5.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    // act
    chart.plot_series({expression_manager.expressions()[1]});
    // assert — the axis extrema come from the finite samples only
    EXPECT_EQ(chart.axes()[0].min_value, 3.0);
    EXPECT_EQ(chart.axes()[0].max_value, 5.0);
    // the ordinate range stays finite through the padded autorange
    ASSERT_LT(chart.axes()[0].plot_min_value, chart.axes()[0].plot_max_value);
    EXPECT_TRUE(std::isfinite(chart.axes()[0].plot_min_value));
    EXPECT_TRUE(std::isfinite(chart.axes()[0].plot_max_value));
}

TEST(ChartExtremaTest, deselected_outlier_step_no_longer_stretches_the_axis) {
    // arrange — two steps with disjoint value ranges
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 0.0, 1.0, 2.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 100.0, 110.0, 120.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 3}, {3, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 2.0}, {0.0, 2.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    std::set<size_t> both_steps = {0, 1};
    chart.set_selected_steps(both_steps);
    chart.plot_series({expression_manager.expressions()[1]});
    ASSERT_EQ(chart.axes()[0].max_value, 120.0);
    // act — deselect the outlier step
    std::set<size_t> first_only = {0};
    chart.set_selected_steps(first_only);
    // assert — the axis extrema track the retained step only
    EXPECT_EQ(chart.axes()[0].min_value, 1.0);
    EXPECT_EQ(chart.axes()[0].max_value, 3.0);
    ASSERT_LT(chart.axes()[0].plot_min_value, chart.axes()[0].plot_max_value);
}

TEST(ChartExtremaTest, axis_falls_back_to_the_default_range_when_no_step_is_plotted) {
    // arrange — one plotted series
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 3}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 2.0}});
    ChartEngine chart(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    chart.plot_series({expression_manager.expressions()[1]});
    // act — deselect every step
    chart.set_selected_steps({});
    // assert — the axis keeps a usable default range instead of a nan window
    EXPECT_EQ(chart.axes()[0].plot_min_value, 0.0);
    EXPECT_EQ(chart.axes()[0].plot_max_value, 1.0);
}

TEST(ChartUpdateTest, update_prunes_step_selections_beyond_the_new_dataset) {
    // arrange — a chart with three selected steps over a three step dataset
    std::vector<double> abscissa_a = {0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
    std::vector<double> voltage_a = {1.0, 2.0, 1.0, 2.0, 1.0, 2.0};
    std::vector<std::pair<size_t, size_t>> slices_a = {{0, 2}, {2, 4}, {4, 6}};
    std::vector<AnyExpression> expressions_a;
    expressions_a.emplace_back(Expression<double>("time", std::move(abscissa_a), slices_a, "s"));
    expressions_a.emplace_back(Expression<double>("V(out)", std::move(voltage_a), slices_a, "V"));
    ExpressionManager first_manager(expressions_a, slices_a);
    StepInformation first_information({"time"}, {{}}, {{0.0, 1.0}, {0.0, 1.0}, {0.0, 1.0}});
    ChartEngine chart(&first_manager, &first_information, AbscissaScale::LINEAR, 1000);
    chart.plot_series({first_manager.expressions()[1]});
    std::set<size_t> all_steps = {0, 1, 2};
    chart.set_selected_steps(all_steps);
    // arrange — a replacement dataset carrying a single step
    std::vector<double> abscissa_b = {0.0, 1.0};
    std::vector<double> voltage_b = {3.0, 4.0};
    std::vector<std::pair<size_t, size_t>> slices_b = {{0, 2}};
    std::vector<AnyExpression> expressions_b;
    expressions_b.emplace_back(Expression<double>("time", std::move(abscissa_b), slices_b, "s"));
    expressions_b.emplace_back(Expression<double>("V(out)", std::move(voltage_b), slices_b, "V"));
    ExpressionManager second_manager(expressions_b, slices_b);
    StepInformation second_information({"time"}, {{}}, {{0.0, 1.0}});
    // act — re-point the chart at the replacement dataset
    chart.update(&second_manager, &second_information, AbscissaScale::LINEAR);
    // assert — the stale third step was pruned and the series replotted
    const auto& selected = chart.selected_steps();
    EXPECT_EQ(selected.size(), 1u);
    EXPECT_TRUE(selected.contains(0));
    EXPECT_EQ(chart.axes()[0].min_value, 3.0);
    EXPECT_EQ(chart.axes()[0].max_value, 4.0);
}

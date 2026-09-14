#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "charts/chart_engine.h"
#include "charts/chart_layout.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"

TEST(ChartZoomLayoutTest, vertical_zoom_window_produces_ascending_axis_ranges) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 10.0};
    std::vector<double> voltage_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 11}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 10.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({expression_manager.expressions()[1]});
    // act — a band selected in the lower half of the plot (ratios from the top)
    engine.update_zoom_window(0.2, 0.4, 0.55, 0.76);
    // assert — the y axis stores an ascending value window between the band edges
    const auto& axes = engine.axes();
    ASSERT_LT(axes[0].plot_min_value, axes[0].plot_max_value);
    // visual range is the series range padded by 3% on both ends
    ASSERT_NEAR(axes[0].plot_min_value, 10.3 - 0.76 * 10.6, 1e-9);
    ASSERT_NEAR(axes[0].plot_max_value, 10.3 - 0.55 * 10.6, 1e-9);
}

TEST(ChartLayoutLimitsTest, linear_limits_pass_through_unchanged) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 2.0, 4.0, 6.0, 8.0, 10.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 10.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({});
    // act
    const auto [left_value, right_value] = clamped_abscissa_limits(engine);
    // assert
    ASSERT_NEAR(left_value, 0.0, 1e-9);
    ASSERT_NEAR(right_value, 10.0, 1e-9);
}

TEST(ChartLayoutLimitsTest, logarithmic_limits_clamp_non_positive_left) {
    // arrange
    std::vector<double> abscissa_data = {-1.0, 0.0, 1.0, 10.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("sweep", std::move(abscissa_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"sweep"}, {{}}, {{-1.0, 10.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::DECADE, 1000);
    engine.plot_series({});
    // act
    const auto [left_value, right_value] = clamped_abscissa_limits(engine);
    // assert — the left limit becomes a tiny fraction of the right limit
    ASSERT_NEAR(left_value, 1e-5, 1e-12);
    ASSERT_NEAR(right_value, 10.0, 1e-9);
}

TEST(ChartLayoutLimitsTest, logarithmic_limits_avoid_degenerate_range) {
    // arrange
    std::vector<double> abscissa_data = {2.0, 2.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 2}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("sweep", std::move(abscissa_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"sweep"}, {{}}, {{2.0, 2.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::DECADE, 1000);
    engine.plot_series({});
    // act
    const auto [left_value, right_value] = clamped_abscissa_limits(engine);
    // assert — equal limits expand so the axis keeps a usable range
    ASSERT_NEAR(left_value, 2.0, 1e-9);
    ASSERT_NEAR(right_value, 4.0, 1e-9);
}

TEST(ChartLayoutGeometryTest, frame_insets_canvas_and_reserves_x_axis_strip) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(engine, 800.0f, 600.0f);
    // assert — canvas is inset by the plot padding on every side
    ASSERT_NEAR(frame.frame_w, 800.0f, 1e-6);
    ASSERT_NEAR(frame.frame_h, 600.0f, 1e-6);
    // the x axis strip below the plot holds one text line plus label padding
    ASSERT_NEAR(frame.plot_h, 600.0f - 20.0f - (14.0f + 6.0f), 1e-6);
    ASSERT_NEAR(frame.plot_y, 10.0f, 1e-6);
    ASSERT_NEAR(frame.x_datum, 10.0f + frame.plot_h, 1e-6);
    // y1 is enabled and its datum sits at the plot left edge
    ASSERT_TRUE(frame.y_axes[0].enabled);
    ASSERT_NEAR(frame.y_axes[0].datum, frame.plot_x, 1e-6);
    // the left pad reserves room for the default 0..1 axis labels
    ASSERT_GT(frame.plot_x, 10.0f);
}

TEST(ChartLayoutGeometryTest, legend_shrinks_canvas_and_is_positioned_south) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({expression_manager.expressions()[1]});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(engine, 800.0f, 600.0f);
    // assert — one legend entry of six characters
    ASSERT_EQ(frame.legend.size(), 1u);
    // horizontal legend size: inner padding, icon per entry, labels and spacing
    ASSERT_NEAR(frame.legend_w, 2.0f * 8.0f + 14.0f + 6.0f * 7.0f, 1e-6);
    ASSERT_NEAR(frame.legend_h, 2.0f * 4.0f + 14.0f, 1e-6);
    // the legend is centered horizontally and sits below the abscissa tick
    // label band, like the settled implot render (legend anchored to the
    // canvas south edge, never overlapping the tick labels)
    ASSERT_NEAR(frame.legend_x, (800.0f - frame.legend_w) * 0.5f, 1e-6);
    ASSERT_NEAR(frame.legend_y, 10.0f + 580.0f - frame.legend_h, 1e-6);
    // the canvas reserves the legend block (legend height plus legend padding)
    // below the x axis strip before the plot rect is computed
    ASSERT_NEAR(frame.plot_h, 580.0f - (14.0f + 6.0f) - (22.0f + 6.0f), 1e-6);
    ASSERT_NEAR(frame.x_datum, 10.0f + frame.plot_h, 1e-6);
}

TEST(ChartLayoutGeometryTest, left_pad_reserves_y_tick_label_width) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {0.4, 0.5, 0.45, 0.55};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("I(R1)", std::move(voltage_data), step_slices, "A"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({expression_manager.expressions()[1]});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(engine, 800.0f, 600.0f);
    // assert — the widest shown y label drives the left pad (420 mA is 6 chars)
    ASSERT_NEAR(frame.plot_x, 10.0f + 6.0f * 7.0f + 6.0f, 1e-3);
    ASSERT_NEAR(frame.y_axes[0].datum, frame.plot_x, 1e-3);
}

TEST(ChartLayoutTicksTest, linear_locator_places_majors_and_minors) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 10.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 2}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 10.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(engine, 800.0f, 600.0f);
    // assert — ticks cover the range with major ticks carrying labels
    ASSERT_FALSE(frame.x_ticks.empty());
    const ChartTick& first = frame.x_ticks.front();
    ASSERT_NEAR(first.value, 0.0, 1e-9);
    ASSERT_TRUE(first.major);
    ASSERT_NEAR(first.pixel_pos, frame.plot_x, 1e-3);
    // the last tick maps to the right plot edge
    const ChartTick& last = frame.x_ticks.back();
    ASSERT_NEAR(last.pixel_pos, frame.plot_x + frame.plot_w, 1e-3);
    // minor ticks exist between majors and are classified as minor
    bool has_minor = false;
    for (const auto& tick : frame.x_ticks)
        if (!tick.major)
            has_minor = true;
    ASSERT_TRUE(has_minor);
}

TEST(ChartLayoutTicksTest, linear_locator_major_grid_is_unconditional_and_inside_plot) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 10.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 2}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 10.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(engine, 800.0f, 600.0f);
    // assert — every major grid line keeps full alpha and stays in the plot
    ASSERT_FALSE(frame.x_grid.empty());
    for (const auto& line : frame.x_grid)
        if (line.major)
            ASSERT_NEAR(line.alpha, 1.0f, 1e-6);
    // grid pixel positions are relative to the frame origin
    const ChartGridLine& first = frame.x_grid.front();
    ASSERT_NEAR(first.pixel_pos, frame.plot_x, 1e-3);
}

TEST(ChartLayoutSeriesTest, linear_series_points_map_to_pixel_coordinates) {
    // arrange
    std::vector<double> abscissa_data = {0.0, 2.0, 4.0, 6.0, 8.0, 10.0};
    std::vector<double> voltage_data = {0.0, 2.5, 5.0, 5.0, 2.5, 0.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 10.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({expression_manager.expressions()[1]});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(engine, 800.0f, 600.0f);
    // assert — one series run with all decimated samples
    ASSERT_EQ(frame.series.size(), 1u);
    const ChartSeriesFrame& run = frame.series[0];
    ASSERT_EQ(run.name, "V(out)");
    ASSERT_EQ(run.points.size(), 6u);
    // first sample maps to the left plot edge
    ASSERT_NEAR(run.points[0].x, frame.plot_x, 1e-3);
    // last sample maps to the right plot edge
    ASSERT_NEAR(run.points[5].x, frame.plot_x + frame.plot_w, 1e-3);
    // the 2.5 V midpoint sits halfway up the y axis (auto range pads both ends by 3%)
    ASSERT_NEAR(run.points[1].y, frame.plot_y + frame.plot_h * 0.5f, 1e-3);
}

TEST(ChartLayoutSeriesTest, logarithmic_series_points_map_logarithmically) {
    // arrange
    std::vector<double> abscissa_data = {1.0, 10.0, 100.0, 1000.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("sweep", std::move(abscissa_data), step_slices, "V"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"sweep"}, {{}}, {{1.0, 1000.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::DECADE, 1000);
    engine.plot_series({expression_manager.expressions()[1]});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(engine, 800.0f, 600.0f);
    // assert — the 10.0 sample sits one third along the log axis
    ASSERT_EQ(frame.series.size(), 1u);
    const ChartSeriesFrame& run = frame.series[0];
    ASSERT_NEAR(run.points[1].x, frame.plot_x + frame.plot_w / 3.0f, 1e-3);
    // the 100.0 sample sits two thirds along the log axis
    ASSERT_NEAR(run.points[2].x, frame.plot_x + 2.0f * frame.plot_w / 3.0f, 1e-3);
}

TEST(ChartLayoutSeriesTest, multi_axis_series_map_to_their_own_axis) {
    // arrange — two series with different units claim separate y axes
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> current_data = {0.1, 0.2, 0.3, 0.4};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    expressions.emplace_back(Expression<double>("I(R1)", std::move(current_data), step_slices, "A"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 3.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({expression_manager.expressions()[1], expression_manager.expressions()[2]});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(engine, 800.0f, 600.0f);
    // assert — both axes are enabled with the y2 datum at the plot right edge
    ASSERT_TRUE(frame.y_axes[0].enabled);
    ASSERT_TRUE(frame.y_axes[1].enabled);
    ASSERT_TRUE(frame.y_axes[1].opposite);
    ASSERT_NEAR(frame.y_axes[1].datum, frame.plot_x + frame.plot_w, 1e-3);
    // the second series run maps into the same plot rect
    ASSERT_EQ(frame.series.size(), 2u);
    for (const auto& run : frame.series) {
        ASSERT_GE(run.points.front().x, frame.plot_x - 1.0f);
        ASSERT_LE(run.points.back().x, frame.plot_x + frame.plot_w + 1.0f);
    }
}

TEST(ChartLayoutLimitsTest, descending_sweep_maps_larger_values_toward_the_west_edge) {
    // arrange — step ranges describing a descending sweep
    std::vector<double> abscissa_data = {5.0, 4.0, 3.0, 2.0, 1.0, 0.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{5.0, 0.0}});
    ChartEngine engine(&expression_manager, &step_information, AbscissaScale::LINEAR, 1000);
    engine.plot_series({expression_manager.expressions()[1]});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(engine, 800.0f, 600.0f);
    // assert — the locator produced usable ticks on the reversed bounds
    ASSERT_FALSE(frame.x_ticks.empty());
    for (const auto& tick : frame.x_ticks)
        ASSERT_TRUE(std::isfinite(tick.pixel_pos));
    // the largest tick value sits at the plot west edge and the smallest at
    // the east edge
    double largest_value = frame.x_ticks.front().value;
    double smallest_value = largest_value;
    float position_of_largest = frame.x_ticks.front().pixel_pos;
    float position_of_smallest = position_of_largest;
    for (const auto& tick : frame.x_ticks) {
        if (tick.value > largest_value) {
            largest_value = tick.value;
            position_of_largest = tick.pixel_pos;
        }
        if (tick.value < smallest_value) {
            smallest_value = tick.value;
            position_of_smallest = tick.pixel_pos;
        }
    }
    EXPECT_LT(position_of_largest, position_of_smallest);
    // the plotted series maps into the same plot rect with the largest
    // sample value at the west edge
    ASSERT_EQ(frame.series.size(), 1u);
    const auto& run = frame.series.front();
    ASSERT_EQ(run.points.size(), 6u);
    EXPECT_LT(run.points.front().x, run.points.back().x);
}

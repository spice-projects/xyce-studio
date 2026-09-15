#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "charts/chart_engine.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "ui/charts_renderer.h"

TEST(ChartsZoomTest, zoom_on_one_chart_applies_full_2d_there_and_horizontal_only_elsewhere) {
    // arrange — panel with two charts, the first plotting a six sample signal
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(out)"}});
    renderer.add_chart();
    renderer.plot_chart_expressions(1, {expression_manager.expressions()[1]});
    renderer.publish_frames();
    // capture the shared pre-zoom abscissa tick labels from the second chart
    ASSERT_EQ(published.back().size(), 2u);
    const ChartFrame& full_second = published.back()[1];
    const size_t pre_zoom_tick_count = full_second.x_ticks.size();
    // act — drag a zoom selection inside the first chart
    const ChartFrame& full_first = published.back()[0];
    const float x_min = full_first.plot_x;
    const float y_min = full_first.plot_y;
    const float width = full_first.plot_w;
    const float height = full_first.plot_h;
    renderer.zoom_drag_started(x_min + width * 0.05f, y_min + height * 0.05f);
    renderer.zoom_drag_moved(x_min + width * 0.80f, y_min + height * 0.80f);
    renderer.zoom_drag_ended();
    renderer.publish_frames();
    // assert — the first chart received the full 2d zoom: its ordinate range
    // sits strictly inside the untouched ordinate range of the second chart
    ASSERT_EQ(published.back().size(), 2u);
    const ChartFrame& zoomed_first = published.back()[0];
    const ChartFrame& zoomed_second = published.back()[1];
    ASSERT_FALSE(zoomed_first.y_axes[0].ticks.empty());
    ASSERT_FALSE(zoomed_second.y_axes[0].ticks.empty());
    EXPECT_GT(zoomed_first.y_axes[0].ticks.front().value, zoomed_second.y_axes[0].ticks.front().value);
    EXPECT_LT(zoomed_first.y_axes[0].ticks.back().value, zoomed_second.y_axes[0].ticks.back().value);
    // both charts share the zoomed abscissa range: identical tick labels that
    // differ from the pre-zoom full range
    ASSERT_EQ(zoomed_first.x_ticks.size(), zoomed_second.x_ticks.size());
    for (size_t t = 0; t < zoomed_first.x_ticks.size(); ++t)
        EXPECT_EQ(zoomed_first.x_ticks[t].label, zoomed_second.x_ticks[t].label);
    EXPECT_NE(zoomed_second.x_ticks.size(), pre_zoom_tick_count);
}

TEST(ChartsZoomTest, chart_added_after_a_zoom_joins_the_shared_abscissa_range) {
    // arrange — panel with two charts, zoom performed on the first chart
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> voltage_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(voltage_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(out)"}});
    renderer.add_chart();
    renderer.plot_chart_expressions(1, {expression_manager.expressions()[1]});
    renderer.publish_frames();
    const ChartFrame& pre = published.back()[0];
    renderer.zoom_drag_started(pre.plot_x + pre.plot_w * 0.05f, pre.plot_y + pre.plot_h * 0.05f);
    renderer.zoom_drag_moved(pre.plot_x + pre.plot_w * 0.80f, pre.plot_y + pre.plot_h * 0.80f);
    renderer.zoom_drag_ended();
    renderer.publish_frames();
    // copy the frames: the next publish appends to the outer vector and may
    // reallocate it, invalidating references
    const ChartFrame zoomed = published.back()[0];
    const ChartFrame other = published.back()[1];
    // act — add a third chart after the zoom and plot the same signal on it
    renderer.add_chart();
    renderer.plot_chart_expressions(2, {expression_manager.expressions()[1]});
    renderer.publish_frames();
    // assert — the new chart shows the shared zoomed abscissa range
    ASSERT_EQ(published.back().size(), 3u);
    const ChartFrame& joined = published.back()[2];
    ASSERT_EQ(joined.x_ticks.size(), zoomed.x_ticks.size());
    for (size_t t = 0; t < joined.x_ticks.size(); ++t)
        EXPECT_EQ(joined.x_ticks[t].label, zoomed.x_ticks[t].label);
    // the vertical zoom of the dragged chart is not propagated: the joined
    // chart autoranges over the full plotted data like the untouched chart
    ASSERT_FALSE(joined.y_axes[0].ticks.empty());
    ASSERT_FALSE(other.y_axes[0].ticks.empty());
    EXPECT_EQ(joined.y_axes[0].ticks.front().value, other.y_axes[0].ticks.front().value);
    EXPECT_EQ(joined.y_axes[0].ticks.back().value, other.y_axes[0].ticks.back().value);
}

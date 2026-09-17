#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "charts/chart_engine.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "ui/charts_renderer.h"

TEST(ChartsActionsTest, zoom_to_fit_resets_the_zoom_window_on_every_chart) {
    // arrange — two charts with zoom applied by dragging inside the first one
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> va_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<double> vb_data = {11.0, 12.0, 13.0, 14.0, 15.0, 16.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(a)", std::move(va_data), step_slices, "V"));
    expressions.emplace_back(Expression<double>("V(b)", std::move(vb_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}, {"V(b)"}}, false);
    const ChartFrame pre_first = published.back()[0];
    const ChartFrame pre_second = published.back()[1];
    renderer.zoom_drag_started(pre_first.plot_x + pre_first.plot_w * 0.1f, pre_first.plot_y + pre_first.plot_h * 0.1f);
    renderer.zoom_drag_moved(pre_first.plot_x + pre_first.plot_w * 0.8f, pre_first.plot_y + pre_first.plot_h * 0.8f);
    renderer.zoom_drag_ended();
    const ChartFrame zoomed_first = published.back()[0];
    // act — zoom to fit triggered from the context menu of the second chart
    renderer.zoom_to_fit(0.75f);
    // assert — both charts show the full abscissa range again; the second
    // chart (the one the menu was opened on) also lost its vertical zoom
    // while the first chart keeps it
    const ChartFrame reset_first = published.back()[0];
    const ChartFrame reset_second = published.back()[1];
    EXPECT_EQ(reset_first.x_ticks.front().value, pre_first.x_ticks.front().value);
    EXPECT_EQ(reset_first.x_ticks.back().value, pre_first.x_ticks.back().value);
    EXPECT_EQ(reset_second.x_ticks.front().value, pre_second.x_ticks.front().value);
    EXPECT_EQ(reset_second.x_ticks.back().value, pre_second.x_ticks.back().value);
    EXPECT_EQ(reset_second.y_axes[0].ticks.front().value, pre_second.y_axes[0].ticks.front().value);
    EXPECT_EQ(reset_second.y_axes[0].ticks.back().value, pre_second.y_axes[0].ticks.back().value);
    // the zoomed state really differed from the reset one before the action
    EXPECT_GT(zoomed_first.y_axes[0].ticks.front().value, pre_first.y_axes[0].ticks.front().value);
    EXPECT_LT(zoomed_first.y_axes[0].ticks.back().value, pre_first.y_axes[0].ticks.back().value);
}

TEST(ChartsActionsTest, autorange_resets_only_the_vertical_zoom_of_the_target_chart) {
    // arrange — two charts with zoom applied by dragging inside the first one
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> va_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<double> vb_data = {11.0, 12.0, 13.0, 14.0, 15.0, 16.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(a)", std::move(va_data), step_slices, "V"));
    expressions.emplace_back(Expression<double>("V(b)", std::move(vb_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}, {"V(b)"}}, false);
    const ChartFrame pre_first = published.back()[0];
    renderer.zoom_drag_started(pre_first.plot_x + pre_first.plot_w * 0.1f, pre_first.plot_y + pre_first.plot_h * 0.1f);
    renderer.zoom_drag_moved(pre_first.plot_x + pre_first.plot_w * 0.8f, pre_first.plot_y + pre_first.plot_h * 0.8f);
    renderer.zoom_drag_ended();
    const ChartFrame zoomed_first = published.back()[0];
    const ChartFrame zoomed_second = published.back()[1];
    // act — autorange triggered from the context menu of the first chart
    renderer.autorange(0.25f);
    // assert — the first chart regained its full ordinate range while keeping
    // the zoomed abscissa range; the second chart kept both zoomed ranges
    const ChartFrame ranged_first = published.back()[0];
    const ChartFrame ranged_second = published.back()[1];
    EXPECT_EQ(ranged_first.y_axes[0].ticks.front().value, pre_first.y_axes[0].ticks.front().value);
    EXPECT_EQ(ranged_first.y_axes[0].ticks.back().value, pre_first.y_axes[0].ticks.back().value);
    EXPECT_EQ(ranged_first.x_ticks.front().value, zoomed_first.x_ticks.front().value);
    EXPECT_EQ(ranged_first.x_ticks.back().value, zoomed_first.x_ticks.back().value);
    EXPECT_EQ(ranged_second.x_ticks.front().value, zoomed_second.x_ticks.front().value);
    EXPECT_EQ(ranged_second.x_ticks.back().value, zoomed_second.x_ticks.back().value);
}

TEST(ChartsActionsTest, zoom_abscissa_extent_resets_the_horizontal_zoom_on_every_chart_but_keeps_the_vertical_one) {
    // arrange — two charts with zoom applied by dragging inside the first one
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> va_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<double> vb_data = {11.0, 12.0, 13.0, 14.0, 15.0, 16.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(a)", std::move(va_data), step_slices, "V"));
    expressions.emplace_back(Expression<double>("V(b)", std::move(vb_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}, {"V(b)"}}, false);
    const ChartFrame pre_first = published.back()[0];
    const ChartFrame pre_second = published.back()[1];
    renderer.zoom_drag_started(pre_first.plot_x + pre_first.plot_w * 0.1f, pre_first.plot_y + pre_first.plot_h * 0.1f);
    renderer.zoom_drag_moved(pre_first.plot_x + pre_first.plot_w * 0.8f, pre_first.plot_y + pre_first.plot_h * 0.8f);
    renderer.zoom_drag_ended();
    const ChartFrame zoomed_first = published.back()[0];
    // act — zoom abscissa extent triggered from any chart context menu
    renderer.zoom_abscissa_extent(0.25f);
    // assert — both charts show the full abscissa range again, the first
    // chart keeps its zoomed vertical range and the second is untouched
    const ChartFrame extended_first = published.back()[0];
    const ChartFrame extended_second = published.back()[1];
    EXPECT_EQ(extended_first.x_ticks.front().value, pre_first.x_ticks.front().value);
    EXPECT_EQ(extended_first.x_ticks.back().value, pre_first.x_ticks.back().value);
    EXPECT_EQ(extended_second.x_ticks.front().value, pre_second.x_ticks.front().value);
    EXPECT_EQ(extended_second.x_ticks.back().value, pre_second.x_ticks.back().value);
    EXPECT_EQ(extended_first.y_axes[0].ticks.front().value, zoomed_first.y_axes[0].ticks.front().value);
    EXPECT_EQ(extended_first.y_axes[0].ticks.back().value, zoomed_first.y_axes[0].ticks.back().value);
    EXPECT_EQ(extended_second.y_axes[0].ticks.front().value, pre_second.y_axes[0].ticks.front().value);
    EXPECT_EQ(extended_second.y_axes[0].ticks.back().value, pre_second.y_axes[0].ticks.back().value);
}

TEST(ChartsActionsTest, a_drag_selection_smaller_than_the_threshold_does_not_zoom) {
    // arrange — one chart plotting V(a) with its pre-zoom frame captured
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> va_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(a)", std::move(va_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}}, false);
    const ChartFrame pre = published.back()[0];
    // act — drag a 5px selection inside the plot area, below the 10px threshold
    renderer.zoom_drag_started(pre.plot_x + pre.plot_w * 0.5f, pre.plot_y + pre.plot_h * 0.5f);
    renderer.zoom_drag_moved(pre.plot_x + pre.plot_w * 0.5f + 5.0f, pre.plot_y + pre.plot_h * 0.5f + 5.0f);
    renderer.zoom_drag_ended();
    // assert — the zoom window was not applied: the full range is still shown
    const ChartFrame after = published.back()[0];
    EXPECT_EQ(after.x_ticks.front().value, pre.x_ticks.front().value);
    EXPECT_EQ(after.x_ticks.back().value, pre.x_ticks.back().value);
}

TEST(ChartsActionsTest, a_drag_that_never_moves_applies_no_zoom) {
    // arrange — one chart plotting V(a) with its pre-zoom frame captured
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> va_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(a)", std::move(va_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}}, false);
    const ChartFrame pre = published.back()[0];
    // act — press inside the plot area and release without moving
    renderer.zoom_drag_started(pre.plot_x + pre.plot_w * 0.5f, pre.plot_y + pre.plot_h * 0.5f);
    renderer.zoom_drag_ended();
    // assert — no zoom was applied
    const ChartFrame after = published.back()[0];
    EXPECT_EQ(after.x_ticks.front().value, pre.x_ticks.front().value);
    EXPECT_EQ(after.x_ticks.back().value, pre.x_ticks.back().value);
}

TEST(ChartsActionsTest, a_drag_cancelation_discards_the_selection) {
    // arrange — one chart plotting V(a) with its pre-zoom frame captured
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> va_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(a)", std::move(va_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}}, false);
    const ChartFrame pre = published.back()[0];
    // act — start a valid selection and cancel it before releasing
    renderer.zoom_drag_started(pre.plot_x + pre.plot_w * 0.1f, pre.plot_y + pre.plot_h * 0.1f);
    renderer.zoom_drag_moved(pre.plot_x + pre.plot_w * 0.8f, pre.plot_y + pre.plot_h * 0.8f);
    renderer.zoom_drag_canceled();
    // assert — no zoom was applied despite the large selection
    const ChartFrame after = published.back()[0];
    EXPECT_EQ(after.x_ticks.front().value, pre.x_ticks.front().value);
    EXPECT_EQ(after.x_ticks.back().value, pre.x_ticks.back().value);
}

TEST(ChartsActionsTest, a_drag_started_outside_the_plot_area_does_not_zoom) {
    // arrange — one chart plotting V(a) with its pre-zoom frame captured
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> va_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(a)", std::move(va_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}}, false);
    const ChartFrame pre = published.back()[0];
    // act — start the drag one pixel left of the plot area and release inside
    renderer.zoom_drag_started(pre.plot_x - 1.0f, pre.plot_y + pre.plot_h * 0.5f);
    renderer.zoom_drag_moved(pre.plot_x + pre.plot_w * 0.8f, pre.plot_y + pre.plot_h * 0.8f);
    renderer.zoom_drag_ended();
    // assert — no chart accepted the drag so no zoom was applied
    const ChartFrame after = published.back()[0];
    EXPECT_EQ(after.x_ticks.front().value, pre.x_ticks.front().value);
    EXPECT_EQ(after.x_ticks.back().value, pre.x_ticks.back().value);
}

TEST(ChartsActionsTest, context_menu_actions_without_an_active_dataset_are_safe) {
    // arrange — a renderer without a dataset and without publishing anything
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    // act — trigger every position based context menu action without a dataset
    renderer.zoom_to_fit(0.5f);
    renderer.autorange(0.5f);
    renderer.zoom_abscissa_extent(0.5f);
    renderer.delete_all_plots(0.5f);
    // assert — nothing was published and nothing crashed
    EXPECT_TRUE(published.empty());
    EXPECT_EQ(renderer.chart_count(), 0u);
}

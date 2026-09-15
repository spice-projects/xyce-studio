#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "ui/charts_renderer.h"

TEST(ChartsMoveChartTest, move_chart_reorders_the_stack_and_carries_the_plotted_series) {
    // arrange — panel with three charts plotting V(a), V(b) and V(c) so the
    // charts are identifiable by their plotted expressions
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> va_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<double> vb_data = {11.0, 12.0, 13.0, 14.0, 15.0, 16.0};
    std::vector<double> vc_data = {21.0, 22.0, 23.0, 24.0, 25.0, 26.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(a)", std::move(va_data), step_slices, "V"));
    expressions.emplace_back(Expression<double>("V(b)", std::move(vb_data), step_slices, "V"));
    expressions.emplace_back(Expression<double>("V(c)", std::move(vc_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}, {"V(b)"}, {"V(c)"}});
    ASSERT_EQ(renderer.chart_count(), 3u);
    // act — move the last chart to the top of the stack
    renderer.move_chart(2, 0);
    // assert — the stack is reordered and every chart kept its series
    ASSERT_EQ(renderer.chart_count(), 3u);
    const std::vector<AnyExpression*> first = renderer.chart_selected_expressions(0);
    const std::vector<AnyExpression*> second = renderer.chart_selected_expressions(1);
    const std::vector<AnyExpression*> third = renderer.chart_selected_expressions(2);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], expression_manager.expressions()[3]);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0], expression_manager.expressions()[1]);
    ASSERT_EQ(third.size(), 1u);
    EXPECT_EQ(third[0], expression_manager.expressions()[2]);
    // the reordered frames were published once by the move
    ASSERT_EQ(published.back().size(), 3u);
}

TEST(ChartsMoveChartTest, move_chart_downward_lands_at_the_target_index) {
    // arrange — panel with three charts plotting V(a), V(b), V(c)
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> va_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<double> vb_data = {11.0, 12.0, 13.0, 14.0, 15.0, 16.0};
    std::vector<double> vc_data = {21.0, 22.0, 23.0, 24.0, 25.0, 26.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    expressions.emplace_back(Expression<double>("V(a)", std::move(va_data), step_slices, "V"));
    expressions.emplace_back(Expression<double>("V(b)", std::move(vb_data), step_slices, "V"));
    expressions.emplace_back(Expression<double>("V(c)", std::move(vc_data), step_slices, "V"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}, {"V(b)"}, {"V(c)"}});
    // act — move the first chart to the last position
    renderer.move_chart(0, 2);
    // assert — the moved chart sits between the other two charts
    const std::vector<AnyExpression*> first = renderer.chart_selected_expressions(0);
    const std::vector<AnyExpression*> second = renderer.chart_selected_expressions(1);
    const std::vector<AnyExpression*> third = renderer.chart_selected_expressions(2);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], expression_manager.expressions()[2]);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0], expression_manager.expressions()[3]);
    ASSERT_EQ(third.size(), 1u);
    EXPECT_EQ(third[0], expression_manager.expressions()[1]);
}

TEST(ChartsMoveChartTest, move_chart_carries_the_zoom_window_with_the_chart) {
    // arrange — panel with two charts and a shared zoomed abscissa range
    // applied by dragging inside the first chart
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
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}, {"V(b)"}});
    const ChartFrame pre_first = published.back()[0];
    renderer.zoom_drag_started(pre_first.plot_x + pre_first.plot_w * 0.1f, pre_first.plot_y + pre_first.plot_h * 0.1f);
    renderer.zoom_drag_moved(pre_first.plot_x + pre_first.plot_w * 0.8f, pre_first.plot_y + pre_first.plot_h * 0.8f);
    renderer.zoom_drag_ended();
    const ChartFrame zoomed_first = published.back()[0];
    const ChartFrame pre_second = published.back()[1];
    // act — move the second chart to the top of the stack
    renderer.move_chart(1, 0);
    // assert — the moved chart carries its full ordinate range (untouched by
    // the zoom) and shares the zoomed abscissa range with the other chart
    const ChartFrame& moved = published.back()[0];
    const ChartFrame& zoomed = published.back()[1];
    ASSERT_EQ(moved.x_ticks.size(), zoomed.x_ticks.size());
    for (size_t t = 0; t < zoomed.x_ticks.size(); ++t)
        EXPECT_EQ(moved.x_ticks[t].label, zoomed.x_ticks[t].label);
    ASSERT_FALSE(moved.y_axes[0].ticks.empty());
    EXPECT_EQ(moved.y_axes[0].ticks.front().value, pre_second.y_axes[0].ticks.front().value);
    EXPECT_EQ(moved.y_axes[0].ticks.back().value, pre_second.y_axes[0].ticks.back().value);
    // the zoomed chart still carries its zoomed vertical range
    ASSERT_FALSE(zoomed.y_axes[0].ticks.empty());
    EXPECT_EQ(zoomed.y_axes[0].ticks.front().value, zoomed_first.y_axes[0].ticks.front().value);
    EXPECT_EQ(zoomed.y_axes[0].ticks.back().value, zoomed_first.y_axes[0].ticks.back().value);
}

TEST(ChartsMoveChartTest, move_chart_to_the_same_index_is_a_noop) {
    // arrange — panel with two charts plotting V(a) and V(b)
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
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}, {"V(b)"}});
    const size_t publish_count = published.size();
    // act — move the first chart onto itself
    renderer.move_chart(0, 0);
    // assert — nothing changed and nothing was published
    EXPECT_EQ(published.size(), publish_count);
    const std::vector<AnyExpression*> first = renderer.chart_selected_expressions(0);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], expression_manager.expressions()[1]);
}

TEST(ChartsMoveChartTest, move_chart_with_out_of_range_indexes_is_a_noop) {
    // arrange — panel with one chart plotting V(a)
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
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}});
    const size_t publish_count = published.size();
    // act — move with indexes beyond the stack
    renderer.move_chart(0, 5);
    renderer.move_chart(5, 0);
    // assert — nothing changed and nothing was published
    EXPECT_EQ(published.size(), publish_count);
    EXPECT_EQ(renderer.chart_count(), 1u);
    const std::vector<AnyExpression*> first = renderer.chart_selected_expressions(0);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], expression_manager.expressions()[1]);
}

TEST(ChartsMoveChartTest, move_chart_without_an_active_dataset_is_a_safe_noop) {
    // arrange — a renderer without a dataset
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    // act
    renderer.move_chart(0, 1);
    // assert — nothing was published and nothing crashed
    EXPECT_TRUE(published.empty());
    EXPECT_EQ(renderer.chart_count(), 0u);
}

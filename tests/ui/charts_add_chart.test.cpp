#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "ui/charts_renderer.h"

TEST(ChartsAddChartTest, add_chart_after_index_inserts_directly_after_the_referenced_chart) {
    // arrange — panel with a chart plotting V(a) and a second chart plotting
    // V(b), so the charts are identifiable by their plotted expressions
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
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}}, false);
    renderer.add_chart();
    renderer.plot_chart_expressions(1, {expression_manager.expressions()[2]});
    ASSERT_EQ(renderer.chart_count(), 2u);
    // act — add a chart directly after the first one, like the context menu
    // opened on the first chart would
    renderer.add_chart(0);
    // publish frames to show the new chart, like the view does
    renderer.publish_frames();
    // assert — three frames stacked top to bottom and the new chart sits
    // between the two plotted ones: empty selection at the inserted index
    ASSERT_EQ(published.back().size(), 3u);
    ASSERT_EQ(renderer.chart_count(), 3u);
    const std::vector<AnyExpression*> first = renderer.chart_selected_expressions(0);
    const std::vector<AnyExpression*> inserted = renderer.chart_selected_expressions(1);
    const std::vector<AnyExpression*> last = renderer.chart_selected_expressions(2);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], expression_manager.expressions()[1]);
    EXPECT_TRUE(inserted.empty());
    ASSERT_EQ(last.size(), 1u);
    EXPECT_EQ(last[0], expression_manager.expressions()[2]);
}

TEST(ChartsAddChartTest, add_chart_with_out_of_range_index_appends_at_the_end) {
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
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}}, false);
    ASSERT_EQ(renderer.chart_count(), 1u);
    // act — add a chart referencing an index beyond the stack
    renderer.add_chart(50);
    // publish frames to show the new chart, like the view does
    renderer.publish_frames();
    // assert — the chart is appended at the end of the stack
    ASSERT_EQ(published.back().size(), 2u);
    ASSERT_EQ(renderer.chart_count(), 2u);
    const std::vector<AnyExpression*> first = renderer.chart_selected_expressions(0);
    const std::vector<AnyExpression*> appended = renderer.chart_selected_expressions(1);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], expression_manager.expressions()[1]);
    EXPECT_TRUE(appended.empty());
}

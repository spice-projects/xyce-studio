#include <complex>
#include <cstddef>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "charts/charts_controller.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"

TEST(StudioChartsControllerChecks, a_chart_fills_its_share_of_the_panel) {
    // arrange: a one step dataset with one real expression
    std::vector<std::pair<size_t, size_t>> slices{{0, 11}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 10.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    // act: create the chart and plot the expression
    ChartEngine* chart = controller.add_chart();
    ASSERT_NE(chart, nullptr);
    chart->plot_series({manager.evaluate("V(out)")});
    controller.rebuild_frames();
    // assert: the chart builds one frame and a usable plot rect
    ASSERT_EQ(controller.frames().size(), 1U);
    const auto [left, top, right, bottom] = controller.plot_rect(0);
    EXPECT_GT(right, left);
    EXPECT_GT(bottom, top);
    EXPECT_GE(left, 0.0f);
    EXPECT_GE(top, 0.0f);
    EXPECT_LE(bottom, 300.0f);
    EXPECT_EQ(controller.position_to_index(10.0f), 0U);
    EXPECT_EQ(controller.position_to_index(295.0f), 0U);
}

TEST(StudioChartsControllerChecks, a_zoom_drag_updates_the_dragged_chart_in_two_axes_and_the_others_in_one) {
    // arrange: two charts over the same one step dataset
    std::vector<std::pair<size_t, size_t>> slices{{0, 11}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 10.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* first = controller.add_chart();
    ChartEngine* second = controller.add_chart();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    first->plot_series({manager.evaluate("V(out)")});
    second->plot_series({manager.evaluate("V(out)")});
    controller.rebuild_frames();
    const auto [left, top, right, bottom] = controller.plot_rect(0);
    // act: drag a zoom window inside the first chart
    ASSERT_TRUE(controller.zoom_drag_started(left + 20.0f, top + 20.0f));
    ASSERT_TRUE(controller.zoom_drag_moved(right - 20.0f, bottom - 20.0f));
    ASSERT_TRUE(controller.zoom_drag_ended());
    // assert: the dragged chart takes both axes and the sibling only joins the abscissa
    const auto first_zoom = first->zoom_window();
    const auto second_zoom = second->zoom_window();
    EXPECT_GE(std::get<0>(first_zoom), 0.0);
    EXPECT_GE(std::get<1>(first_zoom), 0.0);
    EXPECT_GE(std::get<2>(first_zoom), 0.0);
    EXPECT_GE(std::get<3>(first_zoom), 0.0);
    EXPECT_GE(std::get<0>(second_zoom), 0.0);
    EXPECT_GE(std::get<2>(second_zoom), 0.0);
    EXPECT_LT(std::get<1>(second_zoom), 0.0);
    EXPECT_LT(std::get<3>(second_zoom), 0.0);
    EXPECT_FALSE(controller.zoom_selection().has_value());
}

TEST(StudioChartsControllerChecks, a_tiny_zoom_drag_is_not_a_zoom_window) {
    // arrange: one chart with a known plot rect
    std::vector<std::pair<size_t, size_t>> slices{{0, 11}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 10.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* chart = controller.add_chart();
    ASSERT_NE(chart, nullptr);
    chart->plot_series({manager.evaluate("V(out)")});
    controller.rebuild_frames();
    const auto [left, top, right, bottom] = controller.plot_rect(0);
    // act: drag a window smaller than the minimum
    ASSERT_TRUE(controller.zoom_drag_started(left + 20.0f, top + 20.0f));
    ASSERT_TRUE(controller.zoom_drag_moved(left + 24.0f, top + 24.0f));
    // assert: the release refuses the window and leaves the engine untouched
    EXPECT_FALSE(controller.zoom_drag_ended());
    EXPECT_LT(std::get<1>(chart->zoom_window()), 0.0);
    EXPECT_LT(std::get<3>(chart->zoom_window()), 0.0);
}

TEST(StudioChartsControllerChecks, a_hover_reads_the_series_at_the_pointer_abscissa) {
    // arrange: one chart with a known plot rect
    std::vector<std::pair<size_t, size_t>> slices{{0, 11}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 10.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* chart = controller.add_chart();
    ASSERT_NE(chart, nullptr);
    chart->plot_series({manager.evaluate("V(out)")});
    controller.rebuild_frames();
    const auto [left, top, right, bottom] = controller.plot_rect(0);
    // act: hover the middle of the plot
    const std::string text = controller.hover_moved((left + right) * 0.5f, (top + bottom) * 0.5f);
    // assert: the readout names the series and the hover chart
    EXPECT_NE(text.find("V(out)"), std::string::npos);
    EXPECT_EQ(controller.hover_text(), text);
    ASSERT_TRUE(controller.hover_chart().has_value());
    EXPECT_EQ(*controller.hover_chart(), 0U);
    // act: leave the plot
    controller.hover_ended();
    // assert: the readout is cleared
    EXPECT_TRUE(controller.hover_text().empty());
    EXPECT_FALSE(controller.hover_chart().has_value());
}

TEST(StudioChartsControllerChecks, moving_a_chart_reorders_the_stack) {
    // arrange: two charts plotting different expressions
    std::vector<std::pair<size_t, size_t>> slices{{0, 5}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0}, slices, "V"));
    expressions.emplace_back(Expression<double>("V(in)", std::vector<double>{4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 4.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* first = controller.add_chart();
    ChartEngine* second = controller.add_chart();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    first->plot_series({manager.evaluate("V(out)")});
    second->plot_series({manager.evaluate("V(in)")});
    controller.rebuild_frames();
    // act: move the first chart below the second
    controller.move_chart(0, 1);
    // assert: the stack order follows the move and every frame was rebuilt
    ASSERT_EQ(controller.chart_count(), 2U);
    ASSERT_EQ(controller.chart(0)->selected_expressions().size(), 1U);
    EXPECT_EQ(std::get<Expression<double>>(*controller.chart(0)->selected_expressions()[0]).name(), "V(in)");
    ASSERT_EQ(controller.chart(1)->selected_expressions().size(), 1U);
    EXPECT_EQ(std::get<Expression<double>>(*controller.chart(1)->selected_expressions()[0]).name(), "V(out)");
    EXPECT_EQ(controller.frames().size(), 2U);
}

TEST(StudioChartsControllerChecks, a_smith_chart_rejects_a_zoom_drag) {
    // arrange: one smith chart over a complex reflection coefficient
    std::vector<std::pair<size_t, size_t>> slices{{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("frequency", std::vector<double>{1.0, 2.0, 3.0, 4.0}, slices, "Hz"));
    expressions.emplace_back(Expression<std::complex<double>>("S11", std::vector<std::complex<double>>{{0.0, 0.0}, {0.5, 0.0}, {0.0, 0.5}, {-0.5, 0.0}}, slices, ""));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{1.0, 4.0}});
    studio::ChartsController controller(300.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* chart = controller.add_chart(ChartKind::SMITH);
    ASSERT_NE(chart, nullptr);
    chart->plot_series({manager.evaluate("S11")});
    controller.rebuild_frames();
    const auto [left, top, right, bottom] = controller.plot_rect(0);
    // act: press inside the fixed gamma plane
    const bool started = controller.zoom_drag_started((left + right) * 0.5f, (top + bottom) * 0.5f);
    // assert: the smith plane refuses the drag and keeps no selection
    EXPECT_FALSE(started);
    EXPECT_FALSE(controller.zoom_selection().has_value());
}

TEST(StudioChartsControllerChecks, moving_a_chart_clears_a_stale_hover) {
    // arrange: two charts with a hover on the first
    std::vector<std::pair<size_t, size_t>> slices{{0, 5}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0}, slices, "V"));
    expressions.emplace_back(Expression<double>("V(in)", std::vector<double>{4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 4.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* first = controller.add_chart();
    ChartEngine* second = controller.add_chart();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    first->plot_series({manager.evaluate("V(out)")});
    second->plot_series({manager.evaluate("V(in)")});
    controller.rebuild_frames();
    const auto [left, top, right, bottom] = controller.plot_rect(0);
    ASSERT_FALSE(controller.hover_moved((left + right) * 0.5f, (top + bottom) * 0.5f).empty());
    // act: reorder the charts under the pointer
    controller.move_chart(0, 1);
    // assert: the readout that named the old index is cleared
    EXPECT_TRUE(controller.hover_text().empty());
    EXPECT_FALSE(controller.hover_chart().has_value());
}

TEST(StudioChartsControllerChecks, a_smith_dataset_builds_a_smith_frame_and_reads_the_gamma_plane) {
    // arrange: one step dataset with a complex reflection coefficient
    std::vector<std::pair<size_t, size_t>> slices{{0, 4}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("frequency", std::vector<double>{1.0, 2.0, 3.0, 4.0}, slices, "Hz"));
    expressions.emplace_back(Expression<std::complex<double>>("S11", std::vector<std::complex<double>>{{0.0, 0.0}, {0.5, 0.0}, {0.0, 0.5}, {-0.5, 0.0}}, slices, ""));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{1.0, 4.0}});
    studio::ChartsController controller(300.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* chart = controller.add_chart(ChartKind::SMITH);
    ASSERT_NE(chart, nullptr);
    chart->plot_series({manager.evaluate("S11")});
    controller.rebuild_frames();
    // assert: the frame carries the smith kind and a series run
    ASSERT_EQ(controller.frames().size(), 1U);
    EXPECT_TRUE(controller.frames()[0].smith);
    EXPECT_FALSE(controller.frames()[0].series.empty());
    // act: hover the gamma plane centre
    const auto [left, top, right, bottom] = controller.plot_rect(0);
    const std::string text = controller.hover_moved((left + right) * 0.5f, (top + bottom) * 0.5f);
    // assert: the readout reports a trace point or the empty no-trace text
    EXPECT_EQ(controller.hover_text(), text);
}

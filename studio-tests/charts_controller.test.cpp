#include <complex>
#include <cstddef>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "charts/chart_style.h"
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

TEST(StudioChartsControllerChecks, update_dataset_repoints_the_charts_and_keeps_the_zoom_window) {
    // arrange: a chart with a zoom window over the first dataset
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
    ASSERT_TRUE(controller.zoom_drag_started(left + 20.0f, top + 20.0f));
    ASSERT_TRUE(controller.zoom_drag_moved(right - 20.0f, bottom - 20.0f));
    ASSERT_TRUE(controller.zoom_drag_ended());
    const std::tuple<double, double, double, double> zoom = chart->zoom_window();
    // arrange: a replacement dataset with the same expression names
    std::vector<std::pair<size_t, size_t>> replacement_slices{{0, 11}};
    std::vector<AnyExpression> replacement_expressions;
    replacement_expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}, replacement_slices, "s"));
    replacement_expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0}, replacement_slices, "V"));
    ExpressionManager replacement_manager(replacement_expressions, replacement_slices);
    // act: repoint the controller at the replacement
    controller.update_dataset(replacement_manager, steps, AbscissaScale::LINEAR);
    // assert: the chart kept its window and re-plotted the same expression against the new data
    ASSERT_EQ(controller.chart_count(), 1U);
    EXPECT_EQ(chart->zoom_window(), zoom);
    ASSERT_EQ(controller.frames().size(), 1U);
    EXPECT_FALSE(controller.frames().front().series.empty());
}

TEST(StudioChartsControllerChecks, the_zoom_commands_reset_the_axes_the_context_menu_names) {
    // arrange: two charts zoomed into the same abscissa window
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
    ASSERT_TRUE(controller.zoom_drag_started(left + 20.0f, top + 20.0f));
    ASSERT_TRUE(controller.zoom_drag_moved(right - 20.0f, bottom - 20.0f));
    ASSERT_TRUE(controller.zoom_drag_ended());
    // act: fit the second chart, which resets both axes there and the abscissa of the first
    controller.zoom_to_fit(1);
    // assert: the targeted chart keeps no window while its sibling keeps the ordinate
    EXPECT_LT(std::get<0>(second->zoom_window()), 0.0);
    EXPECT_LT(std::get<2>(second->zoom_window()), 0.0);
    EXPECT_LT(std::get<0>(first->zoom_window()), 0.0);
    EXPECT_GE(std::get<1>(first->zoom_window()), 0.0);
    // arrange: a fresh two axis window on the first chart
    ASSERT_TRUE(controller.zoom_drag_started(left + 20.0f, top + 20.0f));
    ASSERT_TRUE(controller.zoom_drag_moved(right - 20.0f, bottom - 20.0f));
    ASSERT_TRUE(controller.zoom_drag_ended());
    // act: autorange the first chart
    controller.autorange(0);
    // assert: only the ordinate reset, because the abscissa belongs to the whole panel
    EXPECT_LT(std::get<1>(first->zoom_window()), 0.0);
    EXPECT_GE(std::get<0>(first->zoom_window()), 0.0);
    // act: reset the shared abscissa extent
    controller.zoom_abscissa_extent();
    // assert: every chart dropped its horizontal window while the ordinate stays per chart
    EXPECT_LT(std::get<0>(first->zoom_window()), 0.0);
    EXPECT_LT(std::get<0>(second->zoom_window()), 0.0);
}

TEST(StudioChartsControllerChecks, the_chart_stack_commands_insert_and_delete_around_the_target) {
    // arrange: two charts over the same dataset
    std::vector<std::pair<size_t, size_t>> slices{{0, 11}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 10.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* first = controller.add_chart();
    ASSERT_NE(first, nullptr);
    first->plot_series({manager.evaluate("V(out)")});
    controller.rebuild_frames();
    // act: insert an empty chart after the first one
    ChartEngine* inserted = controller.insert_chart(0);
    // assert: the new chart sits at the second position and carries no series
    ASSERT_NE(inserted, nullptr);
    ASSERT_EQ(controller.chart_count(), 2U);
    EXPECT_EQ(controller.chart(1), inserted);
    ASSERT_EQ(controller.frames().size(), 2U);
    EXPECT_TRUE(controller.frames()[1].series.empty());
    // act: delete the inserted chart
    controller.delete_chart(1);
    // assert: the original chart remains
    ASSERT_EQ(controller.chart_count(), 1U);
    EXPECT_EQ(controller.chart(0), first);
    // act: delete the last chart
    controller.delete_chart(0);
    // assert: the panel keeps one replacement chart with no series
    ASSERT_EQ(controller.chart_count(), 1U);
    EXPECT_TRUE(controller.frames().front().series.empty());
    // act: delete the replacement with a smith kind
    controller.delete_chart(0, ChartKind::SMITH);
    // assert: the replacement keeps the dataset kind
    ASSERT_EQ(controller.chart_count(), 1U);
    EXPECT_EQ(controller.frames().front().smith, true);
}

TEST(StudioChartsControllerChecks, clearing_a_chart_drops_only_its_series) {
    // arrange: two charts plotting the same expression
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
    // act: clear the first chart
    controller.clear_chart(0);
    // assert: only the first chart lost its series
    ASSERT_EQ(controller.chart_count(), 2U);
    EXPECT_TRUE(controller.frames()[0].series.empty());
    EXPECT_FALSE(controller.frames()[1].series.empty());
}

TEST(StudioChartsControllerChecks, a_chart_added_after_a_zoom_joins_the_shared_abscissa) {
    // arrange: one chart zoomed into the middle of the abscissa
    std::vector<std::pair<size_t, size_t>> slices{{0, 11}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 10.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* first = controller.add_chart();
    ASSERT_NE(first, nullptr);
    first->plot_series({manager.evaluate("V(out)")});
    controller.rebuild_frames();
    const auto [left, top, right, bottom] = controller.plot_rect(0);
    ASSERT_TRUE(controller.zoom_drag_started(left + 20.0f, top + 20.0f));
    ASSERT_TRUE(controller.zoom_drag_moved(right - 20.0f, bottom - 20.0f));
    ASSERT_TRUE(controller.zoom_drag_ended());
    // act: add a chart after the zoomed one
    ChartEngine* added = controller.insert_chart(0);
    // assert: the new chart shares the horizontal window and keeps its full ordinate
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(std::get<0>(added->zoom_window()), std::get<0>(first->zoom_window()));
    EXPECT_EQ(std::get<2>(added->zoom_window()), std::get<2>(first->zoom_window()));
    EXPECT_LT(std::get<1>(added->zoom_window()), 0.0);
    EXPECT_LT(std::get<3>(added->zoom_window()), 0.0);
}

TEST(StudioChartsControllerChecks, one_expression_keeps_one_color_across_the_dataset) {
    // arrange: two charts that both plot the same voltage, one with an extra current
    std::vector<std::pair<size_t, size_t>> slices{{0, 11}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    expressions.emplace_back(Expression<double>("I(out)", std::vector<double>{0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.4, 0.3, 0.2, 0.1, 0.0}, slices, "A"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 10.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* volts = controller.add_chart();
    ChartEngine* volts_and_amps = controller.add_chart();
    ASSERT_NE(volts, nullptr);
    ASSERT_NE(volts_and_amps, nullptr);
    volts->plot_series({manager.evaluate("V(out)")});
    volts_and_amps->plot_series({manager.evaluate("V(out)"), manager.evaluate("I(out)")});
    // act: rebuild the stack
    controller.rebuild_frames();
    // assert: the voltage keeps one color in both charts and the current takes another
    ASSERT_EQ(controller.frames().size(), 2U);
    const ChartSeriesFrame* first_volts = nullptr;
    const ChartSeriesFrame* second_volts = nullptr;
    const ChartSeriesFrame* amps = nullptr;
    for (const ChartSeriesFrame& series : controller.frames()[0].series)
        if (series.name == "V(out)")
            first_volts = &series;
    for (const ChartSeriesFrame& series : controller.frames()[1].series) {
        if (series.name == "V(out)")
            second_volts = &series;
        if (series.name == "I(out)")
            amps = &series;
    }
    ASSERT_NE(first_volts, nullptr);
    ASSERT_NE(second_volts, nullptr);
    ASSERT_NE(amps, nullptr);
    EXPECT_FLOAT_EQ(first_volts->color.r, second_volts->color.r);
    EXPECT_FLOAT_EQ(first_volts->color.g, second_volts->color.g);
    EXPECT_FLOAT_EQ(first_volts->color.b, second_volts->color.b);
    EXPECT_NE(first_volts->color.r, amps->color.r);
    EXPECT_NE(first_volts->color.g, amps->color.g);
    EXPECT_NE(first_volts->color.b, amps->color.b);
}

TEST(StudioChartsControllerChecks, every_chart_shares_one_arranged_plot_rect) {
    // arrange: two charts whose ordinate magnitudes differ, so their labels differ too
    std::vector<std::pair<size_t, size_t>> slices{{0, 11}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}, slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0}, slices, "V"));
    expressions.emplace_back(Expression<double>("V(big)", std::vector<double>{0.0, 5000.0, 10000.0, 15000.0, 20000.0, 25000.0, 20000.0, 15000.0, 10000.0, 5000.0, 0.0}, slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 10.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* small = controller.add_chart();
    ChartEngine* large = controller.add_chart();
    ASSERT_NE(small, nullptr);
    ASSERT_NE(large, nullptr);
    small->plot_series({manager.evaluate("V(out)")});
    large->plot_series({manager.evaluate("V(big)")});
    // act: rebuild the shared stack
    controller.rebuild_frames();
    ASSERT_EQ(controller.frames().size(), 2U);
    const ChartFrame& first = controller.frames()[0];
    const ChartFrame& second = controller.frames()[1];
    // assert: both charts paint and hit test the same column layout
    EXPECT_FLOAT_EQ(first.plot_x, second.plot_x);
    EXPECT_FLOAT_EQ(first.plot_w, second.plot_w);
    EXPECT_FLOAT_EQ(first.axis_column_width, second.axis_column_width);
    EXPECT_GE(first.axis_column_width, 6.0f * 5.0f + CHART_LABEL_PADDING);
    // assert: the hit test rect keeps the arranged abscissa
    EXPECT_FLOAT_EQ(std::get<0>(controller.plot_rect(0)), first.plot_x);
    EXPECT_FLOAT_EQ(std::get<2>(controller.plot_rect(0)), first.plot_x + first.plot_w);
    // assert: every abscissa tick sits inside the shared range
    for (const ChartFrame& frame : controller.frames()) {
        for (const ChartTick& tick : frame.x_ticks) {
            EXPECT_GE(tick.pixel_pos, frame.plot_x - 0.01f);
            EXPECT_LE(tick.pixel_pos, frame.plot_x + frame.plot_w + 0.01f);
        }
    }
}

TEST(StudioChartsControllerChecks, the_step_selection_keeps_only_the_chosen_step) {
    // arrange: two steps of the same sweep
    std::vector<std::pair<size_t, size_t>> slices{{0, 11}, {11, 22}};
    std::vector<double> time;
    std::vector<double> volts;
    for (int step = 0; step < 2; ++step) {
        for (int index = 0; index < 11; ++index) {
            time.push_back(static_cast<double>(index));
            volts.push_back(static_cast<double>(index) + (step == 0 ? 0.0 : 100.0));
        }
    }
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("time", std::move(time), slices, "s"));
    expressions.emplace_back(Expression<double>("V(out)", std::move(volts), slices, "V"));
    ExpressionManager manager(expressions, slices);
    StepInformation steps({}, {}, {{0.0, 10.0}, {0.0, 10.0}});
    studio::ChartsController controller(400.0f, 300.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* chart = controller.add_chart();
    ASSERT_NE(chart, nullptr);
    chart->plot_series({manager.evaluate("V(out)")});
    controller.rebuild_frames();
    // assert: every step is the default selection
    ASSERT_EQ(controller.frames().size(), 1U);
    EXPECT_EQ(chart->selected_steps(), std::set<size_t>({0, 1}));
    ASSERT_FALSE(controller.frames()[0].series.empty());
    // act: keep only the second step
    controller.set_selected_steps(0, {1});
    // assert: the frame carries the second step's samples, which sit a hundred volts higher
    ASSERT_EQ(controller.frames().size(), 1U);
    ASSERT_FALSE(controller.frames()[0].series.empty());
    ASSERT_FALSE(controller.frames()[0].y_axes[0].ticks.empty());
    EXPECT_GT(controller.frames()[0].y_axes[0].ticks.back().value, 50.0);
    // act: clear the selection
    controller.set_selected_steps(0, {});
    // assert: an empty selection plots nothing
    ASSERT_EQ(controller.frames().size(), 1U);
    EXPECT_TRUE(controller.frames()[0].series.empty());
}

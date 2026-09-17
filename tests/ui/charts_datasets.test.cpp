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

TEST(ChartsDatasetsTest, update_creates_one_chart_per_suggested_plot_group) {
    // arrange — one expression manager with two plotted signals
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
    // act — activate a dataset suggesting two plot groups
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}, {"V(b)"}}, false);
    // assert — one chart per group, each chart holding only its own series
    ASSERT_EQ(renderer.chart_count(), 2u);
    ASSERT_EQ(published.back().size(), 2u);
    const std::vector<AnyExpression*> first = renderer.chart_selected_expressions(0);
    const std::vector<AnyExpression*> second = renderer.chart_selected_expressions(1);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], expression_manager.expressions()[1]);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0], expression_manager.expressions()[2]);
}

TEST(ChartsDatasetsTest, update_without_suggested_plots_creates_a_single_empty_chart) {
    // arrange — one expression manager with a plotted signal
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
    // act — activate a dataset without suggested plots
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {}, false);
    // assert — a single empty chart is created
    ASSERT_EQ(renderer.chart_count(), 1u);
    ASSERT_EQ(published.back().size(), 1u);
    EXPECT_TRUE(renderer.chart_selected_expressions(0).empty());
}

TEST(ChartsDatasetsTest, update_skips_suggested_plot_names_missing_from_the_expression_manager) {
    // arrange — one expression manager that knows only V(a)
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
    // act — activate with a suggested group whose name does not resolve
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(missing)"}}, false);
    // assert — the chart is still created but stays empty instead of crashing
    ASSERT_EQ(renderer.chart_count(), 1u);
    ASSERT_EQ(published.back().size(), 1u);
    EXPECT_TRUE(renderer.chart_selected_expressions(0).empty());
}

TEST(ChartsDatasetsTest, switching_plot_tabs_preserves_series_and_zoom_per_tab) {
    // arrange — dataset 1 with a plotted signal and a zoom applied to the panel
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
    renderer.add_chart();
    renderer.publish_frames();
    ASSERT_EQ(published.back().size(), 2u);
    // arrange — capture the pre-zoom frames, then zoom on the first chart
    const ChartFrame pre = published.back()[0];
    renderer.zoom_drag_started(pre.plot_x + pre.plot_w * 0.1f, pre.plot_y + pre.plot_h * 0.1f);
    renderer.zoom_drag_moved(pre.plot_x + pre.plot_w * 0.8f, pre.plot_y + pre.plot_h * 0.8f);
    renderer.zoom_drag_ended();
    const ChartFrame zoomed = published.back()[0];
    // act — switch to a second tab, then back to the first one
    renderer.update(2, expression_manager, step_information, AbscissaScale::LINEAR, {}, false);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {}, false);
    // assert — the second tab got its own single empty chart while the first
    // tab restored its two charts, their series and the zoomed abscissa range
    ASSERT_EQ(renderer.chart_count(), 2u);
    ASSERT_EQ(published.back().size(), 2u);
    const std::vector<AnyExpression*> first = renderer.chart_selected_expressions(0);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], expression_manager.expressions()[1]);
    ASSERT_EQ(published.back()[0].x_ticks.size(), zoomed.x_ticks.size());
    for (size_t t = 0; t < zoomed.x_ticks.size(); ++t)
        EXPECT_EQ(published.back()[0].x_ticks[t].value, zoomed.x_ticks[t].value);
    EXPECT_NE(zoomed.x_ticks.size(), pre.x_ticks.size());
}

TEST(ChartsDatasetsTest, re_running_a_simulation_repoints_the_charts_to_the_new_file) {
    // arrange — dataset 1 shows a plotted signal from the first file
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> first_data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> first_expressions;
    first_expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
    first_expressions.emplace_back(Expression<double>("V(out)", std::move(first_data), step_slices, "V"));
    ExpressionManager first_manager(first_expressions, step_slices);
    StepInformation step_information({"time"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, first_manager, step_information, AbscissaScale::LINEAR, {{"V(out)"}}, false);
    ASSERT_EQ(renderer.chart_count(), 1u);
    // arrange — a second file with the same signal names but different data
    std::vector<double> second_abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> second_data = {2.0, 4.0, 6.0, 8.0, 10.0, 12.0};
    std::vector<std::pair<size_t, size_t>> second_slices = {{0, 6}};
    std::vector<AnyExpression> second_expressions;
    second_expressions.emplace_back(Expression<double>("time", std::move(second_abscissa_data), second_slices, "s"));
    second_expressions.emplace_back(Expression<double>("V(out)", std::move(second_data), second_slices, "V"));
    ExpressionManager second_manager(second_expressions, second_slices);
    // act — activate the same tab with the replaced file
    renderer.update(1, second_manager, step_information, AbscissaScale::LINEAR, {}, false);
    // assert — the chart is reused (not recreated) and re-pointed to the new
    // expression: the series keeps its name and now references the new file
    ASSERT_EQ(renderer.chart_count(), 1u);
    ASSERT_EQ(published.back().size(), 1u);
    const std::vector<AnyExpression*> replotted = renderer.chart_selected_expressions(0);
    ASSERT_EQ(replotted.size(), 1u);
    EXPECT_EQ(replotted[0], second_manager.expressions()[1]);
    EXPECT_EQ(published.back()[0].legend.size(), 1u);
    EXPECT_EQ(published.back()[0].legend[0].name, "V(out)");
}

TEST(ChartsDatasetsTest, releasing_an_inactive_dataset_keeps_the_active_one) {
    // arrange — two tabs each with one chart, the second one active
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
    renderer.update(2, expression_manager, step_information, AbscissaScale::LINEAR, {}, false);
    // act — release the inactive first tab
    renderer.release_dataset(1);
    // assert — the active second tab is untouched
    ASSERT_EQ(renderer.chart_count(), 1u);
    ASSERT_EQ(published.back().size(), 1u);
    // act — release the active second tab
    renderer.release_dataset(2);
    // assert — no dataset is active anymore and no frames are shown
    EXPECT_EQ(renderer.chart_count(), 0u);
    EXPECT_TRUE(published.back().empty());
}

TEST(ChartsDatasetsTest, release_all_datasets_clears_every_dataset) {
    // arrange — two tabs each with one chart, the second one active
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
    renderer.update(2, expression_manager, step_information, AbscissaScale::LINEAR, {}, false);
    // act — release every dataset at once
    renderer.release_all_datasets();
    // assert — no dataset is active anymore and no frames are shown
    EXPECT_EQ(renderer.chart_count(), 0u);
    EXPECT_TRUE(published.back().empty());
}

TEST(ChartsDatasetsTest, delete_chart_removes_the_chart_at_the_position) {
    // arrange — two charts, the first plotting V(a), the second plotting V(b)
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
    // act — delete the first chart via its context menu position
    renderer.delete_chart(0.25f);
    // assert — the second chart moves up to the first slot
    ASSERT_EQ(renderer.chart_count(), 1u);
    ASSERT_EQ(published.back().size(), 1u);
    const std::vector<AnyExpression*> remaining = renderer.chart_selected_expressions(0);
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0], expression_manager.expressions()[2]);
}

TEST(ChartsDatasetsTest, delete_chart_keeps_a_blank_chart_when_the_last_one_is_removed) {
    // arrange — one chart plotting V(a)
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
    // act — delete the only chart
    renderer.delete_chart(0.5f);
    // assert — a blank chart replaces it so the panel is never empty
    ASSERT_EQ(renderer.chart_count(), 1u);
    ASSERT_EQ(published.back().size(), 1u);
    EXPECT_TRUE(renderer.chart_selected_expressions(0).empty());
    EXPECT_TRUE(published.back()[0].legend.empty());
}

TEST(ChartsDatasetsTest, delete_chart_without_an_active_dataset_is_a_safe_noop) {
    // arrange — a renderer without a dataset
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    // act — delete a chart via a context menu position without any dataset
    renderer.delete_chart(0.5f);
    // assert — nothing was published and nothing crashed
    EXPECT_TRUE(published.empty());
    EXPECT_EQ(renderer.chart_count(), 0u);
}

TEST(ChartsDatasetsTest, delete_all_plots_clears_only_the_target_chart) {
    // arrange — two charts, the first plotting V(a), the second plotting V(b)
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
    // act — delete all plots on the second chart via its context menu position
    renderer.delete_all_plots(0.75f);
    // assert — only the second chart was cleared
    renderer.publish_frames();
    ASSERT_EQ(published.back().size(), 2u);
    const std::vector<AnyExpression*> first = renderer.chart_selected_expressions(0);
    const std::vector<AnyExpression*> second = renderer.chart_selected_expressions(1);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], expression_manager.expressions()[1]);
    EXPECT_TRUE(second.empty());
}

TEST(ChartsDatasetsTest, position_to_index_translates_relative_positions_to_chart_indexes) {
    // arrange — a renderer without a dataset translates everything to index 0
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
    EXPECT_EQ(renderer.position_to_index(0.0f), 0u);
    EXPECT_EQ(renderer.position_to_index(1.0f), 0u);
    // act — activate a panel with two charts and translate boundary positions
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}}, false);
    renderer.add_chart();
    // assert — positions map to the upper slot from 0.5 on and clamp at both ends
    EXPECT_EQ(renderer.position_to_index(0.0f), 0u);
    EXPECT_EQ(renderer.position_to_index(0.25f), 0u);
    EXPECT_EQ(renderer.position_to_index(0.5f), 1u);
    EXPECT_EQ(renderer.position_to_index(1.0f), 1u);
    EXPECT_EQ(renderer.position_to_index(-0.25f), 0u);
    EXPECT_EQ(renderer.position_to_index(1.5f), 1u);
    // act — with three charts the middle slot starts at 1/3
    renderer.add_chart();
    EXPECT_EQ(renderer.position_to_index(0.0f), 0u);
    EXPECT_EQ(renderer.position_to_index(0.34f), 1u);
    EXPECT_EQ(renderer.position_to_index(0.67f), 2u);
    EXPECT_EQ(renderer.position_to_index(1.0f), 2u);
}

TEST(ChartsDatasetsTest, chart_selected_steps_roundtrips_through_the_renderer) {
    // arrange — one dataset with one chart
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
    // act — replace the step selection of the first chart; the loaded file has
    // a single step (slice 0) so the selection is limited to that step
    renderer.set_chart_selected_steps(0, {0});
    // assert — the selection is readable again through the renderer
    EXPECT_EQ(renderer.chart_selected_steps(0), std::set<size_t>({0}));
    // act — clear the selection again
    renderer.set_chart_selected_steps(0, {});
    // assert — an empty selection reads back as empty
    EXPECT_TRUE(renderer.chart_selected_steps(0).empty());
    // an out of range index reads empty and writing to it is a no-op
    EXPECT_TRUE(renderer.chart_selected_steps(5).empty());
    renderer.set_chart_selected_steps(5, {0});
    EXPECT_TRUE(renderer.chart_selected_steps(0).empty());
}

TEST(ChartsDatasetsTest, abscissa_range_reports_the_loaded_step_information) {
    // arrange — a renderer without a loaded dataset falls back to [0, 1]
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
    const auto fallback = renderer.abscissa_range();
    EXPECT_EQ(fallback.first, 0.0);
    EXPECT_EQ(fallback.second, 1.0);
    // act — load a dataset whose abscissa spans [0, 5]
    renderer.set_viewport(800.0f, 600.0f);
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}}, false);
    // assert — the loaded step information abscissa range is reported
    const auto range = renderer.abscissa_range();
    EXPECT_EQ(range.first, 0.0);
    EXPECT_EQ(range.second, 5.0);
}

TEST(ChartsDatasetsTest, evaluate_expression_evaluates_through_the_expression_manager) {
    // arrange — one dataset with one chart and three known expressions
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
    // act — evaluate the known name through the renderer
    AnyExpression* resolved = renderer.evaluate_expression("V(a)");
    // assert — it resolves to the same expression pointer the manager holds
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved, expression_manager.expressions()[1]);
    // repeated evaluation returns the cached expression instead of a new one
    EXPECT_EQ(renderer.evaluate_expression("V(a)"), resolved);
    // every expression known to the manager is listed
    EXPECT_EQ(renderer.all_expressions().size(), expression_manager.expressions().size());
}

TEST(ChartsDatasetsTest, smith_dataset_flag_follows_the_active_dataset) {
    // arrange — one renderer with an XY dataset and a complex expression manager
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::complex<double>> s11_data(6, std::complex<double>(0.5, 0.1));
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("frequency", std::move(abscissa_data), step_slices, "Hz"));
    expressions.emplace_back(Expression<std::complex<double>>("S11", std::move(s11_data), step_slices, "", "", "parameter"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"frequency"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    // act — activate an xy dataset and a smith dataset
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {}, false);
    const bool xy_smith = renderer.active_dataset_is_smith();
    renderer.update(2, expression_manager, step_information, AbscissaScale::LINEAR, {}, true);
    const bool smith_smith = renderer.active_dataset_is_smith();
    // assert — the flag reflects the active dataset; releasing the active
    // smith dataset reports the default (non-smith) state
    EXPECT_FALSE(xy_smith);
    EXPECT_TRUE(smith_smith);
    renderer.release_dataset(2);
    EXPECT_FALSE(renderer.active_dataset_is_smith());
}

TEST(ChartsDatasetsTest, smith_dataset_creates_smith_kind_charts) {
    // arrange — one renderer activated as a smith dataset
    std::vector<double> abscissa_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::complex<double>> s11_data(6, std::complex<double>(0.5, 0.1));
    std::vector<std::pair<size_t, size_t>> step_slices = {{0, 6}};
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("frequency", std::move(abscissa_data), step_slices, "Hz"));
    expressions.emplace_back(Expression<std::complex<double>>("S11", std::move(s11_data), step_slices, "", "", "parameter"));
    ExpressionManager expression_manager(expressions, step_slices);
    StepInformation step_information({"frequency"}, {{}}, {{0.0, 5.0}});
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    // act
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"S11"}}, true);
    // assert — the published frames carry the smith flag, the grid paths and
    // the two axis diameters
    ASSERT_EQ(published.back().size(), 1u);
    EXPECT_TRUE(published.back().front().smith);
    EXPECT_EQ(published.back().front().smith_grid.size(), 18u);
    // the frame carries the smith tick labels
    EXPECT_EQ(published.back().front().smith_labels.size(), 15u);
    // the series carries the plotted gamma trace
    ASSERT_EQ(published.back().front().series.size(), 1u);
    EXPECT_EQ(published.back().front().series.front().name, "S11");
}

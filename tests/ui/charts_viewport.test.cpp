#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "charts/chart_engine.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "ui/charts_renderer.h"

TEST(ChartsViewportTest, set_viewport_ignores_degenerate_sizes_and_noop_updates) {
    // arrange — one dataset with one chart and a valid viewport
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
    // the initial viewport and the dataset activation each publish once
    ASSERT_EQ(published.size(), 2u);
    EXPECT_EQ(published.back().size(), 1u);
    // act — repeated identical viewport updates are ignored
    renderer.set_viewport(800.0f, 600.0f);
    // degenerate viewports are rejected and publish nothing
    renderer.set_viewport(0.0f, 600.0f);
    renderer.set_viewport(800.0f, -1.0f);
    // a real resize republishes the frames for the new geometry
    renderer.set_viewport(400.0f, 300.0f);
    // assert — exactly one additional publish happened, for the resize
    ASSERT_EQ(published.size(), 3u);
    EXPECT_EQ(published.back().size(), 1u);
}

TEST(ChartsViewportTest, frames_are_not_published_without_a_valid_viewport) {
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
    // act — load a dataset before any viewport is known
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}});
    // assert — the charts exist but nothing was published to the hidden panel
    EXPECT_EQ(renderer.chart_count(), 1u);
    EXPECT_TRUE(published.empty());
    // act — the panel becomes visible
    renderer.set_viewport(800.0f, 600.0f);
    // assert — the frames are published now
    ASSERT_EQ(published.size(), 1u);
    EXPECT_EQ(published.back().size(), 1u);
    // act — the panel is hidden again; explicit publishes stay suppressed
    renderer.reset_viewport();
    renderer.publish_frames();
    // assert — no additional publish reached the sink
    EXPECT_EQ(published.size(), 1u);
    // act — the panel becomes visible again with the same size
    renderer.set_viewport(800.0f, 600.0f);
    // assert — the zeroed viewport makes the same size a real update
    ASSERT_EQ(published.size(), 2u);
    EXPECT_EQ(published.back().size(), 1u);
}

TEST(ChartsViewportTest, publishing_without_an_active_dataset_produces_no_frames) {
    // arrange — a renderer with a valid viewport but no dataset
    std::vector<std::vector<ChartFrame>> published;
    ChartsRenderer renderer([&published](const std::vector<ChartFrame>& frames) { published.push_back(frames); });
    renderer.set_viewport(800.0f, 600.0f);
    // act — publish frames
    renderer.publish_frames();
    // assert — the sink received empty frame lists (from the viewport update
    // and the explicit publish) instead of crashing
    ASSERT_EQ(published.size(), 2u);
    EXPECT_TRUE(published.back().empty());
}

TEST(ChartsViewportTest, set_dark_mode_republishes_the_frames_only_on_change) {
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
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(a)"}});
    ASSERT_EQ(published.size(), 2u);
    // act — switch to dark mode, repeat it, then switch back
    renderer.set_dark_mode(true);
    renderer.set_dark_mode(true);
    renderer.set_dark_mode(false);
    // assert — the palette change republished once per real change only
    ASSERT_EQ(published.size(), 4u);
    EXPECT_EQ(published.back().size(), 1u);
}

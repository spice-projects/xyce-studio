#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "charts/chart_engine.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "ui/charts_renderer.h"

TEST(ChartsLifecycleTest, reset_viewport_clears_the_active_hover_readout) {
    // arrange — a renderer showing a plotted signal with a captured hover readout
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
    renderer.update(1, expression_manager, step_information, AbscissaScale::LINEAR, {{"V(out)"}}, false);
    ASSERT_EQ(published.back().size(), 1u);
    // arrange — capture the hover readout text
    std::string hover_text;
    renderer.set_hover_callback([&hover_text](const std::string& text) { hover_text = text; });
    // arrange — schedule the event loop shutdown after the hover debounce fires
    slint::Timer stopper;
    stopper.start(slint::TimerMode::SingleShot, std::chrono::milliseconds(150), [] { slint::quit_event_loop(); });
    // act — hover inside the first chart plot rect; the readout is published
    // through the debounce timer while the event loop runs
    const ChartFrame frame = published.back()[0];
    renderer.hover_moved(frame.plot_x + frame.plot_w * 0.5f, frame.plot_y + frame.plot_h * 0.5f);
    slint::run_event_loop();
    // assert — the readout was published for the plotted series
    ASSERT_NE(hover_text.find("V(out)"), std::string::npos);
    // act — hide the charts panel
    renderer.reset_viewport();
    // assert — the readout was cleared synchronously so the status bar no
    // longer shows chart text while the panel is hidden
    EXPECT_TRUE(hover_text.empty());
}

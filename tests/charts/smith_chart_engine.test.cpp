#include <cmath>
#include <complex>
#include <vector>

#include <gtest/gtest.h>

#include "charts/chart_layout.h"
#include "charts/smith_chart.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"

namespace
{
    // build a smith chart engine over a frequency-swept complex expression
    struct SmithFixture
    {
        std::vector<double> abscissa_data;
        std::vector<std::complex<double>> s11_data;
        std::vector<std::pair<size_t, size_t>> step_slices;
        std::vector<AnyExpression> expressions;
        std::unique_ptr<ExpressionManager> expression_manager;
        StepInformation step_information;
        std::unique_ptr<ChartEngine> engine;

        explicit SmithFixture(std::vector<std::complex<double>> s11) :
            s11_data(std::move(s11)),
            step_slices{{0, s11_data.size()}},
            step_information({"frequency"}, {{}}, {{1.0, 2.0}}) {
            // abscissa frequencies
            std::vector<double> abscissa_data(s11_data.size());
            for (size_t i = 0; i < abscissa_data.size(); ++i)
                abscissa_data[i] = static_cast<double>(i + 1);
            // frequency abscissa and the complex s11 expression
            std::unordered_map<std::string, std::string> s11_metadata{{"reference_impedance", "50.000000"}};
            expressions.emplace_back(Expression<double>("frequency", std::move(abscissa_data), step_slices, "Hz"));
            expressions.emplace_back(Expression<std::complex<double>>("S11", std::move(s11_data), step_slices, "", "", "parameter", std::vector<std::unordered_map<std::string, std::string>>{std::move(s11_metadata)}));
            expression_manager = std::make_unique<ExpressionManager>(expressions, step_slices);
            engine = std::make_unique<ChartEngine>(expression_manager.get(), &step_information, AbscissaScale::LINEAR, 1000, ChartKind::SMITH);
        }
    };
} // namespace

TEST(SmithChartEngineChecks, complex_expressions_plot_as_gamma_traces) {
    // arrange — s11 values (0.5, 0.1) and (0.2, 0.0)
    SmithFixture fixture({std::complex<double>(0.5, 0.1), std::complex<double>(0.2, 0.0)});
    // act
    fixture.engine->plot_series({fixture.expression_manager->expressions()[1]});
    // assert — one series keyed by the expression name
    ASSERT_EQ(fixture.engine->series().size(), 1u);
    const auto& ordinate_series = fixture.engine->series().at("S11");
    // rendered views carry the gamma-plane coordinates (gamma = S for s-parameters)
    const auto& views = std::get<2>(ordinate_series).begin()->second;
    const auto& [gamma_r, gamma_i] = views;
    ASSERT_EQ(gamma_r.size(), 2u);
    EXPECT_DOUBLE_EQ(gamma_r[0], 0.5);
    EXPECT_DOUBLE_EQ(gamma_i[0], 0.1);
    EXPECT_DOUBLE_EQ(gamma_r[1], 0.2);
    EXPECT_DOUBLE_EQ(gamma_i[1], 0.0);
    // no y axis is assigned on smith charts
    EXPECT_EQ(std::get<1>(ordinate_series), -1);
}

TEST(SmithChartEngineChecks, real_expressions_are_rejected_on_smith_charts) {
    // arrange — a smith engine with an extra real expression
    SmithFixture fixture({std::complex<double>(0.5, 0.1)});
    // act — the frequency expression is real and cannot map to the gamma plane
    fixture.engine->plot_series({fixture.expression_manager->expressions()[0], fixture.expression_manager->expressions()[1]});
    // assert — only the complex expression produced a series
    ASSERT_EQ(fixture.engine->series().size(), 1u);
    EXPECT_EQ(fixture.engine->series().count("frequency"), 0u);
}

TEST(SmithChartEngineChecks, smith_charts_ignore_zoom_windows) {
    // arrange — a smith chart with one plotted series
    SmithFixture fixture({std::complex<double>(0.5, 0.1), std::complex<double>(0.2, 0.0)});
    fixture.engine->plot_series({fixture.expression_manager->expressions()[1]});
    // act — apply a zoom window; the smith plane is fixed
    fixture.engine->update_zoom_window(0.25, 0.75, 0.25, 0.75);
    fixture.engine->reset_zoom_window(true, true);
    // assert — the rendered gamma views are unchanged (all samples, no zoom slice)
    const auto& views = std::get<2>(fixture.engine->series().at("S11")).begin()->second;
    EXPECT_EQ(std::get<0>(views).size(), 2u);
}

TEST(SmithChartEngineChecks, hover_reports_the_nearest_trace_point) {
    // arrange — a smith chart with one plotted series
    SmithFixture fixture({std::complex<double>(0.5, 0.1), std::complex<double>(0.2, 0.0)});
    fixture.engine->plot_series({fixture.expression_manager->expressions()[1]});
    // act — hover exactly on the first sample
    const std::string text = fixture.engine->hovered_smith_text(0.5, 0.1);
    // assert — the readout carries the frequency, series name, reflection
    // coefficient, impedance, vswr and return loss
    EXPECT_NE(text.find("frequency=1"), std::string::npos);
    EXPECT_NE(text.find("S11"), std::string::npos);
    EXPECT_NE(text.find("|Γ|"), std::string::npos);
    EXPECT_NE(text.find("Z="), std::string::npos);
    EXPECT_NE(text.find("VSWR="), std::string::npos);
    EXPECT_NE(text.find("RL="), std::string::npos);
}

TEST(SmithChartEngineChecks, hover_away_from_any_trace_returns_empty) {
    // arrange — a smith chart with one plotted series at gamma (0.2, 0)
    SmithFixture fixture({std::complex<double>(0.2, 0.0)});
    fixture.engine->plot_series({fixture.expression_manager->expressions()[1]});
    // act — hover far away from the trace inside the plane
    const std::string text = fixture.engine->hovered_smith_text(-0.8, -0.8);
    // assert — no trace point near the cursor, empty readout
    EXPECT_TRUE(text.empty());
}

TEST(SmithChartLayoutChecks, smith_frames_use_a_square_plot_rect) {
    // arrange
    SmithFixture fixture({std::complex<double>(0.5, 0.1)});
    fixture.engine->plot_series({fixture.expression_manager->expressions()[1]});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act — a wide frame
    const ChartFrame frame = layout.build(*fixture.engine, 800.0f, 600.0f);
    // assert — the plot rect is square and centered horizontally
    EXPECT_TRUE(frame.smith);
    EXPECT_NEAR(frame.plot_w, frame.plot_h, 1e-6);
    EXPECT_NEAR(frame.plot_x, (800.0f - frame.plot_w) * 0.5f, 1e-6);
    // the legend reserves space below the plot
    ASSERT_EQ(frame.legend.size(), 1u);
    EXPECT_NEAR(frame.legend_y + frame.legend_h, 600.0f - 10.0f, 1e-6);
}

TEST(SmithChartLayoutChecks, smith_frames_carry_the_grid_paths) {
    // arrange
    SmithFixture fixture({std::complex<double>(0.5, 0.1)});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(*fixture.engine, 800.0f, 600.0f);
    // assert — the standard grid: the unit circle boundary plus the resistance
    // and reactance paths, all flagged for theme color substitution
    ASSERT_EQ(frame.smith_grid.size(), 16u);
    EXPECT_TRUE(frame.smith_grid[0].boundary);
    for (size_t g = 1; g < frame.smith_grid.size(); ++g)
        EXPECT_FALSE(frame.smith_grid[g].boundary);
    // every grid sample maps inside the square plot rect
    for (const auto& run : frame.smith_grid) {
        for (const auto& point : run.points) {
            EXPECT_GE(point.x, frame.plot_x - 1e-6);
            EXPECT_LE(point.x, frame.plot_x + frame.plot_w + 1e-6);
            EXPECT_GE(point.y, frame.plot_y - 1e-6);
            EXPECT_LE(point.y, frame.plot_y + frame.plot_h + 1e-6);
        }
    }
    // the unit circle boundary touches the plot edges
    const auto& unit_circle = frame.smith_grid[0].points;
    ASSERT_GE(unit_circle.size(), 64u);
    const auto right = std::max_element(unit_circle.begin(), unit_circle.end(), [](const ChartPoint& a, const ChartPoint& b) { return a.x < b.x; });
    EXPECT_NEAR(right->x, frame.plot_x + frame.plot_w, 1e-3);
}

TEST(SmithChartLayoutChecks, smith_series_map_the_gamma_plane) {
    // arrange — s11 = (0.5, 0.1), (0.2, 0.0)
    SmithFixture fixture({std::complex<double>(0.5, 0.1), std::complex<double>(0.2, 0.0)});
    fixture.engine->plot_series({fixture.expression_manager->expressions()[1]});
    ChartLayout layout([](const std::string& text) { return static_cast<float>(text.size()) * 7.0f; });
    // act
    const ChartFrame frame = layout.build(*fixture.engine, 800.0f, 600.0f);
    // assert — one series run carrying both samples
    ASSERT_EQ(frame.series.size(), 1u);
    const ChartSeriesFrame& run = frame.series[0];
    ASSERT_EQ(run.name, "S11");
    ASSERT_EQ(run.points.size(), 2u);
    // gamma (0.5, 0.1) maps to the middle right of the plane
    const float expected_x = frame.plot_x + (0.5f + 1.0f) * 0.5f * frame.plot_w;
    const float expected_y = frame.plot_y + frame.plot_h - (0.1f + 1.0f) * 0.5f * frame.plot_h;
    EXPECT_NEAR(run.points[0].x, expected_x, 1e-3);
    EXPECT_NEAR(run.points[0].y, expected_y, 1e-3);
    // gamma (0.2, 0.0) maps to its matching position
    const float expected_x2 = frame.plot_x + (0.2f + 1.0f) * 0.5f * frame.plot_w;
    EXPECT_NEAR(run.points[1].x, expected_x2, 1e-3);
}

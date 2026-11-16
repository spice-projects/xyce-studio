#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "albor/core/paint.h"
#include "albor/render/draw_command.h"
#include "albor/render/picture_resource.h"
#include "albor/render/shaped_run.h"
#include "albor/runtime/binding_engine.h"
#include "albor/text/text_engine.h"
#include "charts/chart_palette.h"
#include "charts/charts_controller.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "views/charts/chart_view.h"

namespace
{
    // the sample count every series of the benchmark carries
    constexpr std::size_t SAMPLE_COUNT = 4000;

    // the series count every chart of the benchmark plots
    constexpr std::size_t SERIES_COUNT = 4;

    // the text engine double shapes one glyph per character, which keeps the build measurable
    class FakeTextEngine final : public albor::TextEngine
    {
    public:
        albor::TextLayout measure(const std::string_view text, const float max_width) const override {
            // the unbounded measure delegates to the explicit size overload
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        albor::TextLayout measure(const std::string_view text, const float max_width, const float font_size) const override {
            // ignore the ceiling, because chart labels are single line
            (void)max_width;
            // build the line the run hands to the renderer
            albor::ShapedRun::Line line;
            // place the baseline below the run origin
            line.origin = albor::Point{.x = 0.0f, .y = font_size * 0.8f};
            // give the run one glyph per character so it is not empty
            for (std::size_t index = 0; index < text.size(); ++index)
                line.glyphs.push_back(albor::ShapedGlyph{.id = static_cast<std::uint32_t>(index), .position = albor::Point{.x = static_cast<float>(index) * 6.0f, .y = 0.0f}});
            // wrap the line in an immutable shaped run
            std::shared_ptr<const albor::ShapedRun> run = albor::ShapedRun::make({std::move(line)}, font_size);
            // return a deterministic run whose extent grows with the text
            return albor::TextLayout{.run = std::move(run), .size = albor::Size{.width = static_cast<float>(text.size()) * 6.0f, .height = font_size}, .ascent = font_size * 0.8f, .descent = font_size * 0.2f};
        }

        [[nodiscard]] bool ready() const override { return true; }
    };

    // the milliseconds one timed block took
    double elapsed_ms(const std::chrono::steady_clock::time_point start) {
        // the elapsed time in milliseconds
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
} // namespace

TEST(StudioChartsPerformanceChecks, the_chart_build_stays_within_its_debug_budget) {
    // arrange: a transient dataset with four series of four thousand samples
    std::vector<std::pair<std::size_t, std::size_t>> slices;
    // the samples of the time axis
    std::vector<double> time;
    // one turned dataset per chart count
    std::vector<std::vector<double>> series(SERIES_COUNT);
    // every sample of the time axis and of the series
    for (std::size_t sample = 0; sample < SAMPLE_COUNT; ++sample) {
        time.push_back(static_cast<double>(sample) * 1.0e-6);
        // every series carries its own deterministic waveform
        for (std::size_t index = 0; index < SERIES_COUNT; ++index)
            series[index].push_back(static_cast<double>((sample * (index + 1)) % 97));
    }
    // the dataset carries every sample in one step
    slices.emplace_back(0, SAMPLE_COUNT);
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the time axis
    expressions.emplace_back(Expression<double>("time", std::move(time), slices, "s"));
    // the series the charts plot
    for (std::size_t index = 0; index < SERIES_COUNT; ++index)
        expressions.emplace_back(Expression<double>("V(" + std::to_string(index + 1) + ")", std::move(series[index]), slices, "V"));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the dataset carries one step
    StepInformation steps({}, {}, {{0.0, static_cast<double>(SAMPLE_COUNT)}});
    // one chart count at a time
    for (const std::size_t chart_count : {1U, 4U, 10U}) {
        // the controller over the panel the shell arranges
        studio::ChartsController controller(1200.0f, 800.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
        controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
        // every chart plots the whole series set
        for (std::size_t index = 0; index < chart_count; ++index) {
            ChartEngine* chart = controller.add_chart();
            ASSERT_NE(chart, nullptr);
            chart->plot_series({manager.evaluate("V(1)"), manager.evaluate("V(2)"), manager.evaluate("V(3)"), manager.evaluate("V(4)")});
        }
        // act: time one whole rebuild, taken as the best of three blocks
        double best = 1.0e9;
        for (int block = 0; block < 3; ++block) {
            const auto start = std::chrono::steady_clock::now();
            controller.rebuild_frames();
            best = std::min(best, elapsed_ms(start));
        }
        // the per-chart cost the budget is stated against
        const double per_chart = best / static_cast<double>(chart_count);
        // record the measurement so a run reports what it measured
        RecordProperty("charts_" + std::to_string(chart_count) + "_per_chart_ms", std::to_string(per_chart));
        // assert: the build stays inside a debug-safe ceiling of the two millisecond release budget
        EXPECT_LT(per_chart, 8.0) << chart_count << " charts took " << per_chart << " ms per chart";
        // assert: the frames describe every chart
        EXPECT_EQ(controller.frames().size(), chart_count);
    }
}

TEST(StudioChartsPerformanceChecks, a_static_frame_replays_its_retained_layers) {
    // arrange: a view over one chart with four series of four thousand samples
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, SAMPLE_COUNT}};
    // the samples of the time axis
    std::vector<double> time;
    // one turned dataset per series
    std::vector<std::vector<double>> series(SERIES_COUNT);
    // every sample of the time axis and of the series
    for (std::size_t sample = 0; sample < SAMPLE_COUNT; ++sample) {
        time.push_back(static_cast<double>(sample) * 1.0e-6);
        // every series carries its own deterministic waveform
        for (std::size_t index = 0; index < SERIES_COUNT; ++index)
            series[index].push_back(static_cast<double>((sample * (index + 1)) % 97));
    }
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the time axis
    expressions.emplace_back(Expression<double>("time", std::move(time), slices, "s"));
    // the series the chart plots
    for (std::size_t index = 0; index < SERIES_COUNT; ++index)
        expressions.emplace_back(Expression<double>("V(" + std::to_string(index + 1) + ")", std::move(series[index]), slices, "V"));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the dataset carries one step
    StepInformation steps({}, {}, {{0.0, static_cast<double>(SAMPLE_COUNT)}});
    // the controller over the panel the shell arranges
    studio::ChartsController controller(1200.0f, 800.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* chart = controller.add_chart();
    ASSERT_NE(chart, nullptr);
    chart->plot_series({manager.evaluate("V(1)"), manager.evaluate("V(2)"), manager.evaluate("V(3)"), manager.evaluate("V(4)")});
    controller.rebuild_frames();
    // the view the shell keeps alive
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{}, albor::Size{.width = 1200.0f, .height = 800.0f}));
    view.set_frames(controller.frames());
    // act: paint the first frame, which records the layers
    albor::PictureRecorder first;
    const auto start = std::chrono::steady_clock::now();
    view.paint_content(first);
    const double first_ms = elapsed_ms(start);
    const std::size_t base_records = view.base_record_count();
    const std::size_t series_records = view.series_record_count();
    const std::size_t legend_records = view.legend_record_count();
    // act: paint again with no state change
    albor::PictureRecorder second;
    const auto replay_start = std::chrono::steady_clock::now();
    view.paint_content(second);
    const double replay_ms = elapsed_ms(replay_start);
    // record the measurements so a run reports what it measured
    RecordProperty("first_paint_ms", std::to_string(first_ms));
    RecordProperty("replayed_paint_ms", std::to_string(replay_ms));
    // assert: the second paint replays what the first recorded, so it builds nothing
    EXPECT_EQ(view.base_record_count(), base_records);
    EXPECT_EQ(view.series_record_count(), series_records);
    EXPECT_EQ(view.legend_record_count(), legend_records);
    // assert: a replayed frame records the background and one picture per retained layer
    EXPECT_EQ(second.commands().size(), first.commands().size());
    // assert: the replay costs less than the recording it replaces
    EXPECT_LT(replay_ms, first_ms + 1.0);
}

TEST(StudioChartsPerformanceChecks, an_overlay_rebuild_stays_within_its_debug_budget) {
    // arrange: a view over one chart with four series
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, SAMPLE_COUNT}};
    // the samples of the time axis
    std::vector<double> time;
    // one turned dataset per series
    std::vector<std::vector<double>> series(SERIES_COUNT);
    // every sample of the time axis and of the series
    for (std::size_t sample = 0; sample < SAMPLE_COUNT; ++sample) {
        time.push_back(static_cast<double>(sample) * 1.0e-6);
        // every series carries its own deterministic waveform
        for (std::size_t index = 0; index < SERIES_COUNT; ++index)
            series[index].push_back(static_cast<double>((sample * (index + 1)) % 97));
    }
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the time axis
    expressions.emplace_back(Expression<double>("time", std::move(time), slices, "s"));
    // the series the chart plots
    for (std::size_t index = 0; index < SERIES_COUNT; ++index)
        expressions.emplace_back(Expression<double>("V(" + std::to_string(index + 1) + ")", std::move(series[index]), slices, "V"));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the dataset carries one step
    StepInformation steps({}, {}, {{0.0, static_cast<double>(SAMPLE_COUNT)}});
    // the controller over the panel the shell arranges
    studio::ChartsController controller(1200.0f, 800.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* chart = controller.add_chart();
    ASSERT_NE(chart, nullptr);
    chart->plot_series({manager.evaluate("V(1)"), manager.evaluate("V(2)"), manager.evaluate("V(3)"), manager.evaluate("V(4)")});
    controller.rebuild_frames();
    // the view the shell keeps alive
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{}, albor::Size{.width = 1200.0f, .height = 800.0f}));
    view.set_frames(controller.frames());
    // the first paint records the layers the overlay repaints above
    albor::PictureRecorder warmup;
    view.paint_content(warmup);
    // act: time a hundred overlay republishes, which a drag issues per move
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < 100; ++index) {
        // the selection rectangle the drag reports
        const float ratio = 0.01f * static_cast<float>(index);
        view.set_zoom_selection(albor::Rect::from_origin_size(albor::Point{.x = 100.0f + ratio * 100.0f, .y = 100.0f}, albor::Size{.width = 200.0f, .height = 150.0f}));
        // the frame the shell repaints for the move
        albor::PictureRecorder frame;
        view.paint_content(frame);
    }
    const double per_update = elapsed_ms(start) / 100.0;
    // record the measurement so a run reports what it measured
    RecordProperty("overlay_update_ms", std::to_string(per_update));
    // assert: an overlay republish stays inside a debug-safe ceiling of the half millisecond release budget
    EXPECT_LT(per_update, 5.0) << "an overlay update took " << per_update << " ms";
}

TEST(StudioChartsPerformanceChecks, a_hover_update_stays_within_its_debug_budget) {
    // arrange: a controller over one chart with four series
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, SAMPLE_COUNT}};
    // the samples of the time axis
    std::vector<double> time;
    // one turned dataset per series
    std::vector<std::vector<double>> series(SERIES_COUNT);
    // every sample of the time axis and of the series
    for (std::size_t sample = 0; sample < SAMPLE_COUNT; ++sample) {
        time.push_back(static_cast<double>(sample) * 1.0e-6);
        // every series carries its own deterministic waveform
        for (std::size_t index = 0; index < SERIES_COUNT; ++index)
            series[index].push_back(static_cast<double>((sample * (index + 1)) % 97));
    }
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the time axis
    expressions.emplace_back(Expression<double>("time", std::move(time), slices, "s"));
    // the series the chart plots
    for (std::size_t index = 0; index < SERIES_COUNT; ++index)
        expressions.emplace_back(Expression<double>("V(" + std::to_string(index + 1) + ")", std::move(series[index]), slices, "V"));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the dataset carries one step
    StepInformation steps({}, {}, {{0.0, static_cast<double>(SAMPLE_COUNT)}});
    // the controller over the panel the shell arranges
    studio::ChartsController controller(1200.0f, 800.0f, [](const std::string& text) { return static_cast<float>(text.size()) * 6.0f; });
    controller.set_dataset(manager, steps, AbscissaScale::LINEAR);
    ChartEngine* chart = controller.add_chart();
    ASSERT_NE(chart, nullptr);
    chart->plot_series({manager.evaluate("V(1)"), manager.evaluate("V(2)"), manager.evaluate("V(3)"), manager.evaluate("V(4)")});
    controller.rebuild_frames();
    // the plot the pointer moves inside
    const auto [left, top, right, bottom] = controller.plot_rect(0);
    // act: time a hundred hover reads at different abscissae
    std::string last_readout;
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < 100; ++index) {
        // the pointer position inside the plot
        const float ratio = static_cast<float>(index) / 100.0f;
        last_readout = controller.hover_moved(left + ratio * (right - left), top + 0.5f * (bottom - top));
    }
    const double per_update = elapsed_ms(start) / 100.0;
    // record the measurement so a run reports what it measured
    RecordProperty("hover_update_ms", std::to_string(per_update));
    // assert: a hover read stays inside a debug-safe ceiling of the fifth of a millisecond release budget
    EXPECT_LT(per_update, 2.0) << "a hover update took " << per_update << " ms";
    // assert: the readout names the series at the pointer
    EXPECT_FALSE(last_readout.empty());
}

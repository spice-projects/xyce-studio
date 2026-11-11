#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "albor/render/draw_command.h"
#include "albor/render/resource_factory.h"
#include "albor/render/shaped_run.h"
#include "albor/text/text_engine.h"
#include "charts/chart_scene_builder.h"
#include "charts/chart_text_measurer.h"

namespace
{
    // the text engine double shapes every label into a fixed width run
    class FakeTextEngine final : public albor::TextEngine
    {
    public:
        albor::TextLayout measure(const std::string_view text, const float max_width) const override {
            // the unbounded measure delegates to the explicit size overload
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        albor::TextLayout measure(const std::string_view text, const float max_width, const float font_size) const override {
            // count the shaping calls, which the cache test asserts on
            calls++;
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

        // how many times the engine shaped a label
        mutable int calls = 0;
    };

} // namespace

TEST(StudioChartSceneBuilderChecks, a_cartesian_frame_becomes_a_retained_picture) {
    // arrange: a frame with a plot, grid, ticks, one series, two labels and a legend
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 40.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 340.0f;
    frame.plot_h = 240.0f;
    frame.x_datum = 250.0f;
    frame.x_grid.push_back(ChartGridLine{.pixel_pos = 100.0f, .major = true, .alpha = 1.0f});
    frame.y_axes[0].enabled = true;
    frame.y_axes[0].datum = 40.0f;
    frame.y_axes[0].grid_lines.push_back(ChartGridLine{.pixel_pos = 100.0f, .major = true, .alpha = 1.0f});
    frame.y_axes[0].ticks.push_back(ChartTick{.value = 1.0, .pixel_pos = 100.0f, .major = true, .show_label = true, .label = "1", .label_width = 6.0f});
    frame.x_ticks.push_back(ChartTick{.value = 1.0, .pixel_pos = 120.0f, .major = true, .show_label = true, .label = "1", .label_width = 6.0f});
    frame.series.push_back(ChartSeriesFrame{.name = "V(out)", .color = {0.0f, 0.48f, 1.0f, 1.0f}, .axis = 0, .step = 0, .boundary = false, .points = {{50.0f, 50.0f}, {100.0f, 100.0f}}});
    frame.legend.push_back(ChartLegendItem{.name = "V(out)", .color = {0.0f, 0.48f, 1.0f, 1.0f}});
    frame.legend_x = 100.0f;
    frame.legend_y = 270.0f;
    frame.legend_w = 120.0f;
    frame.legend_h = 20.0f;
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    const std::span<const ChartFrame> frames(&frame, 1);
    // count the commands of one kind in the recorded picture
    const auto count_kind = [](const std::shared_ptr<const albor::PictureResource>& picture, const albor::DrawCommandKind kind) {
        // count every command of the requested kind
        std::size_t count = 0;
        // walk the retained command list
        for (const albor::DrawCommand& command : picture->commands())
            if (command.kind() == kind)
                count++;
        // hand out the count
        return count;
    };
    // act: record the base picture
    const std::shared_ptr<const studio::ChartScene> scene = builder.build(frames, 400.0f, 300.0f, CHART_PALETTE_LIGHT, {}, 0.0f);
    // assert: the scene carries one slot and every command family
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->base, nullptr);
    ASSERT_EQ(scene->slots.size(), 1U);
    EXPECT_EQ(scene->slots[0].chart_index, 0U);
    EXPECT_EQ(scene->slots[0].kind, ChartKind::XY);
    EXPECT_GE(count_kind(scene->base, albor::DrawCommandKind::Rect), 2U);
    EXPECT_GE(count_kind(scene->base, albor::DrawCommandKind::LineBatch), 2U);
    EXPECT_GE(count_kind(scene->base, albor::DrawCommandKind::Path), 1U);
    EXPECT_GE(count_kind(scene->base, albor::DrawCommandKind::Text), 3U);
}

TEST(StudioChartSceneBuilderChecks, a_second_build_reuses_the_measured_labels) {
    // arrange: a frame whose labels are all cached after the first build
    ChartFrame frame;
    frame.frame_w = 300.0f;
    frame.frame_h = 200.0f;
    frame.plot_x = 30.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 250.0f;
    frame.plot_h = 150.0f;
    frame.x_datum = 160.0f;
    frame.x_ticks.push_back(ChartTick{.value = 1.0, .pixel_pos = 100.0f, .major = true, .show_label = true, .label = "1", .label_width = 6.0f});
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    const std::span<const ChartFrame> frames(&frame, 1);
    // act: record the same frame twice
    const auto first = builder.build(frames, 300.0f, 200.0f, CHART_PALETTE_LIGHT, {}, 0.0f);
    const int calls_after_first = engine.calls;
    const auto second = builder.build(frames, 300.0f, 200.0f, CHART_PALETTE_LIGHT, {}, 0.0f);
    // assert: the second build shapes no label again
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(engine.calls, calls_after_first);
    EXPECT_EQ(measurer.measure("1"), 6.0f);
}

TEST(StudioChartSceneBuilderChecks, a_smith_frame_records_the_grid_boundary_and_series) {
    // arrange: a smith frame with one boundary run, one grid run and one series
    ChartFrame frame;
    frame.smith = true;
    frame.frame_w = 300.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 200.0f;
    frame.plot_h = 200.0f;
    frame.smith_padding = 26.0f;
    frame.smith_grid.push_back(ChartSeriesFrame{.name = "boundary", .color = {0.0f, 0.0f, 0.0f, 0.0f}, .axis = 0, .step = 0, .boundary = true, .points = {{60.0f, 100.0f}, {100.0f, 60.0f}}});
    frame.smith_grid.push_back(ChartSeriesFrame{.name = "grid", .color = {0.0f, 0.0f, 0.0f, 0.0f}, .axis = 0, .step = 0, .boundary = false, .points = {{80.0f, 80.0f}, {120.0f, 120.0f}}});
    frame.smith_labels.push_back(ChartFrame::SmithLabel{.label = "0.5", .x = 100.0f, .y = 100.0f});
    frame.series.push_back(ChartSeriesFrame{.name = "S11", .color = {0.0f, 0.48f, 1.0f, 1.0f}, .axis = 0, .step = 0, .boundary = false, .points = {{70.0f, 90.0f}, {110.0f, 70.0f}}});
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    const std::span<const ChartFrame> frames(&frame, 1);
    // count the commands of one kind in the recorded picture
    const auto count_kind = [](const std::shared_ptr<const albor::PictureResource>& picture, const albor::DrawCommandKind kind) {
        // count every command of the requested kind
        std::size_t count = 0;
        // walk the retained command list
        for (const albor::DrawCommand& command : picture->commands())
            if (command.kind() == kind)
                count++;
        // hand out the count
        return count;
    };
    // act: record the base picture
    const auto scene = builder.build(frames, 300.0f, 300.0f, CHART_PALETTE_LIGHT, {}, 0.0f);
    // assert: the grid, boundary, series and label commands are present
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->base, nullptr);
    ASSERT_EQ(scene->slots.size(), 1U);
    EXPECT_EQ(scene->slots[0].kind, ChartKind::SMITH);
    EXPECT_GE(count_kind(scene->base, albor::DrawCommandKind::Path), 3U);
    EXPECT_GE(count_kind(scene->base, albor::DrawCommandKind::Text), 1U);
}

TEST(StudioChartSceneBuilderChecks, a_minor_grid_keeps_its_per_line_density_alpha) {
    // arrange: a frame with two minor grid lines carrying different density alphas
    ChartFrame frame;
    frame.frame_w = 300.0f;
    frame.frame_h = 200.0f;
    frame.plot_x = 30.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 250.0f;
    frame.plot_h = 150.0f;
    frame.x_datum = 160.0f;
    frame.x_grid.push_back(ChartGridLine{.pixel_pos = 60.0f, .major = false, .alpha = 0.10f});
    frame.x_grid.push_back(ChartGridLine{.pixel_pos = 120.0f, .major = false, .alpha = 0.35f});
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    const std::span<const ChartFrame> frames(&frame, 1);
    // act: record the grid
    const auto scene = builder.build(frames, 300.0f, 200.0f, CHART_PALETTE_LIGHT, {}, 0.0f);
    // assert: the two minor batches keep their own alphas
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->base, nullptr);
    std::vector<float> alphas;
    // walk the retained commands
    for (const albor::DrawCommand& command : scene->base->commands()) {
        // only the line batches carry the minor grid
        if (command.kind() != albor::DrawCommandKind::LineBatch)
            continue;
        // keep the stroke alpha of this batch
        alphas.push_back(command.batch_geometry().stroke.color.alpha);
    }
    // the two density buckets are recorded separately
    ASSERT_EQ(alphas.size(), 2U);
    EXPECT_NEAR(alphas[0], 0.10f, 0.01f);
    EXPECT_NEAR(alphas[1], 0.35f, 0.01f);
}

TEST(StudioChartSceneBuilderChecks, an_empty_frame_list_records_no_picture) {
    // arrange: the builder with no frame to record
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    // act: build a scene over an empty frame list
    const auto scene = builder.build({}, 400.0f, 300.0f, CHART_PALETTE_LIGHT, {}, 0.0f);
    // assert: the scene is valid but carries no picture or slot
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(scene->base, nullptr);
    EXPECT_EQ(scene->strip, nullptr);
    EXPECT_TRUE(scene->slots.empty());
}

TEST(StudioChartSceneBuilderChecks, a_zoom_selection_becomes_a_retained_overlay) {
    // arrange: the builder and one selection rectangle
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    const albor::Rect selection = albor::Rect::from_origin_size(albor::Point{.x = 10.0f, .y = 20.0f}, albor::Size{.width = 100.0f, .height = 80.0f});
    // act: record the overlay
    const auto overlay = builder.build_zoom_overlay(selection);
    // assert: the overlay carries the picture and the damage bounds
    ASSERT_NE(overlay, nullptr);
    ASSERT_NE(overlay->picture, nullptr);
    EXPECT_EQ(overlay->bounds, selection);
    EXPECT_EQ(overlay->picture->command_count(), 2U);
    EXPECT_EQ(overlay->picture->commands()[0].kind(), albor::DrawCommandKind::Rect);
    EXPECT_EQ(overlay->picture->commands()[1].kind(), albor::DrawCommandKind::Rect);
}

TEST(StudioChartSceneBuilderChecks, a_tab_strip_records_bounds_and_shifts_the_chart_content) {
    // arrange: a builder and two dataset tabs over one chart frame
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 268.0f;
    frame.plot_x = 40.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 340.0f;
    frame.plot_h = 240.0f;
    const std::vector<studio::ChartTabSpec> tabs = {
        studio::ChartTabSpec{.title = "first", .active = true, .closable = false},
        studio::ChartTabSpec{.title = "second", .active = false, .closable = true},
    };
    const std::span<const ChartFrame> frames(&frame, 1);
    // act: record the scene with the strip
    const auto scene = builder.build(frames, 400.0f, 300.0f, CHART_PALETTE_LIGHT, tabs, studio::CHART_TAB_HEIGHT);
    // assert: both tabs are recorded and the chart starts under the strip
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->base, nullptr);
    ASSERT_NE(scene->strip, nullptr);
    EXPECT_FLOAT_EQ(scene->tab_bar_height, studio::CHART_TAB_HEIGHT);
    EXPECT_FLOAT_EQ(scene->content_y, studio::CHART_TAB_HEIGHT);
    ASSERT_EQ(scene->tabs.size(), 2U);
    EXPECT_FLOAT_EQ(scene->tabs[0].bounds.x, 8.0f);
    EXPECT_FLOAT_EQ(scene->tabs[0].bounds.y, 0.0f);
    EXPECT_GE(scene->tabs[0].bounds.width, 96.0f);
    EXPECT_TRUE(scene->tabs[0].close_bounds.width <= 0.0f);
    EXPECT_GT(scene->tabs[1].close_bounds.width, 0.0f);
    EXPECT_GE(scene->tabs[1].close_bounds.x, scene->tabs[1].bounds.x);
    EXPECT_LE(scene->tabs[1].close_bounds.x + scene->tabs[1].close_bounds.width, scene->tabs[1].bounds.x + scene->tabs[1].bounds.width);
    ASSERT_EQ(scene->slots.size(), 1U);
    EXPECT_FLOAT_EQ(scene->slots[0].frame.y, studio::CHART_TAB_HEIGHT);
    EXPECT_GE(scene->slots[0].plot.y, studio::CHART_TAB_HEIGHT);
    EXPECT_LE(scene->slots[0].frame.y + scene->slots[0].frame.height, 300.0f);
}

TEST(StudioChartSceneBuilderChecks, a_narrow_strip_shrinks_the_tabs_to_fit_the_panel) {
    // arrange: three tabs over a narrow panel
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    const std::vector<studio::ChartTabSpec> tabs = {
        studio::ChartTabSpec{.title = "first dataset", .active = true, .closable = true},
        studio::ChartTabSpec{.title = "second dataset", .active = false, .closable = true},
        studio::ChartTabSpec{.title = "third dataset", .active = false, .closable = true},
    };
    // act: record the strip into a 200 pixel panel
    const auto scene = builder.build({}, 200.0f, 300.0f, CHART_PALETTE_LIGHT, tabs, studio::CHART_TAB_HEIGHT);
    // assert: every tab is recorded inside the panel
    ASSERT_NE(scene, nullptr);
    ASSERT_EQ(scene->tabs.size(), 3U);
    for (const studio::ChartTab& tab : scene->tabs) {
        EXPECT_GE(tab.bounds.x, 0.0f);
        EXPECT_LE(tab.bounds.x + tab.bounds.width, 200.0f);
        EXPECT_GT(tab.bounds.width, 0.0f);
    }
}

TEST(StudioChartSceneBuilderChecks, an_empty_frame_list_keeps_the_tab_strip) {
    // arrange: a panel showing tabs but no chart frame
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    const std::vector<studio::ChartTabSpec> tabs = {
        studio::ChartTabSpec{.title = "first", .active = true, .closable = false},
        studio::ChartTabSpec{.title = "second", .active = false, .closable = true},
    };
    // act: build the scene without a chart
    const auto scene = builder.build({}, 400.0f, 300.0f, CHART_PALETTE_LIGHT, tabs, studio::CHART_TAB_HEIGHT);
    // assert: the strip stays recorded and the picture carries it
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(scene->base, nullptr);
    ASSERT_NE(scene->strip, nullptr);
    EXPECT_FLOAT_EQ(scene->tab_bar_height, studio::CHART_TAB_HEIGHT);
    EXPECT_FLOAT_EQ(scene->content_y, studio::CHART_TAB_HEIGHT);
    EXPECT_EQ(scene->tabs.size(), 2U);
    EXPECT_TRUE(scene->slots.empty());
}

TEST(StudioChartSceneBuilderChecks, an_empty_tab_list_reserves_no_strip) {
    // arrange: a chart frame with the caller offering a strip height
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 200.0f;
    const std::span<const ChartFrame> frames(&frame, 1);
    // act: build with no tab specs
    const auto scene = builder.build(frames, 400.0f, 300.0f, CHART_PALETTE_LIGHT, {}, studio::CHART_TAB_HEIGHT);
    // assert: no strip is reserved
    ASSERT_NE(scene, nullptr);
    EXPECT_FLOAT_EQ(scene->tab_bar_height, 0.0f);
    EXPECT_FLOAT_EQ(scene->content_y, 0.0f);
    EXPECT_EQ(scene->strip, nullptr);
    EXPECT_TRUE(scene->tabs.empty());
    ASSERT_EQ(scene->slots.size(), 1U);
    EXPECT_FLOAT_EQ(scene->slots[0].frame.y, 0.0f);
}

TEST(StudioChartSceneBuilderChecks, a_crowded_strip_keeps_every_tab_selectable) {
    // arrange: eight tabs over a narrow panel
    albor::ResourceFactory resources;
    FakeTextEngine engine;
    studio::AlborChartTextMeasurer measurer(engine);
    studio::ChartSceneBuilder builder(resources, measurer);
    std::vector<studio::ChartTabSpec> tabs;
    for (int index = 0; index < 8; ++index)
        tabs.push_back(studio::ChartTabSpec{.title = "dataset " + std::to_string(index), .active = index == 7, .closable = true});
    // act: record the strip into a 200 pixel panel
    const auto scene = builder.build({}, 200.0f, 300.0f, CHART_PALETTE_LIGHT, tabs, studio::CHART_TAB_HEIGHT);
    // assert: every tab is recorded inside the panel
    ASSERT_NE(scene, nullptr);
    ASSERT_EQ(scene->tabs.size(), 8U);
    for (const studio::ChartTab& tab : scene->tabs) {
        EXPECT_GE(tab.bounds.x, 0.0f);
        EXPECT_LE(tab.bounds.x + tab.bounds.width, 200.0f);
        EXPECT_GT(tab.bounds.width, 0.0f);
    }
    EXPECT_TRUE(scene->tabs.back().active);
}

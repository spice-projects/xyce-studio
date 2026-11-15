#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "albor/core/paint.h"
#include "albor/render/draw_command.h"
#include "albor/render/picture_resource.h"
#include "albor/render/shaped_run.h"
#include "albor/runtime/binding_engine.h"
#include "albor/text/text_engine.h"
#include "charts/chart_palette.h"
#include "views/charts/chart_view.h"

namespace
{
    // the text engine double shapes one glyph per character
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
} // namespace

TEST(StudioChartViewChecks, the_background_covers_the_arranged_panel) {
    // arrange: a chart view arranged over a known panel
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.set_palette(CHART_PALETTE_DARK);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 10.0f, .y = 20.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the background step
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    // assert: one background rectangle covers the arranged panel
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_EQ(commands.front().kind(), albor::DrawCommandKind::Rect);
    EXPECT_FLOAT_EQ(commands.front().rect_geometry().rect.x, 10.0f);
    EXPECT_FLOAT_EQ(commands.front().rect_geometry().rect.y, 20.0f);
    EXPECT_FLOAT_EQ(commands.front().rect_geometry().rect.width, 400.0f);
    EXPECT_FLOAT_EQ(commands.front().rect_geometry().rect.height, 300.0f);
}

TEST(StudioChartViewChecks, the_semantic_node_names_the_chart_surface) {
    // arrange: a chart view with the published dataset summary
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.set_accessible_summary("1 line chart");
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 10.0f, .y = 20.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: build the semantic node
    const albor::SemanticNode node = view.semantic_node();
    // assert: the panel is the charts canvas with the published summary
    EXPECT_EQ(node.role(), albor::SemanticRole::Canvas);
    EXPECT_EQ(node.explicit_id(), "charts_view");
    EXPECT_EQ(node.name(), "1 line chart");
    EXPECT_FLOAT_EQ(node.bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(node.bounds().y, 20.0f);
}

TEST(StudioChartViewChecks, one_white_plot_rectangle_is_painted_per_chart) {
    // arrange: one chart frame with a known plot rect
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the background and the plot rectangle
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: the background, the plot fill and the border are painted
    ASSERT_EQ(commands.size(), 3U);
    EXPECT_EQ(commands[0].kind(), albor::DrawCommandKind::Rect);
    EXPECT_FLOAT_EQ(commands[0].rect_geometry().rect.width, 400.0f);
    EXPECT_FLOAT_EQ(commands[1].rect_geometry().rect.x, 50.0f);
    EXPECT_FLOAT_EQ(commands[1].rect_geometry().rect.y, 20.0f);
    EXPECT_FLOAT_EQ(commands[1].rect_geometry().rect.width, 300.0f);
    EXPECT_FLOAT_EQ(commands[1].rect_geometry().rect.height, 240.0f);
}

TEST(StudioChartViewChecks, charts_share_the_content_height_equally) {
    // arrange: two frames with the plot geometry of a half-height slot
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame first;
    first.frame_w = 400.0f;
    first.frame_h = 150.0f;
    first.plot_x = 50.0f;
    first.plot_y = 10.0f;
    first.plot_w = 300.0f;
    first.plot_h = 120.0f;
    ChartFrame second = first;
    view.set_frames({first, second});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the two slots
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: the second base layer is placed one slot below the first
    ASSERT_GE(commands.size(), 5U);
    EXPECT_FLOAT_EQ(commands[1].rect_geometry().rect.y, 10.0f);
    EXPECT_FLOAT_EQ(commands[1].transform().translate_y, 0.0f);
    EXPECT_FLOAT_EQ(commands[3].rect_geometry().rect.y, 10.0f);
    EXPECT_FLOAT_EQ(commands[3].transform().translate_y, 150.0f);
    EXPECT_FLOAT_EQ(commands[1].rect_geometry().rect.x, 50.0f);
    EXPECT_FLOAT_EQ(commands[3].rect_geometry().rect.x, 50.0f);
    EXPECT_FLOAT_EQ(commands[1].rect_geometry().rect.width, 300.0f);
    EXPECT_FLOAT_EQ(commands[3].rect_geometry().rect.width, 300.0f);
}

TEST(StudioChartViewChecks, tick_marks_are_painted_inside_the_centred_plot) {
    // arrange: one abscissa and one ordinate tick on a known plot rect
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    frame.x_ticks.push_back({.value = 5.0, .pixel_pos = 200.0f, .major = true, .show_label = true, .label = "5", .label_width = 6.0f});
    frame.y_axes[0].enabled = true;
    frame.y_axes[0].ticks.push_back({.value = 1.0, .pixel_pos = 140.0f, .major = true, .show_label = true, .label = "1", .label_width = 6.0f});
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the border and the two tick marks
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: the abscissa tick hangs below the shared plot
    ASSERT_EQ(commands.size(), 7U);
    EXPECT_FLOAT_EQ(commands[3].rect_geometry().rect.x, 200.0f);
    EXPECT_FLOAT_EQ(commands[3].rect_geometry().rect.y, 260.0f);
    EXPECT_FLOAT_EQ(commands[3].rect_geometry().rect.height, 4.0f);
    // assert: the west tick grows inward from the left edge
    EXPECT_FLOAT_EQ(commands[4].rect_geometry().rect.x, 50.0f);
    EXPECT_FLOAT_EQ(commands[4].rect_geometry().rect.y, 138.0f);
    EXPECT_FLOAT_EQ(commands[4].rect_geometry().rect.width, 4.0f);
    // assert: the abscissa label centres under the tick
    EXPECT_EQ(commands[5].kind(), albor::DrawCommandKind::Text);
    EXPECT_FLOAT_EQ(commands[5].text_geometry().origin.x, 197.0f);
    EXPECT_FLOAT_EQ(commands[5].text_geometry().origin.y, 266.0f);
    // assert: the west label right aligns against the plot
    EXPECT_EQ(commands[6].kind(), albor::DrawCommandKind::Text);
    EXPECT_FLOAT_EQ(commands[6].text_geometry().origin.x, 38.0f);
}

TEST(StudioChartViewChecks, grid_lines_are_mapped_into_the_shared_plot) {
    // arrange: one major abscissa line plus one major and one minor ordinate line
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    frame.x_grid.push_back({.pixel_pos = 200.0f, .major = true, .alpha = 1.0f});
    frame.y_axes[0].enabled = true;
    frame.y_axes[0].grid_lines.push_back({.pixel_pos = 140.0f, .major = true, .alpha = 1.0f});
    frame.y_axes[0].grid_lines.push_back({.pixel_pos = 80.0f, .major = false, .alpha = 0.5f});
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the plot with its grid
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: the abscissa line spans the mapped plot height
    ASSERT_EQ(commands.size(), 6U);
    EXPECT_FLOAT_EQ(commands[2].rect_geometry().rect.x, 200.0f);
    EXPECT_FLOAT_EQ(commands[2].rect_geometry().rect.y, 20.0f);
    EXPECT_FLOAT_EQ(commands[2].rect_geometry().rect.height, 240.0f);
    // assert: the ordinate lines map into the plot and the minor line keeps its alpha
    EXPECT_FLOAT_EQ(commands[3].rect_geometry().rect.y, 140.0f);
    EXPECT_FLOAT_EQ(commands[3].rect_geometry().rect.width, 300.0f);
    EXPECT_FLOAT_EQ(commands[4].rect_geometry().rect.y, 80.0f);
    EXPECT_NEAR(commands[4].paint().fill.alpha, CHART_PALETTE_LIGHT.grid.a * 0.5f, 0.001f);
}

TEST(StudioChartViewChecks, a_second_paint_reuses_the_base_layer) {
    // arrange: a chart with one labelled y axis
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    frame.y_axes[0].enabled = true;
    frame.y_axes[0].ticks.push_back({.value = 1.0, .pixel_pos = 140.0f, .major = true, .show_label = true, .label = "1", .label_width = 6.0f});
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    albor::PictureRecorder first;
    view.paint_content(first);
    ASSERT_EQ(view.base_record_count(), 1U);
    // act: paint the same chart again
    albor::PictureRecorder second;
    view.paint_content(second);
    // assert: the retained base picture is reused
    EXPECT_EQ(view.base_record_count(), 1U);
}

TEST(StudioChartViewChecks, a_series_change_reuses_the_base_layer) {
    // arrange: a chart with one labelled y axis and no series
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    frame.y_axes[0].enabled = true;
    frame.y_axes[0].ticks.push_back({.value = 1.0, .pixel_pos = 140.0f, .major = true, .show_label = true, .label = "1", .label_width = 6.0f});
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    albor::PictureRecorder first;
    view.paint_content(first);
    ASSERT_EQ(view.base_record_count(), 1U);
    // act: add a series to the same chart
    ChartSeriesFrame series;
    series.name = "V(out)";
    series.points = {{60.0f, 180.0f}, {200.0f, 80.0f}};
    frame.series.push_back(series);
    view.set_frames({frame});
    albor::PictureRecorder second;
    view.paint_content(second);
    // assert: adding a plot does not re-record the base
    EXPECT_EQ(view.base_record_count(), 1U);
    // assert: the new series is recorded in its own layer
    EXPECT_EQ(view.series_record_count(), 1U);
}

TEST(StudioChartViewChecks, an_axis_change_rebuilds_the_base_layer) {
    // arrange: a chart with one labelled y axis
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    frame.y_axes[0].enabled = true;
    frame.y_axes[0].ticks.push_back({.value = 1.0, .pixel_pos = 140.0f, .major = true, .show_label = true, .label = "1", .label_width = 6.0f});
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    albor::PictureRecorder first;
    view.paint_content(first);
    ASSERT_EQ(view.base_record_count(), 1U);
    // act: move and rename the axis tick
    frame.y_axes[0].ticks[0].pixel_pos = 150.0f;
    frame.y_axes[0].ticks[0].label = "2";
    view.set_frames({frame});
    albor::PictureRecorder second;
    view.paint_content(second);
    // assert: the changed axis re-records the base
    EXPECT_EQ(view.base_record_count(), 2U);
}

TEST(StudioChartViewChecks, a_series_change_rebuilds_only_the_series_layer) {
    // arrange: a chart with one series
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    frame.y_axes[0].enabled = true;
    frame.y_axes[0].ticks.push_back({.value = 1.0, .pixel_pos = 140.0f, .major = true, .show_label = true, .label = "1", .label_width = 6.0f});
    ChartSeriesFrame series;
    series.name = "V(out)";
    series.points = {{60.0f, 180.0f}, {200.0f, 80.0f}};
    frame.series.push_back(series);
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    albor::PictureRecorder first;
    view.paint_content(first);
    ASSERT_EQ(view.base_record_count(), 1U);
    ASSERT_EQ(view.series_record_count(), 1U);
    // act: move one series sample
    frame.series[0].points[0].y += 10.0f;
    view.set_frames({frame});
    albor::PictureRecorder second;
    view.paint_content(second);
    // assert: only the series layer is rebuilt
    EXPECT_EQ(view.base_record_count(), 1U);
    EXPECT_EQ(view.series_record_count(), 2U);
}

TEST(StudioChartViewChecks, an_axis_change_rebuilds_the_base_but_reuses_the_series) {
    // arrange: a chart with one series and one labelled y axis
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    frame.y_axes[0].enabled = true;
    frame.y_axes[0].ticks.push_back({.value = 1.0, .pixel_pos = 140.0f, .major = true, .show_label = true, .label = "1", .label_width = 6.0f});
    ChartSeriesFrame series;
    series.name = "V(out)";
    series.points = {{60.0f, 180.0f}, {200.0f, 80.0f}};
    frame.series.push_back(series);
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    albor::PictureRecorder first;
    view.paint_content(first);
    ASSERT_EQ(view.base_record_count(), 1U);
    ASSERT_EQ(view.series_record_count(), 1U);
    // act: change the y tick label without moving the plot
    frame.y_axes[0].ticks[0].label = "2";
    view.set_frames({frame});
    albor::PictureRecorder second;
    view.paint_content(second);
    // assert: the base is rebuilt and the series picture is reused
    EXPECT_EQ(view.base_record_count(), 2U);
    EXPECT_EQ(view.series_record_count(), 1U);
}

TEST(StudioChartViewChecks, a_series_is_painted_above_the_base) {
    // arrange: a chart with one two-point series
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    ChartSeriesFrame series;
    series.name = "V(out)";
    series.color = ChartColor{.r = 0.0f, .g = 0.48f, .b = 1.0f, .a = 1.0f};
    series.points = {{50.0f, 20.0f}, {350.0f, 260.0f}};
    frame.series.push_back(series);
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the base and the series
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: the series is one clipped polyline after the base
    std::size_t polylines = 0;
    for (const albor::DrawCommand& command : commands)
        if (command.kind() == albor::DrawCommandKind::Polyline)
            polylines++;
    EXPECT_EQ(polylines, 1U);
}

TEST(StudioChartViewChecks, a_legend_is_painted_above_the_series) {
    // arrange: a chart with one legend entry
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    frame.legend.push_back({.name = "V(out)", .color = {.r = 0.0f, .g = 0.48f, .b = 1.0f, .a = 1.0f}});
    frame.legend_x = 150.0f;
    frame.legend_y = 260.0f;
    frame.legend_w = 100.0f;
    frame.legend_h = 22.0f;
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the base and the legend
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: the legend swatch and label follow the base
    ASSERT_EQ(commands.size(), 5U);
    EXPECT_EQ(commands[3].kind(), albor::DrawCommandKind::Rect);
    EXPECT_EQ(commands[4].kind(), albor::DrawCommandKind::Text);
    EXPECT_FLOAT_EQ(commands[3].transform().translate_x, 150.0f);
    EXPECT_FLOAT_EQ(commands[3].transform().translate_y, 260.0f);
}

TEST(StudioChartViewChecks, a_series_change_reuses_the_legend_layer) {
    // arrange: a chart with one series and one legend entry
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    ChartSeriesFrame series;
    series.name = "V(out)";
    series.points = {{60.0f, 180.0f}, {200.0f, 80.0f}};
    frame.series.push_back(series);
    frame.legend.push_back({.name = "V(out)", .color = {.r = 0.0f, .g = 0.48f, .b = 1.0f, .a = 1.0f}});
    frame.legend_x = 150.0f;
    frame.legend_y = 260.0f;
    frame.legend_w = 100.0f;
    frame.legend_h = 22.0f;
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    albor::PictureRecorder first;
    view.paint_content(first);
    ASSERT_EQ(view.base_record_count(), 1U);
    ASSERT_EQ(view.series_record_count(), 1U);
    ASSERT_EQ(view.legend_record_count(), 1U);
    // act: move one series sample
    frame.series[0].points[0].y += 10.0f;
    view.set_frames({frame});
    albor::PictureRecorder second;
    view.paint_content(second);
    // assert: the base and legend are reused, only the series changes
    EXPECT_EQ(view.base_record_count(), 1U);
    EXPECT_EQ(view.series_record_count(), 2U);
    EXPECT_EQ(view.legend_record_count(), 1U);
}

TEST(StudioChartViewChecks, a_legend_change_rebuilds_only_the_legend_layer) {
    // arrange: a chart with one series and one legend entry
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    ChartSeriesFrame series;
    series.name = "V(out)";
    series.points = {{60.0f, 180.0f}, {200.0f, 80.0f}};
    frame.series.push_back(series);
    frame.legend.push_back({.name = "V(out)", .color = {.r = 0.0f, .g = 0.48f, .b = 1.0f, .a = 1.0f}});
    frame.legend_x = 150.0f;
    frame.legend_y = 260.0f;
    frame.legend_w = 100.0f;
    frame.legend_h = 22.0f;
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    albor::PictureRecorder first;
    view.paint_content(first);
    ASSERT_EQ(view.base_record_count(), 1U);
    ASSERT_EQ(view.series_record_count(), 1U);
    ASSERT_EQ(view.legend_record_count(), 1U);
    // act: rename the legend entry
    frame.legend[0].name = "I(out)";
    view.set_frames({frame});
    albor::PictureRecorder second;
    view.paint_content(second);
    // assert: only the legend layer is rebuilt
    EXPECT_EQ(view.base_record_count(), 1U);
    EXPECT_EQ(view.series_record_count(), 1U);
    EXPECT_EQ(view.legend_record_count(), 2U);
}

TEST(StudioChartViewChecks, the_dataset_strip_is_a_retained_picture) {
    // arrange: one active tab over a 32 pixel strip
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.set_tabs({studio::ChartTabSpec{.title = " LIN Analysis ", .active = true, .closable = false}});
    view.set_content_y(32.0f);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the strip
    albor::PictureRecorder first;
    view.paint_content(first);
    // assert: the strip records its plate, hairline, active tab and label
    EXPECT_EQ(view.strip_record_count(), 1U);
    ASSERT_EQ(first.commands().size(), 6U);
    // act: paint the same strip again
    albor::PictureRecorder second;
    view.paint_content(second);
    // assert: the strip picture is reused
    EXPECT_EQ(view.strip_record_count(), 1U);
}

TEST(StudioChartViewChecks, a_smith_frame_paints_the_gamma_grid_and_trace) {
    // arrange: a smith frame with a boundary, a grid run, one trace and one label
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 100.0f;
    frame.plot_y = 50.0f;
    frame.plot_w = 200.0f;
    frame.plot_h = 200.0f;
    frame.smith = true;
    frame.smith_padding = 26.0f;
    ChartSeriesFrame boundary;
    boundary.name = "boundary";
    boundary.boundary = true;
    boundary.points = {{100.0f, 50.0f}, {300.0f, 250.0f}};
    frame.smith_grid.push_back(boundary);
    ChartSeriesFrame grid;
    grid.name = "grid";
    grid.boundary = false;
    grid.points = {{150.0f, 100.0f}, {250.0f, 200.0f}};
    frame.smith_grid.push_back(grid);
    ChartSeriesFrame trace;
    trace.name = "S11";
    trace.points = {{180.0f, 120.0f}, {220.0f, 160.0f}};
    frame.series.push_back(trace);
    frame.smith_labels.push_back({.label = "1", .x = 200.0f, .y = 150.0f});
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the smith base and trace
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: the boundary, grid and trace are three polylines
    std::size_t polylines = 0;
    for (const albor::DrawCommand& command : commands)
        if (command.kind() == albor::DrawCommandKind::Polyline)
            polylines++;
    EXPECT_EQ(polylines, 3U);
    EXPECT_EQ(view.base_record_count(), 1U);
    EXPECT_EQ(view.series_record_count(), 1U);
}

TEST(StudioChartViewChecks, a_zoom_selection_paints_above_the_strip) {
    // arrange: a chart under a strip, with a transient selection in content coordinates
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    view.set_frames({frame});
    view.set_content_y(32.0f);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 10.0f, .y = 20.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    view.set_zoom_selection(albor::Rect::from_origin_size(albor::Point{.x = 60.0f, .y = 30.0f}, albor::Size{.width = 100.0f, .height = 80.0f}));
    // act: paint the chart and the selection
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: the wash sits under the strip, offset by the panel origin and the content start
    int washes = 0;
    for (const albor::DrawCommand& command : commands) {
        // only the selection rectangles carry the selection extent
        if (command.kind() != albor::DrawCommandKind::Rect || command.rect_geometry().rect.width != 100.0f || command.rect_geometry().rect.height != 80.0f)
            continue;
        // the selection lands after the panel origin and the strip
        EXPECT_FLOAT_EQ(command.rect_geometry().rect.x, 70.0f);
        EXPECT_FLOAT_EQ(command.rect_geometry().rect.y, 82.0f);
        // the wash and its border are both recorded
        washes++;
    }
    EXPECT_EQ(washes, 2);
}

TEST(StudioChartViewChecks, a_pointer_cancel_ends_the_drag_without_a_zoom) {
    // arrange: a view with recording drag callbacks
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    int started = 0;
    int moved = 0;
    int ended = 0;
    int canceled = 0;
    view.set_on_zoom_drag_started([&](float, float) { started++; });
    view.set_on_zoom_drag_moved([&](float, float) { moved++; });
    view.set_on_zoom_drag_ended([&] { ended++; });
    view.set_on_zoom_drag_canceled([&] { canceled++; });
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 100.0f, .y = 120.0f};
    view.dispatch_event(press);
    ASSERT_EQ(started, 1);
    // act: the input layer aborts the capture
    albor::Event cancel;
    cancel.kind = albor::EventKind::PointerCancel;
    cancel.position = albor::Point{.x = 150.0f, .y = 150.0f};
    view.dispatch_event(cancel);
    // assert: the drag is dropped once and never applied
    EXPECT_EQ(canceled, 1);
    EXPECT_EQ(ended, 0);
    // act: a later move must not keep driving the selection
    albor::Event move;
    move.kind = albor::EventKind::PointerMove;
    move.position = albor::Point{.x = 160.0f, .y = 160.0f};
    view.dispatch_event(move);
    // assert: the canceled gesture no longer reports
    EXPECT_EQ(moved, 0);
}

TEST(StudioChartViewChecks, a_pointer_leave_keeps_a_captured_drag) {
    // arrange: a live drag over the chart
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    int moved = 0;
    int canceled = 0;
    int ended = 0;
    view.set_on_zoom_drag_moved([&](float, float) { moved++; });
    view.set_on_zoom_drag_canceled([&] { canceled++; });
    view.set_on_zoom_drag_ended([&] { ended++; });
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 100.0f, .y = 120.0f};
    view.dispatch_event(press);
    // act: the pointer crosses the panel edge while the capture keeps it
    albor::Event leave;
    leave.kind = albor::EventKind::PointerLeave;
    leave.position = albor::Point{.x = 420.0f, .y = 120.0f};
    view.dispatch_event(leave);
    // assert: the drag survives the leave
    EXPECT_EQ(canceled, 0);
    // act: the capture still delivers the moving corner
    albor::Event move;
    move.kind = albor::EventKind::PointerMove;
    move.position = albor::Point{.x = 150.0f, .y = 150.0f};
    view.dispatch_event(move);
    // assert: the move extends the live drag
    EXPECT_EQ(moved, 1);
    EXPECT_EQ(ended, 0);
}

TEST(StudioChartViewChecks, an_idle_escape_is_left_to_the_shell) {
    // arrange: a view with no live drag
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    int canceled = 0;
    view.set_on_zoom_drag_canceled([&] { canceled++; });
    // act: press escape without a drag
    albor::Event key;
    key.kind = albor::EventKind::KeyDown;
    key.key = albor::Key::Escape;
    view.dispatch_event(key);
    // assert: the key stays available to the rest of the shell
    EXPECT_FALSE(key.handled);
    EXPECT_EQ(canceled, 0);
}

TEST(StudioChartViewChecks, a_escape_aborts_a_live_drag) {
    // arrange: a live drag over the chart
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    int canceled = 0;
    view.set_on_zoom_drag_canceled([&] { canceled++; });
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 100.0f, .y = 120.0f};
    view.dispatch_event(press);
    // act: press escape while the drag is live
    albor::Event key;
    key.kind = albor::EventKind::KeyDown;
    key.key = albor::Key::Escape;
    view.dispatch_event(key);
    // assert: the drag is canceled once and the key is consumed
    EXPECT_TRUE(key.handled);
    EXPECT_EQ(canceled, 1);
}

TEST(StudioChartViewChecks, a_tab_press_follows_the_current_tab_layout) {
    // arrange: two tabs whose strip has already been painted
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.set_tabs({studio::ChartTabSpec{.title = "A", .active = true, .closable = false}, studio::ChartTabSpec{.title = "B", .active = false, .closable = false}});
    view.set_content_y(32.0f);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    int selected = -1;
    view.set_on_tab_selected([&](const int index) { selected = index; });
    // act: the tab set shrinks before the next paint, then the old second tab is pressed
    view.set_tabs({studio::ChartTabSpec{.title = "A", .active = true, .closable = false}});
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 60.0f, .y = 16.0f};
    view.dispatch_event(press);
    // assert: the retired tab no longer answers a press
    EXPECT_EQ(selected, -1);
}

TEST(StudioChartViewChecks, the_semantic_node_exposes_the_chart_commands) {
    // arrange: one cartesian chart and a command sink
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 300.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 20.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 240.0f;
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    studio::ChartCommand command = studio::ChartCommand::DeleteChart;
    std::size_t index = 99;
    view.set_on_chart_command([&](const studio::ChartCommand value, const std::size_t chart_index) {
        command = value;
        index = chart_index;
    });
    // act: invoke the semantic chart action
    const albor::SemanticNode node = view.semantic_node();
    ASSERT_TRUE(node.has_action("chart-0-zoom-to-fit"));
    EXPECT_TRUE(node.invoke("chart-0-zoom-to-fit"));
    // assert: the action reaches the chart command sink
    EXPECT_EQ(command, studio::ChartCommand::ZoomToFit);
    EXPECT_EQ(index, 0U);
}

TEST(StudioChartViewChecks, a_drag_reports_the_chart_content_coordinates) {
    // arrange: a view with a 32 pixel strip and recording callbacks
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    view.set_content_y(32.0f);
    float started_x = -1.0f;
    float started_y = -1.0f;
    float moved_x = -1.0f;
    float moved_y = -1.0f;
    int ended = 0;
    int canceled = 0;
    view.set_on_zoom_drag_started([&](const float x, const float y) {
        started_x = x;
        started_y = y;
    });
    view.set_on_zoom_drag_moved([&](const float x, const float y) {
        moved_x = x;
        moved_y = y;
    });
    view.set_on_zoom_drag_ended([&] { ended++; });
    view.set_on_zoom_drag_canceled([&] { canceled++; });
    // act: press, move and release inside the chart content
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 100.0f, .y = 120.0f};
    view.dispatch_event(press);
    albor::Event move;
    move.kind = albor::EventKind::PointerMove;
    move.position = albor::Point{.x = 150.0f, .y = 180.0f};
    view.dispatch_event(move);
    albor::Event release;
    release.kind = albor::EventKind::PointerUp;
    release.position = albor::Point{.x = 150.0f, .y = 180.0f};
    view.dispatch_event(release);
    // assert: the coordinates are relative to the chart content
    EXPECT_TRUE(press.handled);
    EXPECT_FLOAT_EQ(started_x, 100.0f);
    EXPECT_FLOAT_EQ(started_y, 88.0f);
    EXPECT_FLOAT_EQ(moved_x, 150.0f);
    EXPECT_FLOAT_EQ(moved_y, 148.0f);
    EXPECT_EQ(ended, 1);
    EXPECT_EQ(canceled, 0);
}

TEST(StudioChartViewChecks, a_resize_reports_the_settled_extent_once) {
    // arrange: a view whose clock only moves when the test moves it
    albor::BindingEngine engine;
    FakeTextEngine text;
    auto now = std::make_shared<std::chrono::steady_clock::time_point>();
    albor::TimerScheduler timers([now] { return *now; });
    studio::ChartView view(&engine, &text, &timers);
    std::vector<albor::Size> reports;
    view.set_on_resize_settled([&](const float width, const float height) { reports.push_back(albor::Size{.width = width, .height = height}); });
    // act: arrange a changed extent and let the settle delay pass
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // assert: the arrange alone reports nothing
    EXPECT_TRUE(reports.empty());
    *now += std::chrono::milliseconds(50);
    timers.fire_due();
    // assert: the settled extent reaches the presenter once
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_FLOAT_EQ(reports[0].width, 400.0f);
    EXPECT_FLOAT_EQ(reports[0].height, 300.0f);
}

TEST(StudioChartViewChecks, a_resize_in_flight_restarts_the_settle_timer) {
    // arrange: a view over a deterministic timer registry
    albor::BindingEngine engine;
    FakeTextEngine text;
    auto now = std::make_shared<std::chrono::steady_clock::time_point>();
    albor::TimerScheduler timers([now] { return *now; });
    studio::ChartView view(&engine, &text, &timers);
    std::vector<albor::Size> reports;
    view.set_on_resize_settled([&](const float width, const float height) { reports.push_back(albor::Size{.width = width, .height = height}); });
    // act: resize twice inside the settle window
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    *now += std::chrono::milliseconds(30);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 500.0f, .height = 300.0f}));
    *now += std::chrono::milliseconds(30);
    // assert: the first settle was replaced, so the panel is still quiet
    timers.fire_due();
    EXPECT_TRUE(reports.empty());
    // act: let the second settle come due
    *now += std::chrono::milliseconds(20);
    timers.fire_due();
    // assert: only the final extent is reported
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_FLOAT_EQ(reports[0].width, 500.0f);
    EXPECT_FLOAT_EQ(reports[0].height, 300.0f);
}

TEST(StudioChartViewChecks, the_destructor_cancels_a_pending_settle) {
    // arrange: a view destroyed while its settle is still armed
    albor::BindingEngine engine;
    FakeTextEngine text;
    auto now = std::make_shared<std::chrono::steady_clock::time_point>();
    albor::TimerScheduler timers([now] { return *now; });
    int reports = 0;
    {
        // the settle is armed against the view that dies at the scope end
        studio::ChartView view(&engine, &text, &timers);
        view.set_on_resize_settled([&](float, float) { reports++; });
        view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
        EXPECT_EQ(timers.size(), 1U);
    }
    // act: let the cancelled settle come due
    *now += std::chrono::milliseconds(50);
    timers.fire_due();
    // assert: the destroyed view is never called and the timer is gone
    EXPECT_EQ(reports, 0);
    EXPECT_EQ(timers.size(), 0U);
}

TEST(StudioChartViewChecks, a_chart_handle_is_only_shown_when_the_stack_has_two_charts) {
    // arrange: one frame over a slot
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 150.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 120.0f;
    view.set_frames({frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // assert: a single chart has nothing to reorder
    EXPECT_TRUE(view.chart_drag_handle(0).empty());
    // act: add the second chart
    view.set_frames({frame, frame});
    // assert: both handles sit inside the top right corner of their slot
    const albor::Rect first = view.chart_drag_handle(0);
    const albor::Rect second = view.chart_drag_handle(1);
    EXPECT_FLOAT_EQ(first.x, 314.0f);
    EXPECT_FLOAT_EQ(first.y, 16.0f);
    EXPECT_FLOAT_EQ(first.width, 28.0f);
    EXPECT_FLOAT_EQ(first.height, 18.0f);
    EXPECT_FLOAT_EQ(second.y, 166.0f);
}

TEST(StudioChartViewChecks, the_chart_handles_are_painted_above_the_layers) {
    // arrange: two charts with no series
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 150.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 120.0f;
    view.set_frames({frame, frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: paint the panel
    albor::PictureRecorder recorder;
    view.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: one 28x18 handle plate is painted at each slot corner
    int plates = 0;
    for (const albor::DrawCommand& command : commands) {
        // only the handle plates carry the handle extent
        if (command.kind() != albor::DrawCommandKind::Rect || command.rect_geometry().rect.width != 28.0f || command.rect_geometry().rect.height != 18.0f)
            continue;
        // the first plate sits on the first slot, the second one below it
        EXPECT_FLOAT_EQ(command.rect_geometry().rect.x, 314.0f);
        EXPECT_TRUE(command.rect_geometry().rect.y == 16.0f || command.rect_geometry().rect.y == 166.0f);
        // count the plates
        plates++;
    }
    EXPECT_EQ(plates, 2);
}

TEST(StudioChartViewChecks, dragging_a_chart_handle_reorders_the_stack) {
    // arrange: two charts and a recording reorder callback
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 150.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 120.0f;
    view.set_frames({frame, frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    int from = -1;
    int to = -1;
    int zoom_started = 0;
    view.set_on_chart_reordered([&](const int start, const int target) {
        from = start;
        to = target;
    });
    view.set_on_zoom_drag_started([&](float, float) { zoom_started++; });
    // act: grab the first handle and drop it at the bottom slot
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 322.0f, .y = 25.0f};
    view.dispatch_event(press);
    albor::Event move;
    move.kind = albor::EventKind::PointerMove;
    move.position = albor::Point{.x = 322.0f, .y = 250.0f};
    view.dispatch_event(move);
    albor::Event release;
    release.kind = albor::EventKind::PointerUp;
    release.position = albor::Point{.x = 322.0f, .y = 250.0f};
    view.dispatch_event(release);
    // assert: the grab reorders instead of zooming
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(zoom_started, 0);
    EXPECT_EQ(from, 0);
    EXPECT_EQ(to, 1);
}

TEST(StudioChartViewChecks, a_handle_release_in_place_keeps_the_order) {
    // arrange: two charts and a recording reorder callback
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 150.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 120.0f;
    view.set_frames({frame, frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    int reordered = 0;
    view.set_on_chart_reordered([&](int, int) { reordered++; });
    // act: press and release on the same handle
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 322.0f, .y = 25.0f};
    view.dispatch_event(press);
    albor::Event release;
    release.kind = albor::EventKind::PointerUp;
    release.position = albor::Point{.x = 322.0f, .y = 25.0f};
    view.dispatch_event(release);
    // assert: a plain grab moves nothing
    EXPECT_EQ(reordered, 0);
}

TEST(StudioChartViewChecks, a_pointer_cancel_aborts_a_reorder) {
    // arrange: a reorder drag in flight
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 150.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 120.0f;
    view.set_frames({frame, frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    int reordered = 0;
    view.set_on_chart_reordered([&](int, int) { reordered++; });
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 322.0f, .y = 25.0f};
    view.dispatch_event(press);
    // act: the input layer aborts the capture
    albor::Event cancel;
    cancel.kind = albor::EventKind::PointerCancel;
    cancel.position = albor::Point{.x = 322.0f, .y = 250.0f};
    view.dispatch_event(cancel);
    // act: the release that never arrives must not reorder
    albor::Event release;
    release.kind = albor::EventKind::PointerUp;
    release.position = albor::Point{.x = 322.0f, .y = 250.0f};
    view.dispatch_event(release);
    // assert: the canceled gesture moved nothing
    EXPECT_EQ(reordered, 0);
}

TEST(StudioChartViewChecks, the_semantic_node_offers_move_actions_inside_the_stack) {
    // arrange: two charts
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    ChartFrame frame;
    frame.frame_w = 400.0f;
    frame.frame_h = 150.0f;
    frame.plot_x = 50.0f;
    frame.plot_y = 10.0f;
    frame.plot_w = 300.0f;
    frame.plot_h = 120.0f;
    view.set_frames({frame, frame});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: read the semantic node
    const albor::SemanticNode node = view.semantic_node();
    // assert: only the moves with room in that direction are offered
    EXPECT_FALSE(node.has_action("chart-0-move-chart-up"));
    EXPECT_TRUE(node.has_action("chart-0-move-chart-down"));
    EXPECT_TRUE(node.has_action("chart-1-move-chart-up"));
    EXPECT_FALSE(node.has_action("chart-1-move-chart-down"));
}

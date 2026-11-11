#include "charts/chart_scene_builder.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

#include "albor/core/paint.h"
#include "albor/render/line_batch.h"
#include "albor/render/path_resource.h"
#include "albor/render/picture_resource.h"
#include "charts/chart_style.h"

namespace
{
    // convert one chart palette colour into an albor colour
    albor::Color to_color(const ChartColor& color) { return albor::Color::rgba(color.r, color.g, color.b, color.a); }

    // move a frame relative rect into the chart slot
    albor::Rect shifted(const albor::Rect& rect, const float offset_y) { return albor::Rect::from_origin_size(albor::Point{.x = rect.x, .y = rect.y + offset_y}, albor::Size{.width = rect.width, .height = rect.height}); }

    // append one contiguous run as a path contour
    void append_contour(std::vector<albor::PathVerb>& verbs, std::vector<albor::Point>& points, const std::vector<ChartPoint>& run) {
        // a run with no point has no contour to append
        if (run.empty())
            return;
        // start the contour at the first sample
        verbs.push_back(albor::PathVerb::MoveTo);
        // copy the first sample
        points.push_back(albor::Point{.x = run.front().x, .y = run.front().y});
        // join every later sample with a line
        for (std::size_t index = 1; index < run.size(); ++index) {
            // append the line verb
            verbs.push_back(albor::PathVerb::LineTo);
            // append the line end point
            points.push_back(albor::Point{.x = run[index].x, .y = run[index].y});
        }
    }

    // build the retained path a contour stream describes
    std::shared_ptr<const albor::PathResource> make_path(albor::ResourceFactory& resources, std::vector<albor::PathVerb>& verbs, std::vector<albor::Point>& points) {
        // a stream with no verb has no path
        if (verbs.empty())
            return nullptr;
        // stamp the retained path through the shared factory
        return resources.create_path(verbs, points);
    }

    // build the retained batch of independent line segments
    std::shared_ptr<const albor::LineBatch> make_batch(albor::ResourceFactory& resources, std::vector<albor::Point>& points) {
        // a batch with no segment has no line to draw
        if (points.empty())
            return nullptr;
        // stamp the retained batch through the shared factory
        return resources.create_line_batch(points);
    }

    // draw one retained path under the chart slot translation
    void draw_path(albor::PictureRecorder& recorder, const std::shared_ptr<const albor::PathResource>& path, const albor::Transform& transform, const albor::Color& color, const float width) {
        // a refused path draws nothing
        if (path == nullptr)
            return;
        // a fully transparent path draws nothing
        if (color.alpha <= 0.0f || width <= 0.0f)
            return;
        // the stroke carries the series colour and width
        const albor::Paint paint{.fill = albor::Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = color, .stroke_width = width};
        // record the retained contour
        recorder.draw_path(path, transform, paint);
    }

    // draw one retained batch of line segments under the chart slot translation
    void draw_batch(albor::PictureRecorder& recorder, const std::shared_ptr<const albor::LineBatch>& batch, const albor::Transform& transform, const albor::Color& color, const float width) {
        // a refused batch draws nothing
        if (batch == nullptr)
            return;
        // a fully transparent batch draws nothing
        if (color.alpha <= 0.0f || width <= 0.0f)
            return;
        // the stroke carries the grid colour and width
        const albor::StrokeStyle stroke{.color = color, .width = width};
        // record the retained segments
        recorder.draw_line_batch(batch, transform, stroke);
    }
} // namespace

studio::ChartSceneBuilder::ChartSceneBuilder(albor::ResourceFactory& resources, AlborChartTextMeasurer& text) :
    m_resources(&resources), m_text(&text), m_outline_resolver(text.glyph_outline_resolver()) {}

std::shared_ptr<const studio::ChartScene> studio::ChartSceneBuilder::build(const std::span<const ChartFrame> frames, const float width, const float height, const ChartPalette& palette, const std::span<const ChartTabSpec> tabs, const float tab_bar_height) const {
    // the scene under construction owns the slots and the picture
    auto scene = std::make_shared<ChartScene>();
    // remember the size and palette the picture was recorded for
    scene->width = width;
    // remember the panel height the slots were laid out for
    scene->height = height;
    // remember the palette the commands carry
    scene->palette = palette;
    // the strip only shows when the caller reserved a height for it
    const float strip_height = tabs.empty() ? 0.0f : tab_bar_height;
    // remember where the strip ends and the chart content begins
    scene->tab_bar_height = strip_height;
    // the chart content starts under the strip
    scene->content_y = strip_height;
    // a panel with no area has no picture to record
    if (width <= 0.0f || height <= 0.0f)
        return scene;
    // the strip picture records the dataset tabs in panel local coordinates
    albor::PictureRecorder strip_recorder;
    // record the dataset strip when the caller reserved one
    if (strip_height > 0.0f) {
        // record the tabs and their hit-test rectangles
        scene->tabs = draw_tabs(strip_recorder, tabs, width, strip_height, palette);
        // hand the scene the strip picture the view flattens above the charts
        scene->strip = m_resources->create_picture(strip_recorder);
    }
    // the base picture records every chart in panel local coordinates
    albor::PictureRecorder recorder;
    // a panel with no chart has no chart picture, but keeps its tab strip usable
    if (frames.empty())
        return scene;
    // the charts share the height the strip leaves equally
    const float content_height = std::max(0.0f, height - strip_height);
    // the charts share the content height equally
    const float chart_height = content_height / static_cast<float>(frames.size());
    // record one chart slot per frame
    for (std::size_t index = 0; index < frames.size(); ++index) {
        // the chart slot starts at its share of the content height under the strip
        const float offset_y = strip_height + chart_height * static_cast<float>(index);
        // the slot is shifted from the chart local frame coordinates
        const albor::Transform translation = albor::Transform::translation(0.0f, offset_y);
        // the frame this slot paints
        const ChartFrame& frame = frames[index];
        // remember the slot for hit tests and automation
        scene->slots.push_back(ChartSlot{.chart_index = index, .kind = frame.smith ? ChartKind::SMITH : ChartKind::XY, .frame = albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = offset_y}, albor::Size{.width = width, .height = chart_height}), .plot = shifted(albor::Rect::from_origin_size(albor::Point{.x = frame.plot_x, .y = frame.plot_y}, albor::Size{.width = frame.plot_w, .height = frame.plot_h}), offset_y)});
        // smith charts pad the gamma plane inside a white rect, which the layout sized
        if (frame.smith) {
            // the padded rect covers the gamma square and its labels
            const albor::Rect padded = albor::Rect::from_origin_size(albor::Point{.x = frame.plot_x - frame.smith_padding, .y = frame.plot_y - frame.smith_padding}, albor::Size{.width = frame.plot_w + 2.0f * frame.smith_padding, .height = frame.plot_h + 2.0f * frame.smith_padding});
            // fill the gamma plane background
            recorder.draw_rect(shifted(padded, offset_y), albor::Paint{.fill = to_color(palette.panel)});
            // the smith grid and series stay clipped to the gamma square
            recorder.save();
            // clip to the fixed +-1 plane
            recorder.clip_rect(shifted(albor::Rect::from_origin_size(albor::Point{.x = frame.plot_x, .y = frame.plot_y}, albor::Size{.width = frame.plot_w, .height = frame.plot_h}), offset_y));
            // separate the unit circle and axes from the reactive grid
            std::vector<albor::PathVerb> boundary_verbs;
            // the boundary path points
            std::vector<albor::Point> boundary_points;
            // separate the constant resistance and reactance grid
            std::vector<albor::PathVerb> grid_verbs;
            // the grid path points
            std::vector<albor::Point> grid_points;
            // append every smith grid run to the path matching its role
            for (const ChartSeriesFrame& run : frame.smith_grid) {
                // the boundary runs use the border colour
                if (run.boundary) {
                    // append the boundary contour
                    append_contour(boundary_verbs, boundary_points, run.points);
                    // next run
                    continue;
                }
                // append the reactive grid contour
                append_contour(grid_verbs, grid_points, run.points);
            }
            // draw the reactive grid under the series
            draw_path(recorder, make_path(*m_resources, grid_verbs, grid_points), translation, to_color(palette.grid), CHART_MAJOR_GRID_SIZE);
            // draw the unit circle and the two axes above the grid
            draw_path(recorder, make_path(*m_resources, boundary_verbs, boundary_points), translation, to_color(palette.border), CHART_MAJOR_GRID_SIZE);
            // append every smith series as its own retained path
            for (const ChartSeriesFrame& run : frame.series) {
                // build the contour of this series run
                std::vector<albor::PathVerb> series_verbs;
                // the series path points
                std::vector<albor::Point> series_points;
                // append the contiguous run
                append_contour(series_verbs, series_points, run.points);
                // draw the retained series path
                draw_path(recorder, make_path(*m_resources, series_verbs, series_points), translation, to_color(run.color), CHART_LINE_WEIGHT);
            }
            // the series clip ends with the plot
            recorder.restore();
            // draw the smith tick labels, which sit outside the gamma square
            for (const ChartFrame::SmithLabel& label : frame.smith_labels) {
                // measure the label once and reuse the shaped run
                const albor::TextLayout& layout = m_text->layout(label.label);
                // centre the label on its anchor
                const albor::Point origin{.x = label.x - layout.size.width * 0.5f, .y = label.y - layout.size.height * 0.5f + offset_y};
                // record the text command
                draw_text(recorder, layout, origin, albor::Paint{.fill = to_color(palette.text)});
            }
            // the legend block is shared with the cartesian charts
            draw_legend(recorder, frame, palette, offset_y);
            // the smith slot is complete
            continue;
        }
        // fill the cartesian plot background
        recorder.draw_rect(shifted(albor::Rect::from_origin_size(albor::Point{.x = frame.plot_x, .y = frame.plot_y}, albor::Size{.width = frame.plot_w, .height = frame.plot_h}), offset_y), albor::Paint{.fill = to_color(palette.panel)});
        // stroke the plot border without filling it
        recorder.draw_rect(shifted(albor::Rect::from_origin_size(albor::Point{.x = frame.plot_x, .y = frame.plot_y}, albor::Size{.width = frame.plot_w, .height = frame.plot_h}), offset_y), albor::Paint{.fill = albor::Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = to_color(palette.border), .stroke_width = 1.0f});
        // the major grid segments of this chart
        std::vector<albor::Point> major_grid;
        // the minor grid segments grouped by their density alpha
        std::map<int, std::vector<albor::Point>> minor_grid;
        // the tick marks of this chart
        std::vector<albor::Point> tick_marks;
        // the abscissa grid lines run from the plot bottom to the plot top
        for (const ChartGridLine& line : frame.x_grid) {
            // a line the density gate hid draws nothing
            if (line.alpha <= 0.0f)
                continue;
            // a major line joins the major batch
            if (line.major) {
                // the major segment starts at the plot bottom
                major_grid.push_back(albor::Point{.x = line.pixel_pos, .y = frame.plot_y});
                // the major segment ends at the plot top
                major_grid.push_back(albor::Point{.x = line.pixel_pos, .y = frame.plot_y + frame.plot_h});
                // the major line is settled
                continue;
            }
            // the minor segment joins the bucket its density alpha names
            std::vector<albor::Point>& minor = minor_grid[static_cast<int>(std::lround(std::clamp(line.alpha, 0.0f, 1.0f) * 255.0f))];
            // the minor segment starts at the plot bottom
            minor.push_back(albor::Point{.x = line.pixel_pos, .y = frame.plot_y});
            // the minor segment ends at the plot top
            minor.push_back(albor::Point{.x = line.pixel_pos, .y = frame.plot_y + frame.plot_h});
        }
        // every enabled ordinate axis contributes horizontal grid lines
        for (const ChartAxisFrame& axis : frame.y_axes) {
            // a disabled axis draws no grid
            if (!axis.enabled)
                continue;
            // append each visible grid line of this axis
            for (const ChartGridLine& line : axis.grid_lines) {
                // a line the density gate hid draws nothing
                if (line.alpha <= 0.0f)
                    continue;
                // a major line joins the major batch
                if (line.major) {
                    // the major segment starts at the plot west edge
                    major_grid.push_back(albor::Point{.x = frame.plot_x, .y = line.pixel_pos});
                    // the major segment ends at the plot east edge
                    major_grid.push_back(albor::Point{.x = frame.plot_x + frame.plot_w, .y = line.pixel_pos});
                    // the major line is settled
                    continue;
                }
                // the minor segment joins the bucket its density alpha names
                std::vector<albor::Point>& minor = minor_grid[static_cast<int>(std::lround(std::clamp(line.alpha, 0.0f, 1.0f) * 255.0f))];
                // the minor segment starts at the plot west edge
                minor.push_back(albor::Point{.x = frame.plot_x, .y = line.pixel_pos});
                // the minor segment ends at the plot east edge
                minor.push_back(albor::Point{.x = frame.plot_x + frame.plot_w, .y = line.pixel_pos});
            }
        }
        // draw the major grid with its solid opacity
        draw_batch(recorder, make_batch(*m_resources, major_grid), translation, to_color(palette.grid), CHART_MAJOR_GRID_SIZE);
        // draw each minor density bucket with its own alpha
        for (auto& [alpha, points] : minor_grid) {
            // the bucket alpha scales the grid colour
            const albor::Color minor_grid_color = albor::Color::rgba(palette.grid.r, palette.grid.g, palette.grid.b, palette.grid.a * (static_cast<float>(alpha) / 255.0f));
            // draw the retained minor segments under the major grid
            draw_batch(recorder, make_batch(*m_resources, points), translation, minor_grid_color, CHART_MINOR_GRID_SIZE);
        }
        // the abscissa tick marks extend below the datum line
        for (const ChartTick& tick : frame.x_ticks) {
            // a pruned tick has no mark to draw
            if (!tick.show_label)
                continue;
            // choose the major or minor tick length
            const float length = tick.major ? CHART_MAJOR_TICK_LEN : CHART_MINOR_TICK_LEN;
            // the mark starts at the datum line
            tick_marks.push_back(albor::Point{.x = tick.pixel_pos, .y = frame.x_datum});
            // the mark ends at the chosen length
            tick_marks.push_back(albor::Point{.x = tick.pixel_pos, .y = frame.x_datum + length});
        }
        // every enabled ordinate axis contributes its own tick marks
        for (const ChartAxisFrame& axis : frame.y_axes) {
            // a disabled axis draws no ticks
            if (!axis.enabled)
                continue;
            // append each visible tick mark of this axis
            for (const ChartTick& tick : axis.ticks) {
                // a pruned tick has no mark to draw
                if (!tick.show_label)
                    continue;
                // choose the major or minor tick length
                const float length = tick.major ? CHART_MAJOR_TICK_LEN : CHART_MINOR_TICK_LEN;
                // the west axis extends its ticks into the plot
                if (!axis.opposite) {
                    // the mark starts at the west datum line
                    tick_marks.push_back(albor::Point{.x = frame.plot_x, .y = tick.pixel_pos - length * 0.5f});
                    // the mark ends inside the plot
                    tick_marks.push_back(albor::Point{.x = frame.plot_x + length, .y = tick.pixel_pos - length * 0.5f});
                    // next tick
                    continue;
                }
                // the east axis extends its ticks into the plot
                tick_marks.push_back(albor::Point{.x = axis.datum - length, .y = tick.pixel_pos - length * 0.5f});
                // the east mark ends on the datum line
                tick_marks.push_back(albor::Point{.x = axis.datum, .y = tick.pixel_pos - length * 0.5f});
            }
        }
        // draw every tick mark in the border colour
        draw_batch(recorder, make_batch(*m_resources, tick_marks), translation, to_color(palette.border), 1.0f);
        // the second east axis needs its own datum line when it stacks outward
        if (!frame.y_axes[2].ticks.empty()) {
            // the vertical datum line shares the tick colour
            std::vector<albor::Point> datum_line;
            // the line starts at the plot top
            datum_line.push_back(albor::Point{.x = frame.y_axes[2].datum, .y = frame.plot_y});
            // the line ends at the plot bottom
            datum_line.push_back(albor::Point{.x = frame.y_axes[2].datum, .y = frame.plot_y + frame.plot_h});
            // draw the retained datum line
            draw_batch(recorder, make_batch(*m_resources, datum_line), translation, to_color(palette.border), 1.0f);
        }
        // the series stay clipped to the plot rect
        recorder.save();
        // clip the series and their filling to the plot
        recorder.clip_rect(shifted(albor::Rect::from_origin_size(albor::Point{.x = frame.plot_x, .y = frame.plot_y}, albor::Size{.width = frame.plot_w, .height = frame.plot_h}), offset_y));
        // record one retained path per contiguous series run
        for (const ChartSeriesFrame& run : frame.series) {
            // build the contour of this series run
            std::vector<albor::PathVerb> series_verbs;
            // the series path points
            std::vector<albor::Point> series_points;
            // append the contiguous run
            append_contour(series_verbs, series_points, run.points);
            // draw the retained series path
            draw_path(recorder, make_path(*m_resources, series_verbs, series_points), translation, to_color(run.color), CHART_LINE_WEIGHT);
        }
        // the series clip ends with the plot
        recorder.restore();
        // draw every abscissa label the prune left visible
        for (const ChartTick& tick : frame.x_ticks) {
            // a pruned tick has no label to draw
            if (!tick.show_label)
                continue;
            // measure the label once and reuse the shaped run
            const albor::TextLayout& layout = m_text->layout(tick.label);
            // the furthest left the label may start and still stay inside the panel
            const float label_max_x = std::max(0.0f, frame.frame_w - layout.size.width);
            // centre the label under the datum line, clamped so the edge labels stay inside the panel
            const float label_x = std::clamp(tick.pixel_pos - layout.size.width * 0.5f, 0.0f, label_max_x);
            // build the text origin under the datum line
            const albor::Point origin{.x = label_x, .y = frame.x_datum + CHART_LABEL_PADDING + offset_y};
            // record the text command
            draw_text(recorder, layout, origin, albor::Paint{.fill = to_color(palette.text)});
        }
        // draw every ordinate label the prune left visible
        for (const ChartAxisFrame& axis : frame.y_axes) {
            // a disabled axis has no label
            if (!axis.enabled)
                continue;
            // append each visible label of this axis
            for (const ChartTick& tick : axis.ticks) {
                // a pruned tick has no label to draw
                if (!tick.show_label)
                    continue;
                // measure the label once and reuse the shaped run
                const albor::TextLayout& layout = m_text->layout(tick.label);
                // the west axis right aligns its labels against the plot
                const float x = axis.opposite ? axis.datum + CHART_LABEL_PADDING : frame.plot_x - CHART_LABEL_PADDING - layout.size.width;
                // centre the label vertically on the tick
                const albor::Point origin{.x = x, .y = tick.pixel_pos - layout.size.height * 0.5f + offset_y};
                // record the text command
                draw_text(recorder, layout, origin, albor::Paint{.fill = to_color(palette.text)});
            }
        }
        // the legend block is shared with the smith charts
        draw_legend(recorder, frame, palette, offset_y);
    }
    // freeze the recorded base command list into a retained picture
    scene->base = m_resources->create_picture(recorder);
    // hand the scene to the view
    return scene;
}

std::vector<studio::ChartTab> studio::ChartSceneBuilder::draw_tabs(albor::PictureRecorder& recorder, const std::span<const ChartTabSpec> tabs, const float width, const float height, const ChartPalette& palette) const {
    // the strip plate separates the tabs from the toolbar above
    recorder.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = width, .height = height}), albor::Paint{.fill = to_color(palette.background)});
    // the hairlines under the strip close it against the chart pane
    recorder.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = height - 1.0f}, albor::Size{.width = width, .height = 1.0f}), albor::Paint{.fill = to_color(palette.border)});
    // the strip insets its first tab and leaves the same room at the east edge
    constexpr float STRIP_INSET = 8.0f;
    // the gap between neighbouring tabs
    constexpr float TAB_GAP = 4.0f;
    // the narrowest tab that still shows a label and its close slot
    constexpr float MIN_TAB_WIDTH = 32.0f;
    // the natural width of every tab
    std::vector<float> widths;
    // measure every title once through the shared cache
    widths.reserve(tabs.size());
    // the width the natural tabs occupy
    float natural_total = 0.0f;
    // measure the natural widths first so an overflowing strip can shrink them
    for (const ChartTabSpec& spec : tabs) {
        // measure the title once through the shared cache
        const albor::TextLayout& layout = m_text->layout(spec.title);
        // the close slot needs room beyond the label
        const float natural = std::max(96.0f, layout.size.width + (spec.closable ? 40.0f : 24.0f));
        // keep the natural width
        widths.push_back(natural);
        // accumulate the strip width including the gap
        natural_total += natural + TAB_GAP;
    }
    // the width the strip actually offers
    const float available = std::max(0.0f, width - 2.0f * STRIP_INSET);
    // a strip that overflows shares its width equally between the tabs
    if (!tabs.empty() && natural_total - TAB_GAP > available) {
        // the share every tab receives
        const float share = std::max(MIN_TAB_WIDTH, (available - TAB_GAP * static_cast<float>(tabs.size() - 1)) / static_cast<float>(tabs.size()));
        // shrink every natural width to its share
        for (float& tab_width : widths)
            tab_width = std::min(tab_width, share);
        // a tab count the minimum width cannot fit still shrinks every tab so none disappears
        const float gaps = TAB_GAP * static_cast<float>(tabs.size() - 1);
        // the width the shrunk tabs occupy
        const float shrunk_total = std::accumulate(widths.begin(), widths.end(), 0.0f) + gaps;
        // scale every tab down when the minimum share still overflows
        if (shrunk_total > available && shrunk_total > gaps) {
            // the factor that fits every tab into the strip
            const float factor = std::max(0.05f, (available - gaps) / (shrunk_total - gaps));
            // apply the fit factor to every tab
            for (float& tab_width : widths)
                tab_width = std::max(8.0f, tab_width * factor);
        }
    }
    // the hit-test rectangles the view reports
    std::vector<ChartTab> recorded;
    // the strip starts at its west inset
    float x = STRIP_INSET;
    // record every tab in dataset order
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        // the tab spec this slot draws
        const ChartTabSpec& spec = tabs[index];
        // a tab that would pass the strip edge is clipped to what remains
        const float clipped_width = std::min(widths[index], std::max(0.0f, width - STRIP_INSET - x));
        // a tab with no remaining width ends the strip
        if (clipped_width <= 0.0f)
            break;
        // the tab rectangle spans the strip height
        const albor::Rect bounds = albor::Rect::from_origin_size(albor::Point{.x = x, .y = 0.0f}, albor::Size{.width = clipped_width, .height = height});
        // the active tab reads as the pane it reveals
        if (spec.active)
            recorder.draw_rect(bounds, albor::Paint{.fill = to_color(palette.panel)});
        // clip the label and the close slot to the tab so neither bleeds into the next one
        recorder.save();
        // the clip follows the tab bounds
        recorder.clip_rect(bounds);
        // the label sits inside the tab with its west padding
        const albor::TextLayout& layout = m_text->layout(spec.title);
        // the label origin inside the tab
        const albor::Point text_origin{.x = x + 12.0f, .y = (height - layout.size.height) * 0.5f};
        // draw the tab title in the active or muted ink
        draw_text(recorder, layout, text_origin, albor::Paint{.fill = to_color(spec.active ? palette.text : palette.muted)});
        // the active tab carries the accent underline
        if (spec.active)
            recorder.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = x, .y = height - 2.0f}, albor::Size{.width = clipped_width, .height = 2.0f}), albor::Paint{.fill = albor::Color::rgba(0.0f, 0.48f, 1.0f, 1.0f)});
        // the close affordance sits at the tab east edge
        albor::Rect close_bounds;
        // a tab wide enough carries the close cross
        if (spec.closable && clipped_width >= 24.0f) {
            // the close slot is centred on the strip height
            close_bounds = albor::Rect::from_origin_size(albor::Point{.x = x + clipped_width - 18.0f, .y = (height - 16.0f) * 0.5f}, albor::Size{.width = 16.0f, .height = 16.0f});
            // the two strokes of the cross
            std::vector<albor::Point> cross = {{close_bounds.x + 4.0f, close_bounds.y + 4.0f}, {close_bounds.x + 12.0f, close_bounds.y + 12.0f}, {close_bounds.x + 12.0f, close_bounds.y + 4.0f}, {close_bounds.x + 4.0f, close_bounds.y + 12.0f}};
            // draw the cross in the muted ink
            draw_batch(recorder, make_batch(*m_resources, cross), albor::Transform{}, to_color(palette.muted), 1.0f);
        }
        // the label and close clip ends with the tab
        recorder.restore();
        // remember the tab for the view
        recorded.push_back(ChartTab{.title = spec.title, .active = spec.active, .closable = spec.closable, .bounds = bounds, .close_bounds = close_bounds});
        // advance past the tab and its gap
        x += clipped_width + TAB_GAP;
    }
    // hand the recorded tabs to the scene
    return recorded;
}

void studio::ChartSceneBuilder::draw_text(albor::PictureRecorder& recorder, const albor::TextLayout& layout, const albor::Point origin, const albor::Paint& paint) const {
    // an empty run carries no glyph to draw
    if (layout.empty())
        return;
    // an embedded face reaches the surface as glyph outlines, because the renderer holds no typeface for it
    if (layout.run->font_hash() != 0 && layout.run->shape_source() == albor::ShapeSource::Embedded && m_outline_resolver) {
        // every line places its own baseline under the run origin
        for (const albor::ShapedRun::Line& line : layout.run->lines()) {
            // the baseline is the run origin under the line's own origin
            const albor::Point baseline{.x = origin.x + line.origin.x, .y = origin.y + line.origin.y};
            // every glyph becomes one retained outline path
            for (const albor::ShapedGlyph& glyph : line.glyphs) {
                // the key names the shape the cache holds
                const albor::GlyphOutlineKey key{.font_hash = layout.run->font_hash(), .glyph_id = glyph.id, .font_size = layout.run->font_size()};
                // a cached outline answers before the resolver is asked
                const std::shared_ptr<const albor::GlyphOutline> outline = m_outline_cache.lookup(key, m_outline_resolver);
                // a glyph the resolver refuses draws nothing
                if (outline == nullptr || outline->empty())
                    continue;
                // the glyph sits at its own offset from the line baseline
                const albor::Point position{.x = baseline.x + glyph.position.x, .y = baseline.y + glyph.position.y};
                // record the outline as a filled path at the glyph position
                recorder.draw_glyph_outline(*outline, albor::Transform::translation(position.x, position.y), paint);
            }
        }
        // the embedded run has been expanded
        return;
    }
    // a platform run reaches the surface through the text command
    recorder.draw_text(layout, origin, paint);
}

void studio::ChartSceneBuilder::draw_legend(albor::PictureRecorder& recorder, const ChartFrame& frame, const ChartPalette& palette, const float offset_y) const {
    // a chart with no series has no legend block
    if (frame.legend.empty())
        return;
    // the block starts at its inner west padding
    float x = frame.legend_x + CHART_LEGEND_INNER_PADDING_X;
    // the block starts at its inner north padding
    const float top = frame.legend_y + offset_y;
    // append each legend entry in series order
    for (const ChartLegendItem& item : frame.legend) {
        // measure the label once and reuse the shaped run
        const albor::TextLayout& layout = m_text->layout(item.name);
        // centre the icon slot in the block height
        const float icon_slot_y = top + (frame.legend_h - 14.0f) * 0.5f;
        // the swatch is the inset square inside the icon slot
        recorder.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = x + 2.0f, .y = icon_slot_y + 2.0f}, albor::Size{.width = 10.0f, .height = 10.0f}), albor::Paint{.fill = to_color(item.color)});
        // centre the label beside the icon slot
        const float text_y = top + (frame.legend_h - layout.size.height) * 0.5f;
        // record the legend label
        draw_text(recorder, layout, albor::Point{.x = x + 14.0f, .y = text_y}, albor::Paint{.fill = to_color(palette.text)});
        // advance past the icon slot, the label and the entry spacing
        x += 14.0f + layout.size.width + CHART_LEGEND_SPACING_X;
    }
}

std::shared_ptr<const studio::ChartOverlay> studio::ChartSceneBuilder::build_zoom_overlay(const albor::Rect& selection) const {
    // an empty selection has no overlay to draw
    if (selection.empty() || selection.width <= 0.0f || selection.height <= 0.0f)
        return nullptr;
    // the overlay records a translucent fill and a solid border
    albor::PictureRecorder recorder;
    // fill the selection rectangle with the accent wash
    recorder.draw_rect(selection, albor::Paint{.fill = albor::Color::rgba(0.0f, 0.48f, 1.0f, 0.22f)});
    // stroke the selection rectangle with the accent line
    recorder.draw_rect(selection, albor::Paint{.fill = albor::Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = albor::Color::rgba(0.0f, 0.48f, 1.0f, 0.90f), .stroke_width = 1.0f});
    // freeze the overlay command list into a retained picture
    auto overlay = std::make_shared<ChartOverlay>();
    // keep the picture the view draws above the base
    overlay->picture = m_resources->create_picture(recorder);
    // keep the damage rect the view invalidates
    overlay->bounds = selection;
    // hand the overlay to the view
    return overlay;
}

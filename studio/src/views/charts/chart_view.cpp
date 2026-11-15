#include "views/charts/chart_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "albor/application/application.h"
#include "albor/core/constraints.h"
#include "albor/core/paint.h"
#include "albor/widgets/menu.h"
#include "albor/widgets/menu_item.h"
#include "albor/window/popup_host.h"
#include "charts/chart_style.h"

namespace
{
    // mix one integer into the running key
    void mix(std::uint64_t& key, const std::uint64_t value) {
        // combine the value with the running seed
        key ^= value + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2);
    }

    // mix one float into the running key
    void mix_float(std::uint64_t& key, const float value) {
        // hash the float through the standard hasher
        mix(key, static_cast<std::uint64_t>(std::hash<float>{}(value)));
    }

    // mix one double into the running key
    void mix_double(std::uint64_t& key, const double value) {
        // hash the double through the standard hasher
        mix(key, static_cast<std::uint64_t>(std::hash<double>{}(value)));
    }

    // mix one string into the running key
    void mix_text(std::uint64_t& key, const std::string& text) {
        // the length keeps concatenated names distinct
        mix(key, text.size());
        // every character contributes to the key
        for (const char character : text)
            mix(key, static_cast<unsigned char>(character));
    }

    // mix one chart color into the running key
    void mix_color(std::uint64_t& key, const ChartColor& color) {
        // every channel changes the base
        mix_float(key, color.r);
        mix_float(key, color.g);
        mix_float(key, color.b);
        mix_float(key, color.a);
    }

    // mix one palette into the running key
    void mix_palette(std::uint64_t& key, const ChartPalette& palette) {
        // every role changes the base colors
        mix_color(key, palette.text);
        mix_color(key, palette.muted);
        mix_color(key, palette.background);
        mix_color(key, palette.panel);
        mix_color(key, palette.border);
        mix_color(key, palette.grid);
    }

    // mix one tick into the running key
    void mix_tick(std::uint64_t& key, const ChartTick& tick) {
        // the value, position and label change the base
        mix_double(key, tick.value);
        mix_float(key, tick.pixel_pos);
        mix(key, tick.major);
        mix(key, tick.show_label);
        mix_text(key, tick.label);
        mix_float(key, tick.label_width);
    }

    // mix one grid line into the running key
    void mix_grid(std::uint64_t& key, const ChartGridLine& line) {
        // the position, class and density change the base
        mix_float(key, line.pixel_pos);
        mix(key, line.major);
        mix_float(key, line.alpha);
    }

    // mix one y axis into the running key
    void mix_axis(std::uint64_t& key, const ChartAxisFrame& axis) {
        // only an enabled axis paints ticks, grid and labels
        mix(key, axis.enabled);
        // every tick and grid line changes the base
        for (const ChartTick& tick : axis.ticks)
            mix_tick(key, tick);
        for (const ChartGridLine& line : axis.grid_lines)
            mix_grid(key, line);
    }
    // the drag handle width
    constexpr float DRAG_HANDLE_WIDTH = 28.0f;

    // the drag handle height
    constexpr float DRAG_HANDLE_HEIGHT = 18.0f;

    // the distance from the plot right edge to the handle
    constexpr float DRAG_HANDLE_INSET = 8.0f;

    // the distance from the plot top to the handle
    constexpr float DRAG_HANDLE_TOP_INSET = 6.0f;

    // the width of one grip bar
    constexpr float DRAG_HANDLE_BAR_WIDTH = 8.0f;

    // the height of one grip bar
    constexpr float DRAG_HANDLE_BAR_HEIGHT = 2.0f;

    // the gap between two grip bars
    constexpr float DRAG_HANDLE_BAR_GAP = 2.0f;

    // the opacity of a resting handle
    constexpr float DRAG_HANDLE_REST_ALPHA = 0.4f;

    // the opacity of a hovered or dragged handle
    constexpr float DRAG_HANDLE_LIT_ALPHA = 1.0f;
} // namespace

studio::ChartView::ChartView(albor::BindingEngine* engine, albor::TextEngine* text, albor::TimerScheduler* timers) :
    albor::Widget(engine, "charts_view"), m_text(text), m_outline_resolver(text != nullptr ? text->glyph_outline_resolver() : albor::GlyphOutlineResolver{}), m_timers(timers), m_menu_text(text) {
    // a secondary press asks the panel for its chart context menu
    set_on_context_menu([this](const albor::Point point) { open_context_menu(point); });
}

const albor::TextLayout& studio::ChartView::label_layout(const std::string& text) {
    // an empty cache entry stands in for a view without a text engine
    static const albor::TextLayout empty;
    // a view without a text engine shapes nothing
    if (m_text == nullptr)
        return empty;
    // reuse the run a previous paint shaped
    if (const auto found = m_labels.find(text); found != m_labels.end())
        return found->second;
    // shape the label at the shared chart font size
    const albor::TextLayout shaped = m_text->measure(text, albor::TextEngine::UNBOUNDED_WIDTH, CHART_LABEL_FONT_SIZE);
    // keep the shaped run for the next paint
    return m_labels.emplace(text, shaped).first->second;
}

void studio::ChartView::draw_text(albor::RenderContext& context, const albor::TextLayout& layout, const albor::Point origin, const albor::Paint& paint) {
    // an empty run carries no glyph to draw
    if (layout.empty())
        return;
    // an embedded face reaches the surface as glyph outlines, because the renderer holds no typeface for it
    if (layout.run->font_hash() != 0 && layout.run->shape_source() == albor::ShapeSource::Embedded && m_outline_resolver) {
        // every line places its own baseline under the run origin
        for (const albor::ShapedRun::Line& line : layout.run->lines()) {
            // the baseline is the run origin under the line's own origin
            const albor::Point baseline{.x = origin.x + line.origin.x, .y = origin.y + line.origin.y};
            // every glyph becomes one outline path
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
                // draw the outline at the glyph position
                context.draw_glyph_outline(*outline, albor::Transform::translation(position.x, position.y), paint);
            }
        }
        // the embedded run has been expanded
        return;
    }
    // a platform run reaches the surface through the text command
    context.draw_text(layout, origin, paint);
}

studio::ChartView::~ChartView() {
    // the menu surface dies with the panel that anchors it
    destroy_context_menu();
    // drop the pending settle so it cannot reach a destroyed view
    if (m_timers != nullptr && m_timers->scheduled(m_resize_timer))
        (void)m_timers->cancel(m_resize_timer);
    // tell the presenter that borrowed this view that it is gone
    if (m_on_destroyed)
        m_on_destroyed();
}

void studio::ChartView::set_palette(const ChartPalette& palette) {
    // keep the palette the next frame paints with
    m_palette = palette;
    // the whole panel changed, so the whole panel redraws
    invalidate();
}

void studio::ChartView::set_accessible_summary(std::string summary) { m_summary = std::move(summary); }

void studio::ChartView::set_frames(std::vector<ChartFrame> frames) {
    // a frame republish ends a drag recorded against the old geometry
    cancel_drag();
    // the reorder gesture ends with the frames it named
    cancel_reorder();
    // keep the frames the base layers are recorded from
    m_frames = std::move(frames);
    // the chart content changed, so the panel redraws
    invalidate();
}

void studio::ChartView::set_on_zoom_drag_started(std::function<void(float, float)> handler) { m_on_zoom_drag_started = std::move(handler); }

void studio::ChartView::set_on_zoom_drag_moved(std::function<void(float, float)> handler) { m_on_zoom_drag_moved = std::move(handler); }

void studio::ChartView::set_on_zoom_drag_ended(std::function<void()> handler) { m_on_zoom_drag_ended = std::move(handler); }

void studio::ChartView::set_on_zoom_drag_canceled(std::function<void()> handler) { m_on_zoom_drag_canceled = std::move(handler); }

void studio::ChartView::set_on_hover_moved(std::function<void(float, float)> handler) { m_on_hover_moved = std::move(handler); }

void studio::ChartView::set_on_hover_ended(std::function<void()> handler) { m_on_hover_ended = std::move(handler); }

void studio::ChartView::set_on_chart_command(std::function<void(ChartCommand, std::size_t)> handler) { m_on_chart_command = std::move(handler); }

void studio::ChartView::set_on_chart_reordered(std::function<void(int, int)> handler) { m_on_chart_reordered = std::move(handler); }

void studio::ChartView::set_step_tool_available(const bool available) {
    // an unchanged availability keeps the menu as it is
    if (m_step_tool_available == available)
        return;
    // remember whether the tool applies
    m_step_tool_available = available;
    // the context menu and the semantic actions follow
    invalidate();
}

void studio::ChartView::run_chart_command(const ChartCommand command, const std::size_t chart_index) const {
    // a command reaches the presenter only while the view is wired to it
    if (m_on_chart_command)
        m_on_chart_command(command, chart_index);
}

void studio::ChartView::set_zoom_selection(std::optional<albor::Rect> selection) {
    // keep the transient selection the next frame paints
    m_zoom_selection = std::move(selection);
    // the selection covers a narrow band
    invalidate();
}

void studio::ChartView::cancel_drag() {
    // a reorder shares the pointer with the zoom drag, so it ends with it
    cancel_reorder();
    // a view without a live drag has nothing to cancel
    if (!m_dragging)
        return;
    // the drag no longer owns the pointer
    m_dragging = false;
    // drop the selection without changing the charts
    if (m_on_zoom_drag_canceled)
        m_on_zoom_drag_canceled();
}

void studio::ChartView::set_on_resize_settled(std::function<void(float, float)> handler) { m_on_resize_settled = std::move(handler); }

void studio::ChartView::schedule_resize_settle() {
    // a view without a timer registry leaves the presenter to rebuild on its own
    if (m_timers == nullptr)
        return;
    // cancel the settle a previous arrange armed
    if (m_timers->scheduled(m_resize_timer))
        (void)m_timers->cancel(m_resize_timer);
    // arm one settle after the resize has been quiet for a moment
    m_resize_timer = m_timers->schedule_once(
        [this] {
            // the settle consumed the timer
            m_resize_timer = 0;
            // hand the settled extent to the presenter
            if (m_on_resize_settled)
                m_on_resize_settled(bounds().width, bounds().height);
        },
        std::chrono::milliseconds(50));
}

void studio::ChartView::set_on_destroyed(std::function<void()> handler) { m_on_destroyed = std::move(handler); }

float studio::ChartView::viewport_width() const { return bounds().width; }

float studio::ChartView::viewport_height() const { return bounds().height; }

std::size_t studio::ChartView::base_record_count() const { return m_base_record_count; }

std::size_t studio::ChartView::series_record_count() const { return m_series_record_count; }

std::size_t studio::ChartView::legend_record_count() const { return m_legend_record_count; }

void studio::ChartView::measure(const albor::SizeConstraints& constraints, albor::Size& out_size) {
    // measure the children of the widget, which the panel has none of
    albor::Size children{};
    // let the base widget measure its children
    albor::UiNode::measure(constraints, children);
    // the chart panel asks for a useful default when the host leaves it unbounded
    albor::Size content{.width = 640.0f, .height = 480.0f};
    // a bounded width is what the panel fills
    if (std::isfinite(constraints.maximum.width))
        content.width = constraints.maximum.width;
    // a bounded height is what the panel fills
    if (std::isfinite(constraints.maximum.height))
        content.height = constraints.maximum.height;
    // clamp the fill into the offered constraints
    m_measured_size = constraints.constrain(content);
    // the preferred size follows the measured one
    m_preferred_size = m_measured_size;
    // hand the parent the extent
    out_size = m_measured_size;
}

std::uint64_t studio::ChartView::base_key(const ChartFrame& frame, const BaseLayout& layout) const {
    // the key under construction
    std::uint64_t key = 0;
    // the shared layout decides the plot rect and the label columns
    mix_float(key, layout.view_width);
    mix_float(key, layout.slot_height);
    mix_float(key, layout.plot_x);
    mix_float(key, layout.plot_y);
    mix_float(key, layout.plot_width);
    mix_float(key, layout.plot_height);
    mix_float(key, layout.column_width);
    // the frame kind and recorded plot decide the mapped geometry
    mix(key, frame.smith);
    mix_float(key, frame.frame_w);
    mix_float(key, frame.frame_h);
    mix_float(key, frame.plot_x);
    mix_float(key, frame.plot_y);
    mix_float(key, frame.plot_w);
    mix_float(key, frame.plot_h);
    // the abscissa ticks and grid decide the labels and lines
    for (const ChartTick& tick : frame.x_ticks)
        mix_tick(key, tick);
    for (const ChartGridLine& line : frame.x_grid)
        mix_grid(key, line);
    // every ordinate axis decides the ticks, grid and labels
    for (const ChartAxisFrame& axis : frame.y_axes)
        mix_axis(key, axis);
    // the smith grid and labels are part of the base
    mix_float(key, frame.smith_padding);
    for (const ChartSeriesFrame& run : frame.smith_grid) {
        mix_text(key, run.name);
        mix_color(key, run.color);
        mix(key, run.boundary);
        for (const ChartPoint& point : run.points) {
            mix_float(key, point.x);
            mix_float(key, point.y);
        }
    }
    for (const ChartFrame::SmithLabel& label : frame.smith_labels) {
        mix_text(key, label.label);
        mix_float(key, label.x);
        mix_float(key, label.y);
    }
    // the legend presence changes the plot height the frame carries
    mix(key, !frame.legend.empty());
    // the palette changes the base colors
    mix_palette(key, m_palette);
    // avoid the empty-cache sentinel
    return key != 0 ? key : 1;
}

void studio::ChartView::paint_base(albor::RenderContext& context, const ChartFrame& frame, const BaseLayout& layout) {
    // a smith chart paints its own gamma plane
    if (frame.smith) {
        // paint the gamma plane instead of cartesian axes
        paint_smith_base(context, frame, layout);
        // the smith base is complete
        return;
    }
    // the chart-local plot rect
    const albor::Rect plot = albor::Rect::from_origin_size(albor::Point{.x = layout.plot_x, .y = layout.plot_y}, albor::Size{.width = layout.plot_width, .height = layout.plot_height});
    // paint the plot face
    context.draw_rect(plot, albor::Paint{.fill = albor::Color::rgba(m_palette.panel.r, m_palette.panel.g, m_palette.panel.b, m_palette.panel.a)});
    // the grid colour carries the density alpha of minor lines
    const albor::Color grid = albor::Color::rgba(m_palette.grid.r, m_palette.grid.g, m_palette.grid.b, m_palette.grid.a);
    // the abscissa grid lines run from the plot bottom to the plot top
    for (const ChartGridLine& line : frame.x_grid) {
        // a line the density gate hid draws nothing
        if (line.alpha <= 0.0f)
            continue;
        // a minor line carries its density alpha
        albor::Color color = grid;
        if (!line.major)
            color.alpha *= line.alpha;
        // the fraction of the recorded plot the line occupies
        const float ratio = frame.plot_w > 0.0f ? (line.pixel_pos - frame.plot_x) / frame.plot_w : 0.0f;
        // the mapped line position in the shared plot
        const float x = plot.x + ratio * plot.width;
        // the segment fills the plot height
        context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = x, .y = plot.y}, albor::Size{.width = 1.0f, .height = plot.height}), albor::Paint{.fill = color});
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
            // a minor line carries its density alpha
            albor::Color color = grid;
            if (!line.major)
                color.alpha *= line.alpha;
            // the fraction of the recorded height the line occupies
            const float ratio = frame.plot_h > 0.0f ? (line.pixel_pos - frame.plot_y) / frame.plot_h : 0.0f;
            // the mapped line position in the plot
            const float y = plot.y + ratio * plot.height;
            // the segment fills the plot width
            context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = plot.x, .y = y}, albor::Size{.width = plot.width, .height = 1.0f}), albor::Paint{.fill = color});
        }
    }
    // the border and tick marks share the palette role
    const albor::Color border = albor::Color::rgba(m_palette.muted.r, m_palette.muted.g, m_palette.muted.b, m_palette.muted.a);
    // the plot border closes the plot rect
    context.draw_rect(plot, albor::Paint{.fill = albor::Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = border, .stroke_width = 1.0f});
    // the abscissa tick marks extend below the plot bottom
    for (const ChartTick& tick : frame.x_ticks) {
        // a pruned tick has no mark to draw
        if (!tick.show_label)
            continue;
        // a tick outside the recorded plot has no visible mark
        if (tick.pixel_pos < frame.plot_x || tick.pixel_pos > frame.plot_x + frame.plot_w)
            continue;
        // the fraction of the recorded plot the tick occupies
        const float ratio = frame.plot_w > 0.0f ? (tick.pixel_pos - frame.plot_x) / frame.plot_w : 0.0f;
        // the mapped tick position in the shared plot
        const float x = plot.x + ratio * plot.width;
        // choose the major or minor tick length
        const float length = tick.major ? CHART_MAJOR_TICK_LEN : CHART_MINOR_TICK_LEN;
        // the mark extends downward from the datum
        context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = x, .y = plot.y + plot.height}, albor::Size{.width = 1.0f, .height = length}), albor::Paint{.fill = border});
    }
    // every enabled ordinate axis contributes horizontal tick marks
    for (std::size_t axis_index = 0; axis_index < frame.y_axes.size(); ++axis_index) {
        // the axis this tick set belongs to
        const ChartAxisFrame& axis = frame.y_axes[axis_index];
        // a disabled axis has no tick marks
        if (!axis.enabled)
            continue;
        // the datum of the second east axis sits one column outside the plot
        const float datum = axis_index == 2 ? plot.x + plot.width + layout.column_width : plot.x + plot.width;
        // append each visible tick mark of this axis
        for (const ChartTick& tick : axis.ticks) {
            // a pruned tick has no mark to draw
            if (!tick.show_label)
                continue;
            // a tick outside the recorded plot has no visible mark
            if (tick.pixel_pos < frame.plot_y || tick.pixel_pos > frame.plot_y + frame.plot_h)
                continue;
            // the fraction of the recorded height the tick occupies
            const float ratio = frame.plot_h > 0.0f ? (tick.pixel_pos - frame.plot_y) / frame.plot_h : 0.0f;
            // the mapped tick position in the plot
            const float y = plot.y + ratio * plot.height;
            // choose the major or minor tick length
            const float length = tick.major ? CHART_MAJOR_TICK_LEN : CHART_MINOR_TICK_LEN;
            // the west axis grows inward from the left edge, the east axes from their datum
            const float x = axis_index == 0 ? plot.x : datum - length;
            // the mark is centred on the tick position
            context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = x, .y = y - length * 0.5f}, albor::Size{.width = length, .height = 1.0f}), albor::Paint{.fill = border});
        }
    }
    // the second east axis needs its own datum line when it stacks outward
    if (frame.y_axes[2].enabled && !frame.y_axes[2].ticks.empty()) {
        // the datum line shares the axis color
        const float datum = plot.x + plot.width + layout.column_width;
        // the line spans the plot height
        context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = datum, .y = plot.y}, albor::Size{.width = 1.0f, .height = plot.height}), albor::Paint{.fill = border});
    }
    // every label takes the palette text color
    const albor::Paint text{.fill = albor::Color::rgba(m_palette.text.r, m_palette.text.g, m_palette.text.b, m_palette.text.a)};
    // the abscissa labels sit under the mapped tick positions
    for (const ChartTick& tick : frame.x_ticks) {
        // a pruned tick has no label to draw
        if (!tick.show_label)
            continue;
        // a tick outside the recorded plot has no visible label
        if (tick.pixel_pos < frame.plot_x || tick.pixel_pos > frame.plot_x + frame.plot_w)
            continue;
        // measure the label once and reuse the shaped run
        const albor::TextLayout& label = label_layout(tick.label);
        // the fraction of the recorded plot the tick occupies
        const float ratio = frame.plot_w > 0.0f ? (tick.pixel_pos - frame.plot_x) / frame.plot_w : 0.0f;
        // centre the label under the mapped tick and keep it inside the view
        const float label_x = std::clamp(plot.x + ratio * plot.width - label.size.width * 0.5f, 0.0f, std::max(0.0f, layout.view_width - label.size.width));
        // the label starts one padding below the plot bottom
        draw_text(context, label, albor::Point{.x = label_x, .y = plot.y + plot.height + CHART_LABEL_PADDING}, text);
    }
    // every enabled ordinate axis labels its ticks
    for (std::size_t axis_index = 0; axis_index < frame.y_axes.size(); ++axis_index) {
        // the axis these labels belong to
        const ChartAxisFrame& axis = frame.y_axes[axis_index];
        // a disabled axis has no labels
        if (!axis.enabled)
            continue;
        // append each visible label of this axis
        for (const ChartTick& tick : axis.ticks) {
            // a pruned tick has no label to draw
            if (!tick.show_label)
                continue;
            // a tick outside the recorded plot has no visible label
            if (tick.pixel_pos < frame.plot_y || tick.pixel_pos > frame.plot_y + frame.plot_h)
                continue;
            // measure the label once and reuse the shaped run
            const albor::TextLayout& label = label_layout(tick.label);
            // the fraction of the recorded height the tick occupies
            const float ratio = frame.plot_h > 0.0f ? (tick.pixel_pos - frame.plot_y) / frame.plot_h : 0.0f;
            // the mapped tick position in the plot
            const float y = plot.y + ratio * plot.height;
            // the west labels end before the plot, the east labels start after their own column
            const float x = axis_index == 0 ? plot.x - CHART_LABEL_PADDING - label.size.width : plot.x + plot.width + static_cast<float>(axis_index - 1) * layout.column_width + CHART_LABEL_PADDING;
            // centre the label vertically on the tick
            draw_text(context, label, albor::Point{.x = x, .y = y - label.size.height * 0.5f}, text);
        }
    }
}

std::uint64_t studio::ChartView::series_key(const ChartFrame& frame, const BaseLayout& layout) const {
    // the key under construction
    std::uint64_t key = 0;
    // the chart-local plot rect decides the clip and the mapped geometry
    mix_float(key, layout.plot_x);
    mix_float(key, layout.plot_y);
    mix_float(key, layout.plot_width);
    mix_float(key, layout.plot_height);
    // the frame kind and recorded plot decide the mapped samples
    mix(key, frame.smith);
    mix_float(key, frame.plot_x);
    mix_float(key, frame.plot_y);
    mix_float(key, frame.plot_w);
    mix_float(key, frame.plot_h);
    // every series run decides the layer
    for (const ChartSeriesFrame& run : frame.series) {
        mix_text(key, run.name);
        mix_color(key, run.color);
        mix(key, static_cast<std::uint64_t>(run.axis));
        mix(key, run.step);
        mix(key, run.boundary);
        for (const ChartPoint& point : run.points) {
            mix_float(key, point.x);
            mix_float(key, point.y);
        }
    }
    // a theme change invalidates the series layer
    mix_palette(key, m_palette);
    // avoid the empty-cache sentinel
    return key != 0 ? key : 1;
}

void studio::ChartView::paint_series(albor::RenderContext& context, const ChartFrame& frame, const BaseLayout& layout) {
    // a chart with no series records nothing
    if (frame.series.empty())
        return;
    // the chart-local plot rect the series are clipped to
    const albor::Rect plot = albor::Rect::from_origin_size(albor::Point{.x = layout.plot_x, .y = layout.plot_y}, albor::Size{.width = layout.plot_width, .height = layout.plot_height});
    // the series stay clipped to the plot
    context.save();
    // clip the series to the shared plot
    context.clip_rect(plot);
    // one polyline is drawn per contiguous series run
    for (const ChartSeriesFrame& run : frame.series) {
        // a run with fewer than two points has no line
        if (run.points.size() < 2)
            continue;
        // the chart-local polyline under construction
        std::vector<albor::Point> points;
        // reserve the run size so the loop does not grow
        points.reserve(run.points.size());
        // map every recorded point into the shared plot
        for (const ChartPoint& point : run.points) {
            // the fraction of the recorded plot the sample occupies
            const float ratio_x = frame.plot_w > 0.0f ? (point.x - frame.plot_x) / frame.plot_w : 0.0f;
            // the fraction of the recorded height the sample occupies
            const float ratio_y = frame.plot_h > 0.0f ? (point.y - frame.plot_y) / frame.plot_h : 0.0f;
            // the mapped sample position
            points.push_back(albor::Point{.x = layout.plot_x + ratio_x * layout.plot_width, .y = layout.plot_y + ratio_y * layout.plot_height});
        }
        // the stroke carries the series color and weight
        const albor::StrokeStyle stroke{.color = albor::Color::rgba(run.color.r, run.color.g, run.color.b, run.color.a), .width = CHART_LINE_WEIGHT};
        // record the series run
        context.draw_polyline(points, stroke);
    }
    // the series clip ends with the plot
    context.restore();
}

std::shared_ptr<const albor::PictureResource> studio::ChartView::record_series(const ChartFrame& frame, const BaseLayout& layout) {
    // the recorder collects the chart-local series commands
    albor::PictureRecorder recorder;
    // record the clipped series
    paint_series(recorder, frame, layout);
    // count the recorded picture so the cache tests can inspect reuse
    m_series_record_count++;
    // freeze the series picture
    return m_resources.create_picture(recorder);
}

std::uint64_t studio::ChartView::legend_key(const ChartFrame& frame) const {
    // the key under construction
    std::uint64_t key = 0;
    // every legend entry decides the content and colors
    for (const ChartLegendItem& item : frame.legend) {
        mix_text(key, item.name);
        mix_color(key, item.color);
    }
    // the block extent decides the recorded layout
    mix_float(key, frame.legend_w);
    mix_float(key, frame.legend_h);
    // the palette changes the legend text color
    mix_palette(key, m_palette);
    // avoid the empty-cache sentinel
    return key != 0 ? key : 1;
}

void studio::ChartView::paint_legend(albor::RenderContext& context, const ChartFrame& frame) {
    // a chart with no series has no legend block
    if (frame.legend.empty())
        return;
    // the block starts at its inner west padding
    float x = CHART_LEGEND_INNER_PADDING_X;
    // the block starts at its local top edge
    const float top = 0.0f;
    // append each legend entry in series order
    for (const ChartLegendItem& item : frame.legend) {
        // measure the label once and reuse the shaped run
        const albor::TextLayout& label = label_layout(item.name);
        // centre the icon slot in the block height
        const float icon_slot_y = top + (frame.legend_h - CHART_TEXT_HEIGHT) * 0.5f;
        // the swatch is the inset square inside the icon slot
        context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = x + 2.0f, .y = icon_slot_y + 2.0f}, albor::Size{.width = 10.0f, .height = 10.0f}), albor::Paint{.fill = albor::Color::rgba(item.color.r, item.color.g, item.color.b, item.color.a)});
        // centre the label beside the icon slot
        const float text_y = top + (frame.legend_h - label.size.height) * 0.5f;
        // draw the legend label
        draw_text(context, label, albor::Point{.x = x + 14.0f, .y = text_y}, albor::Paint{.fill = albor::Color::rgba(m_palette.text.r, m_palette.text.g, m_palette.text.b, m_palette.text.a)});
        // advance past the icon slot, the label and the entry spacing
        x += 14.0f + label.size.width + CHART_LEGEND_SPACING_X;
    }
}

std::shared_ptr<const albor::PictureResource> studio::ChartView::record_legend(const ChartFrame& frame) {
    // the recorder collects the chart-local legend commands
    albor::PictureRecorder recorder;
    // record the swatches and labels
    paint_legend(recorder, frame);
    // count the recorded picture so the cache tests can inspect reuse
    m_legend_record_count++;
    // freeze the legend picture
    return m_resources.create_picture(recorder);
}

void studio::ChartView::paint_smith_base(albor::RenderContext& context, const ChartFrame& frame, const BaseLayout& layout) {
    // the chart-local gamma square
    const albor::Rect plot = albor::Rect::from_origin_size(albor::Point{.x = layout.plot_x, .y = layout.plot_y}, albor::Size{.width = layout.plot_width, .height = layout.plot_height});
    // the horizontal scale from the frame to the arranged plot
    const float scale_x = frame.plot_w > 0.0f ? plot.width / frame.plot_w : 1.0f;
    // the vertical scale from the frame to the arranged plot
    const float scale_y = frame.plot_h > 0.0f ? plot.height / frame.plot_h : 1.0f;
    // the padded plate keeps the labels on the white face
    const albor::Rect plate = albor::Rect::from_origin_size(albor::Point{.x = plot.x - frame.smith_padding * scale_x, .y = plot.y - frame.smith_padding * scale_y}, albor::Size{.width = plot.width + frame.smith_padding * scale_x * 2.0f, .height = plot.height + frame.smith_padding * scale_y * 2.0f});
    // fill the padded gamma plate
    context.draw_rect(plate, albor::Paint{.fill = albor::Color::rgba(m_palette.panel.r, m_palette.panel.g, m_palette.panel.b, m_palette.panel.a)});
    // the grid boundary and reactive lines
    const albor::Color grid = albor::Color::rgba(m_palette.grid.r, m_palette.grid.g, m_palette.grid.b, m_palette.grid.a);
    // the unit circle and axes stay visible against the white plate
    const albor::Color boundary = albor::Color::rgba(m_palette.muted.r, m_palette.muted.g, m_palette.muted.b, m_palette.muted.a);
    // the gamma grid stays inside the square
    context.save();
    // clip the gamma content to the square
    context.clip_rect(plot);
    // one polyline is drawn per grid run
    for (const ChartSeriesFrame& run : frame.smith_grid) {
        // a run with fewer than two points has no line
        if (run.points.size() < 2)
            continue;
        // the chart-local polyline under construction
        std::vector<albor::Point> points;
        // reserve the run size
        points.reserve(run.points.size());
        // map every recorded gamma point into the plot
        for (const ChartPoint& point : run.points) {
            // the x fraction of the recorded plot
            const float ratio_x = frame.plot_w > 0.0f ? (point.x - frame.plot_x) / frame.plot_w : 0.0f;
            // the y fraction of the recorded plot
            const float ratio_y = frame.plot_h > 0.0f ? (point.y - frame.plot_y) / frame.plot_h : 0.0f;
            // the mapped gamma point
            points.push_back(albor::Point{.x = plot.x + ratio_x * plot.width, .y = plot.y + ratio_y * plot.height});
        }
        // the boundary uses the visible axis color, the reactive grid uses the grid color
        const albor::StrokeStyle stroke{.color = run.boundary ? boundary : grid, .width = CHART_MAJOR_GRID_SIZE};
        // record the gamma run
        context.draw_polyline(points, stroke);
    }
    // the gamma clip ends with the square
    context.restore();
    // every smith label takes the palette text color
    const albor::Paint text{.fill = albor::Color::rgba(m_palette.text.r, m_palette.text.g, m_palette.text.b, m_palette.text.a)};
    // draw every resistance and reactance label outside the clip
    for (const ChartFrame::SmithLabel& label : frame.smith_labels) {
        // measure the label once
        const albor::TextLayout& shaped = label_layout(label.label);
        // map the label anchor into the arranged plot
        const float x = plot.x + (label.x - frame.plot_x) * scale_x - shaped.size.width * 0.5f;
        // map the label vertical anchor
        const float y = plot.y + (label.y - frame.plot_y) * scale_y - shaped.size.height * 0.5f;
        // record the text command
        draw_text(context, shaped, albor::Point{.x = x, .y = y}, text);
    }
}

std::shared_ptr<const albor::PictureResource> studio::ChartView::record_base(const ChartFrame& frame, const BaseLayout& layout) {
    // the recorder collects the chart-local base commands
    albor::PictureRecorder recorder;
    // record the plot plate, grid, border, ticks and labels
    paint_base(recorder, frame, layout);
    // count the recorded picture so the cache tests can inspect reuse
    m_base_record_count++;
    // freeze the base picture
    return m_resources.create_picture(recorder);
}

void studio::ChartView::paint_content(albor::RenderContext& context) {
    // an empty panel draws nothing
    if (bounds().empty())
        return;
    // paint the chart background across the whole panel
    context.draw_rect(bounds(), albor::Paint{.fill = albor::Color::rgba(m_palette.background.r, m_palette.background.g, m_palette.background.b, m_palette.background.a)});
    // a panel with no chart has no base layer
    if (m_frames.empty())
        return;
    // the charts share the content height equally
    const float content_height = bounds().height;
    // the height one chart slot fills
    const float slot_height = content_height / static_cast<float>(m_frames.size());
    // the base, series and legend caches have one entry per chart
    m_base_layers.resize(m_frames.size());
    m_series_layers.resize(m_frames.size());
    m_legend_layers.resize(m_frames.size());
    // one retained base layer is replayed per chart slot
    for (std::size_t index = 0; index < m_frames.size(); ++index) {
        // the chart frame this slot paints
        const ChartFrame& frame = m_frames[index];
        // the slot starts at its share of the content height
        const float slot_y = bounds().y + slot_height * static_cast<float>(index);
        // the vertical scale from the frame canvas to the arranged slot
        const float scale_y = frame.frame_h > 0.0f ? slot_height / frame.frame_h : 1.0f;
        // the horizontal scale from the frame canvas to the arranged panel
        const float scale_x = frame.frame_w > 0.0f ? bounds().width / frame.frame_w : 1.0f;
        // the chart-local geometry the base picture is recorded with
        const BaseLayout layout{.view_width = bounds().width, .slot_height = slot_height, .plot_x = frame.plot_x * scale_x, .plot_y = frame.plot_y * scale_y, .plot_width = frame.plot_w * scale_x, .plot_height = frame.plot_h * scale_y, .column_width = frame.axis_column_width * scale_x};
        // compute the state key of this base layer
        const std::uint64_t key = base_key(frame, layout);
        // the cached layer of this chart
        BaseLayer& layer = m_base_layers[index];
        // record only when the key changed or the picture is missing
        if (layer.key != key || layer.picture == nullptr) {
            // record the chart-local base picture
            layer.picture = record_base(frame, layout);
            // remember the state the picture was recorded for
            layer.key = key;
        }
        // replay the base picture at the chart slot origin
        if (layer.picture != nullptr)
            context.draw_picture(layer.picture, albor::Transform::translation(bounds().x, slot_y));
        // compute the state key of this series layer
        const std::uint64_t series_hash = series_key(frame, layout);
        // the cached series layer of this chart
        SeriesLayer& series_layer = m_series_layers[index];
        // record only when the key changed or the series picture is missing
        if (series_layer.key != series_hash) {
            // an empty series set drops the picture without recording
            series_layer.picture = frame.series.empty() ? nullptr : record_series(frame, layout);
            // remember the state the picture was recorded for
            series_layer.key = series_hash;
        }
        // replay the series picture above the base grid
        if (series_layer.picture != nullptr)
            context.draw_picture(series_layer.picture, albor::Transform::translation(bounds().x, slot_y));
        // compute the state key of this legend layer
        const std::uint64_t legend_hash = legend_key(frame);
        // the cached legend layer of this chart
        LegendLayer& legend_layer = m_legend_layers[index];
        // record only when the key changed
        if (legend_layer.key != legend_hash) {
            // an empty legend set drops the picture without recording
            legend_layer.picture = frame.legend.empty() ? nullptr : record_legend(frame);
            // remember the state the picture was recorded for
            legend_layer.key = legend_hash;
        }
        // replay the legend block at its chart-local anchor
        if (legend_layer.picture != nullptr) {
            // the legend anchor follows the frame geometry
            context.draw_picture(legend_layer.picture, albor::Transform::translation(bounds().x + frame.legend_x * scale_x, slot_y + frame.legend_y * scale_y));
        }
    }
    // the drag handles and the reorder feedback draw above the chart layers
    paint_chart_handles(context);
    // the transient zoom selection draws above the chart content
    if (m_zoom_selection) {
        // the selection in view coordinates under the strip
        const albor::Rect selection = albor::Rect::from_origin_size(albor::Point{.x = bounds().x + m_zoom_selection->x, .y = bounds().y + m_zoom_selection->y}, albor::Size{.width = m_zoom_selection->width, .height = m_zoom_selection->height});
        // the translucent selection wash
        context.draw_rect(selection, albor::Paint{.fill = albor::Color::rgba(0.0f, 0.48f, 1.0f, 0.22f)});
        // the selection border
        context.draw_rect(selection, albor::Paint{.fill = albor::Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = albor::Color::rgba(0.0f, 0.48f, 1.0f, 0.90f), .stroke_width = 1.0f});
    }
}

void studio::ChartView::dispatch_event(albor::Event& event) {
    // a hidden view takes no input
    if (!visible())
        return;
    // an escape aborts a gesture the input layer left to the view
    if (event.kind == albor::EventKind::KeyDown && event.key == albor::Key::Escape && (m_dragging || m_reordering)) {
        // a live gesture is canceled
        cancel_drag();
        // the key is consumed
        event.handled = true;
        // the key is settled
        return;
    }
    // an aborted capture drops the gesture without changing the charts
    if (event.kind == albor::EventKind::PointerCancel) {
        // the input layer already owns the cancel, so it is consumed when a gesture was live
        if (m_dragging || m_reordering) {
            // a live gesture is canceled
            cancel_drag();
            // the view consumed the cancel
            event.handled = true;
        }
        // the cancel is settled
        return;
    }
    // the pointer in view-local coordinates
    const albor::Point local{.x = event.position.x - bounds().x, .y = event.position.y - bounds().y};
    // a press on a drag handle starts a reorder instead of a zoom
    if (event.kind == albor::EventKind::PointerDown && m_frames.size() > 1) {
        // the handle under the press, if any
        for (std::size_t index = 0; index < m_frames.size(); ++index) {
            // the handle rect of this chart
            const albor::Rect handle = chart_drag_handle(index);
            // a miss keeps looking
            if (handle.empty() || local.x < handle.x || local.x > handle.x + handle.width || local.y < handle.y || local.y > handle.y + handle.height)
                continue;
            // the drag owns the pointer until it ends or is canceled
            m_reordering = true;
            // remember the chart the drag grabbed
            m_reorder_from = index;
            // keep the ghost under the grab point
            m_reorder_grab = local.y - handle.y;
            // the pointer starts at the press
            m_reorder_pointer = local;
            // the drop starts in the grabbed chart's gap
            m_reorder_gap = index;
            // the view consumed the press
            event.handled = true;
            // the handles and the feedback repaint
            invalidate();
            // the press is settled
            return;
        }
    }
    // a press inside the chart content starts a zoom drag
    if (event.kind == albor::EventKind::PointerDown) {
        // a press outside the view belongs to whatever is behind it
        if (!hit_test(event.position))
            return;
        // the drag owns the pointer until it ends or is canceled
        m_dragging = true;
        // the view consumed the press
        event.handled = true;
        // report the drag anchor to the presenter
        if (m_on_zoom_drag_started)
            m_on_zoom_drag_started(local.x, local.y);
        // the press is settled
        return;
    }
    // a move either extends a gesture or updates the hover readout
    if (event.kind == albor::EventKind::PointerMove) {
        // a reorder consumes the move
        if (m_reordering) {
            // the ghost follows the pointer
            m_reorder_pointer = local;
            // the insertion line follows the rounded slot boundary
            m_reorder_gap = reorder_gap_at(local.y);
            // the view consumed the move
            event.handled = true;
            // the handles and the feedback repaint
            invalidate();
            // the move is settled
            return;
        }
        // the handle under the pointer brightens while no gesture runs
        std::optional<std::size_t> hovered;
        for (std::size_t index = 0; index < m_frames.size(); ++index) {
            // the handle rect of this chart
            const albor::Rect handle = chart_drag_handle(index);
            // a hover needs a handle the press would find
            if (!handle.empty() && local.x >= handle.x && local.x <= handle.x + handle.width && local.y >= handle.y && local.y <= handle.y + handle.height)
                hovered = index;
        }
        // a changed hover repaints the handles
        if (hovered != m_hovered_handle) {
            // remember the handle the pointer rests on
            m_hovered_handle = hovered;
            // the handles repaint
            invalidate();
        }
        // a drag consumes the move
        if (m_dragging) {
            // the view consumed the move
            event.handled = true;
            // report the moving corner to the presenter
            if (m_on_zoom_drag_moved)
                m_on_zoom_drag_moved(local.x, local.y);
            // the move is settled
            return;
        }
        // report the hover position to the presenter
        if (m_on_hover_moved)
            m_on_hover_moved(local.x, local.y);
        // the move is settled
        return;
    }
    // a release ends a gesture, or refreshes the hover readout when no gesture is active
    if (event.kind == albor::EventKind::PointerUp) {
        // a reorder consumes the release
        if (m_reordering) {
            // the gap the pointer names
            const std::size_t gap = reorder_gap_at(local.y);
            // the chart the drag grabbed
            const std::size_t from = m_reorder_from;
            // the final index the gap names
            const std::size_t to = std::min(gap > from ? gap - 1 : gap, m_frames.size() - 1);
            // the drag no longer owns the pointer
            m_reordering = false;
            // the view consumed the release
            event.handled = true;
            // the handles and the feedback repaint
            invalidate();
            // a landing on another chart reorders the stack
            if (to != from && m_on_chart_reordered)
                m_on_chart_reordered(static_cast<int>(from), static_cast<int>(to));
            // the release is settled
            return;
        }
        // a drag consumes the release
        if (m_dragging) {
            // the drag no longer owns the pointer
            m_dragging = false;
            // the view consumed the release
            event.handled = true;
            // apply the completed zoom window
            if (m_on_zoom_drag_ended)
                m_on_zoom_drag_ended();
            // the release is settled
            return;
        }
        // report the final hover position to the presenter
        if (m_on_hover_moved)
            m_on_hover_moved(local.x, local.y);
        // the release is settled
        return;
    }
    // a leave clears the readout, while a captured gesture keeps running
    if (event.kind == albor::EventKind::PointerLeave) {
        // the capture keeps delivering moves until the release or the cancel
        if (m_dragging || m_reordering)
            return;
        // a handle the pointer left is no longer lit
        if (m_hovered_handle) {
            // the handles repaint
            m_hovered_handle.reset();
            // the handles repaint
            invalidate();
        }
        // clear the readout the pointer left
        if (m_on_hover_ended)
            m_on_hover_ended();
        // the leave is settled
        return;
    }
    // an enter refreshes the hover readout
    if (event.kind == albor::EventKind::PointerEnter) {
        // a pointer on the chart content explains the sample under it
        if (m_on_hover_moved)
            m_on_hover_moved(local.x, local.y);
    }
}

albor::Rect studio::ChartView::chart_drag_handle(const std::size_t index) const {
    // a single chart has nothing to reorder, and a chart past the stack has no handle
    if (m_frames.size() < 2 || index >= m_frames.size() || bounds().width <= 0.0f || bounds().height <= 0.0f)
        return {};
    // the height one chart slot fills
    const float slot_height = bounds().height / static_cast<float>(m_frames.size());
    // the frame this slot paints
    const ChartFrame& frame = m_frames[index];
    // the vertical scale from the frame canvas to the arranged slot
    const float scale_y = frame.frame_h > 0.0f ? slot_height / frame.frame_h : 1.0f;
    // the horizontal scale from the frame canvas to the arranged panel
    const float scale_x = frame.frame_w > 0.0f ? bounds().width / frame.frame_w : 1.0f;
    // the handle sits just inside the plot top right corner, clear of the axis labels
    const float x = (frame.plot_x + frame.plot_w) * scale_x - DRAG_HANDLE_WIDTH - DRAG_HANDLE_INSET;
    // the slot origin plus the plot top and the handle inset
    const float y = slot_height * static_cast<float>(index) + frame.plot_y * scale_y + DRAG_HANDLE_TOP_INSET;
    // the content-relative handle rect
    return albor::Rect::from_origin_size(albor::Point{.x = x, .y = y}, albor::Size{.width = DRAG_HANDLE_WIDTH, .height = DRAG_HANDLE_HEIGHT});
}

std::size_t studio::ChartView::reorder_gap_at(const float y) const {
    // a panel without charts has only the leading gap
    if (m_frames.empty())
        return 0;
    // the content height the slots share
    const float content_height = bounds().height;
    // a degenerate content height names the leading gap
    if (content_height <= 0.0f)
        return 0;
    // the gap under construction, from the rounded slot boundary under the pointer
    const std::size_t gap = static_cast<std::size_t>(std::lround(std::clamp(y / content_height, 0.0f, 1.0f) * static_cast<float>(m_frames.size())));
    // the trailing gap closes the range
    return std::min(gap, m_frames.size());
}

void studio::ChartView::paint_chart_handles(albor::RenderContext& context) {
    // a single chart has nothing to reorder
    if (m_frames.size() < 2 || bounds().empty())
        return;
    // one handle plate and three grip bars share the muted role
    const albor::Color muted = albor::Color::rgba(m_palette.muted.r, m_palette.muted.g, m_palette.muted.b, m_palette.muted.a);
    // draw one handle at a content-relative rect and opacity
    const auto draw_handle = [&](const albor::Rect& handle, const float alpha) {
        // the handle in view coordinates
        const albor::Rect plate = albor::Rect::from_origin_size(albor::Point{.x = handle.x + bounds().x, .y = handle.y + bounds().y}, albor::Size{.width = handle.width, .height = handle.height});
        // the plate behind the bars
        context.draw_rect(plate, albor::Paint{.fill = albor::Color::rgba(muted.red, muted.green, muted.blue, muted.alpha * 0.18f * alpha)});
        // three bars centred in the plate
        const float bars_height = 3.0f * DRAG_HANDLE_BAR_HEIGHT + 2.0f * DRAG_HANDLE_BAR_GAP;
        const float bar_x = plate.x + (plate.width - DRAG_HANDLE_BAR_WIDTH) * 0.5f;
        const float bars_y = plate.y + (plate.height - bars_height) * 0.5f;
        // every bar is one rect
        for (int bar = 0; bar < 3; ++bar)
            context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = bar_x, .y = bars_y + static_cast<float>(bar) * (DRAG_HANDLE_BAR_HEIGHT + DRAG_HANDLE_BAR_GAP)}, albor::Size{.width = DRAG_HANDLE_BAR_WIDTH, .height = DRAG_HANDLE_BAR_HEIGHT}), albor::Paint{.fill = albor::Color::rgba(muted.red, muted.green, muted.blue, muted.alpha * alpha)});
    };
    // one handle is painted per chart, except the one the ghost replaces
    for (std::size_t index = 0; index < m_frames.size(); ++index) {
        // the dragged handle follows the pointer instead
        if (m_reordering && index == m_reorder_from)
            continue;
        // the handle rect of this chart
        const albor::Rect handle = chart_drag_handle(index);
        // a handle that does not fit draws nothing
        if (handle.empty())
            continue;
        // a hovered or dragged handle is lit
        const bool lit = m_reordering || (m_hovered_handle && *m_hovered_handle == index);
        // paint the handle
        draw_handle(handle, lit ? DRAG_HANDLE_LIT_ALPHA : DRAG_HANDLE_REST_ALPHA);
    }
    // the insertion line and the ghost draw only while a reorder runs
    if (!m_reordering)
        return;
    // the height one chart slot fills
    const float slot_height = bounds().height / static_cast<float>(m_frames.size());
    // the insertion line marks the gap the drop lands in
    const float line_y = bounds().y + slot_height * static_cast<float>(m_reorder_gap);
    // the line spans the panel
    context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = bounds().x, .y = line_y - 1.0f}, albor::Size{.width = bounds().width, .height = 2.0f}), albor::Paint{.fill = albor::Color::rgba(0.0f, 0.48f, 1.0f, 0.9f)});
    // the grabbed handle names the column the ghost stays in
    const albor::Rect source = chart_drag_handle(m_reorder_from);
    // the ghost keeps the grab offset under the pointer
    const albor::Rect ghost = albor::Rect::from_origin_size(albor::Point{.x = source.empty() ? bounds().width - DRAG_HANDLE_WIDTH - DRAG_HANDLE_INSET : source.x, .y = m_reorder_pointer.y - m_reorder_grab}, albor::Size{.width = DRAG_HANDLE_WIDTH, .height = DRAG_HANDLE_HEIGHT});
    // the ghost draws at full opacity
    draw_handle(ghost, DRAG_HANDLE_LIT_ALPHA);
}

void studio::ChartView::cancel_reorder() {
    // a view without a live reorder has nothing to cancel
    if (!m_reordering)
        return;
    // the gesture no longer owns the pointer
    m_reordering = false;
    // the ghost and the insertion line leave
    invalidate();
}

std::vector<studio::ChartContextMenuEntry> studio::ChartView::context_menu_entries(const std::size_t chart_index) const {
    // a chart index past the frames has no menu
    if (chart_index >= m_frames.size())
        return {};
    // the smith plane is fixed at the unit circle, so its chart only offers the plot command
    const bool cartesian = !m_frames[chart_index].smith;
    // the menu under construction
    std::vector<ChartContextMenuEntry> entries;
    // the zoom commands apply to a cartesian chart only
    if (cartesian) {
        // fit both axes of the targeted chart
        entries.push_back(ChartContextMenuEntry{.label = "Zoom to Fit", .command = ChartCommand::ZoomToFit});
        // reset only the ordinate of the targeted chart
        entries.push_back(ChartContextMenuEntry{.label = "Autorange", .command = ChartCommand::Autorange});
        // reset the abscissa extent of every chart
        entries.push_back(ChartContextMenuEntry{.label = "Zoom Abscissa Extent", .command = ChartCommand::ZoomAbscissaExtent});
        // separate the zoom group from the plot commands
        entries.push_back(ChartContextMenuEntry{.label = {}, .command = ChartCommand::ZoomToFit, .separator = true});
    }
    // the plot dialog applies to both chart kinds
    entries.push_back(ChartContextMenuEntry{.label = "Add/Remove Plots...", .command = ChartCommand::AddRemovePlots});
    // the plot command applies to both chart kinds
    entries.push_back(ChartContextMenuEntry{.label = "Delete All Plots", .command = ChartCommand::DeleteAllPlots});
    // the step tool only applies to a cartesian chart of a stepped dataset
    if (cartesian && m_step_tool_available)
        entries.push_back(ChartContextMenuEntry{.label = "Step Tool...", .command = ChartCommand::StepTool});
    // the chart stack commands apply to a cartesian chart only
    if (cartesian) {
        // separate the plot command from the chart stack commands
        entries.push_back(ChartContextMenuEntry{.label = {}, .command = ChartCommand::ZoomToFit, .separator = true});
        // insert an empty chart after the targeted one
        entries.push_back(ChartContextMenuEntry{.label = "Add Chart", .command = ChartCommand::AddChart});
        // only a chart with something above it can move up
        if (chart_index > 0)
            entries.push_back(ChartContextMenuEntry{.label = "Move Chart Up", .command = ChartCommand::MoveChartUp});
        // only a chart with something below it can move down
        if (chart_index + 1 < m_frames.size())
            entries.push_back(ChartContextMenuEntry{.label = "Move Chart Down", .command = ChartCommand::MoveChartDown});
        // remove the targeted chart
        entries.push_back(ChartContextMenuEntry{.label = "Delete Chart", .command = ChartCommand::DeleteChart});
    }
    // hand the rows to the popup builder
    return entries;
}

std::unique_ptr<albor::Menu> studio::ChartView::build_context_menu(albor::BindingEngine& engine, albor::TextEngine& text, const std::size_t chart_index) {
    // the menu is the whole popup surface
    auto menu = std::make_unique<albor::Menu>(&engine, &text);
    // every row routes its command to the presenter through this view
    for (const ChartContextMenuEntry& entry : context_menu_entries(chart_index)) {
        // a separator groups the rows around it
        if (entry.separator) {
            // the rule joins the menu between the rows it separates
            menu->add_separator();
            // next row
            continue;
        }
        // one row presents the label and runs the command
        auto item = std::make_unique<albor::MenuItem>(&engine, &text, entry.label);
        // the press dismisses the menu before the chart it names changes
        item->set_on_invoke([this, command = entry.command, chart_index] {
            // hide the surface the press landed on
            close_context_menu();
            // forward the chosen command to the presenter
            run_chart_command(command, chart_index);
        });
        // the row joins the menu, which lays it out and routes its input
        menu->add_child(std::move(item));
    }
    // the popup reads the panel palette so its rows match the shell
    if (const albor::Theme* installed = theme()) {
        // the menu face takes the panel role the shell paints behind it
        menu->set_background(installed->palette().panel_background[0]);
        // every row takes the theme colors and metrics
        albor::apply_theme(*menu, *installed);
    }
    // hand the built menu to the popup stage
    return menu;
}

void studio::ChartView::open_context_menu(const albor::Point local_point) {
    // a panel with no engine, chart or text engine has no menu to offer
    if (engine() == nullptr || m_frames.empty() || m_menu_text == nullptr)
        return;
    // the content the chart slots share
    const float content_height = bounds().height;
    // the chart the press lands on, by the vertical slice it fills
    const std::size_t index = content_height <= 0.0f ? 0 : std::min(static_cast<std::size_t>(local_point.y / content_height * static_cast<float>(m_frames.size())), m_frames.size() - 1);
    // the measuring pass measures the rows the popup will build
    auto measuring = build_context_menu(*engine(), *m_menu_text, index);
    // the popup takes the extent the rows measured
    albor::Size size{};
    measuring->measure(albor::SizeConstraints::unconstrained(), size);
    // a popup is opened by the process wide host the application installs
    albor::PopupHost* host = albor::window_popup_host();
    // no installed host leaves the menu without a surface
    if (host == nullptr)
        return;
    // a surface from the previous open is released before the replacement takes its place
    destroy_context_menu();
    // the menu opens at the press, with the panel position the anchor expects
    const albor::Rect anchor = albor::Rect::from_origin_size(albor::Point{.x = bounds().x + local_point.x, .y = bounds().y + local_point.y}, albor::Size{});
    // the popup clears with the panel role so its first frame never flashes
    const albor::Color background = theme() != nullptr ? theme()->palette().panel_background[0] : albor::Color::rgba(0.13f, 0.15f, 0.19f, 1.0f);
    // the builder runs over the popup stage engines, which is where the menu tree lives
    m_menu_popup = host->open_popup(
        *this, anchor, size, [this, index](albor::BindingEngine& popup_engine, albor::TextEngine& popup_text) { return build_context_menu(popup_engine, popup_text, index); }, background,
        [this] {
            // a native dismissal releases the surface once the notification returns
            schedule_menu_destroy();
        });
}

void studio::ChartView::close_context_menu() {
    // a view without a surface has nothing to hide
    if (!m_menu_popup.valid())
        return;
    // the host hides the surface before the invoking row returns
    if (albor::PopupHost* host = albor::window_popup_host())
        host->close_popup(m_menu_popup);
    // the tree is released after the row callback that asked for the hide
    schedule_menu_destroy();
}

void studio::ChartView::schedule_menu_destroy() {
    // a view without a surface has nothing to release
    if (!m_menu_popup.valid())
        return;
    // the surface this task may release
    const albor::WindowHandle popup = m_menu_popup;
    // the teardown runs after the current event, so a row callback never destroys itself
    albor::Application::instance().post(
        [this, popup] {
            // a later open replaced the surface, which this task must not release
            if (!(m_menu_popup == popup))
                return;
            // the surface and its tree are done
            destroy_context_menu();
        },
        m_lifetime.token());
}

void studio::ChartView::destroy_context_menu() {
    // a view without a surface has nothing to release
    if (!m_menu_popup.valid())
        return;
    // a live host destroys the surface and the tree it built
    if (albor::PopupHost* host = albor::window_popup_host())
        host->destroy_popup(m_menu_popup);
    // the handle no longer names a live surface
    m_menu_popup = {};
}

void studio::ChartView::arrange(const albor::Rect& bounds) {
    // a width change moves every tab
    const bool width_changed = std::abs(bounds.width - m_last_width) > 0.01f;
    // remember whether the panel actually changed extent
    const bool changed = width_changed || std::abs(bounds.height - m_last_height) > 0.01f;
    // let the base component arrange the bounds
    albor::Component::arrange(bounds);
    // remember the extent the next arrange compares against
    m_last_width = bounds.width;
    // remember the height the next arrange compares against
    m_last_height = bounds.height;
    // a changed extent needs fresh geometry, but only after the resize settles
    if (changed)
        schedule_resize_settle();
}

albor::SemanticNode studio::ChartView::semantic_node() const {
    // the chart panel is a drawing surface the user interacts with
    albor::SemanticNode node{albor::SemanticRole::Canvas};
    // carry the author id so the automation can select the panel
    node.set_explicit_id(identity().explicit_id);
    // prefer the summary that names the chart kinds, falling back to the author id
    node.set_name(!m_summary.empty() ? m_summary : (identity().explicit_id.empty() ? "charts" : identity().explicit_id));
    // place the announcement where the panel was arranged
    node.set_bounds(bounds());
    // register the context menu commands for every recorded chart
    for (std::size_t index = 0; index < m_frames.size(); ++index) {
        // the command prefix keeps every chart's actions apart
        const std::string prefix = "chart-" + std::to_string(index) + "-";
        // the smith plane keeps the plot command only
        if (!m_frames[index].smith) {
            // fit both axes of the chart
            (void)node.add_action(prefix + "zoom-to-fit", [this, index] { run_chart_command(ChartCommand::ZoomToFit, index); });
            // reset only the ordinate of the chart
            (void)node.add_action(prefix + "autorange", [this, index] { run_chart_command(ChartCommand::Autorange, index); });
            // reset the abscissa extent of every chart
            (void)node.add_action(prefix + "zoom-abscissa-extent", [this, index] { run_chart_command(ChartCommand::ZoomAbscissaExtent, index); });
            // insert an empty chart after the chart
            (void)node.add_action(prefix + "add-chart", [this, index] { run_chart_command(ChartCommand::AddChart, index); });
            // a chart with something above it can move up
            if (index > 0)
                (void)node.add_action(prefix + "move-chart-up", [this, index] { run_chart_command(ChartCommand::MoveChartUp, index); });
            // a chart with something below it can move down
            if (index + 1 < m_frames.size())
                (void)node.add_action(prefix + "move-chart-down", [this, index] { run_chart_command(ChartCommand::MoveChartDown, index); });
            // remove the chart, keeping at least one chart
            (void)node.add_action(prefix + "delete-chart", [this, index] { run_chart_command(ChartCommand::DeleteChart, index); });
        }
        // edit the expressions the chart plots
        (void)node.add_action(prefix + "add-remove-plots", [this, index] { run_chart_command(ChartCommand::AddRemovePlots, index); });
        // choose the steps the chart keeps active
        if (!m_frames[index].smith && m_step_tool_available)
            (void)node.add_action(prefix + "step-tool", [this, index] { run_chart_command(ChartCommand::StepTool, index); });
        // drop every plotted series of the chart
        (void)node.add_action(prefix + "delete-all-plots", [this, index] { run_chart_command(ChartCommand::DeleteAllPlots, index); });
    }
#ifdef XYCE_STUDIO_AUTOMATION
    // the automation host has no pointer command, so the canvas exposes the press a click scenario drives
    if (!m_frames.empty() && bounds().width > 0.0f && bounds().height > 0.0f) {
        (void)node.add_action("press-center", [this] {
            // a view without a plot has no center to press
            if (m_frames.empty())
                return;
            // the first chart frame, whose plot the press targets
            const ChartFrame& frame = m_frames.front();
            // the slot height the frames share
            const float slot_height = bounds().height / static_cast<float>(m_frames.size());
            // the scales from the frame canvas to the arranged panel
            const float scale_x = frame.frame_w > 0.0f ? bounds().width / frame.frame_w : 1.0f;
            const float scale_y = frame.frame_h > 0.0f ? slot_height / frame.frame_h : 1.0f;
            // the plot center in view coordinates
            const albor::Point center{.x = bounds().x + (frame.plot_x + frame.plot_w * 0.5f) * scale_x, .y = bounds().y + (frame.plot_y + frame.plot_h * 0.5f) * scale_y};
            // the press the user would make
            albor::Event press{.kind = albor::EventKind::PointerDown, .position = center};
            const_cast<ChartView*>(this)->dispatch_event(press);
            // the release that ends the press
            albor::Event release = press;
            release.kind = albor::EventKind::PointerUp;
            const_cast<ChartView*>(this)->dispatch_event(release);
        });
    }
#endif
    // hand the node to the adapter
    return node;
}

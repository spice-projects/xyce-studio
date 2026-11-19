#include "views/charts/add_plot_widgets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "albor/core/paint.h"
#include "albor/render/render_context.h"
#include "albor/style/theme.h"
#include "albor/text/text_engine.h"

namespace
{
    // the chip height the grid rows share
    constexpr float CHIP_HEIGHT = 32.0f;

    // the chip corner radius the plate draws
    constexpr float CHIP_RADIUS = 8.0f;

    // the chip label size both chip kinds draw
    constexpr float CHIP_FONT_SIZE = 13.0f;

    // the breadcrumb and legend label sizes
    constexpr float BREADCRUMB_FONT_SIZE = 12.0f;

    constexpr float LEGEND_FONT_SIZE = 11.0f;

    // the horizontal padding around the chip content
    constexpr float CHIP_PADDING = 10.0f;

    // the gap between the type dot and the label
    constexpr float DOT_GAP = 7.0f;

    // the dot extent the chip and the legend draw
    constexpr float DOT_SIZE = 8.0f;

    // the color a subcircuit scope dot carries
    constexpr std::uint32_t SUBCIRCUIT_RGB = 0x8e6dd9;

    // the color a sheet scope dot carries
    constexpr std::uint32_t SHEET_RGB = 0x4dd0e1;

    // the color a flat scope dot carries
    constexpr std::uint32_t FLAT_RGB = 0x3a3d4a;

    // build a color from a packed 0xRRGGBB value
    albor::Color packed_color(const std::uint32_t rgb) { return albor::Color::rgba(static_cast<float>((rgb >> 16) & 0xff) / 255.0f, static_cast<float>((rgb >> 8) & 0xff) / 255.0f, static_cast<float>(rgb & 0xff) / 255.0f, 1.0f); }

    // the scope kind color, which the dot and the chevron share
    albor::Color scope_color(const std::string& kind) {
        // a subcircuit scope carries its violet dot
        if (kind == "subcircuit")
            return packed_color(SUBCIRCUIT_RGB);
        // a sheet scope carries its cyan dot
        if (kind == "sheet")
            return packed_color(SHEET_RGB);
        // every other scope reads as the neutral ink
        return packed_color(FLAT_RGB);
    }

    // measure a run that fits the offered width, eliding the tail with an ellipsis
    albor::TextLayout fit_text(const albor::TextEngine& text, const std::string& label, const float max_width, const float font_size) {
        // a run with no room shapes as nothing
        if (max_width <= 0.0f || label.empty())
            return {};
        // the unbounded measure answers whether the whole run fits
        albor::TextLayout full = text.measure(label, albor::TextEngine::UNBOUNDED_WIDTH, font_size);
        // a run that fits keeps every glyph
        if (full.size.width <= max_width)
            return full;
        // the ellipsis the tail carries
        const std::string ellipsis = "\u2026";
        // shrink the prefix until the elided run fits
        for (std::size_t length = label.size(); length > 0; --length) {
            // a continuation byte cannot end a UTF-8 prefix
            while (length < label.size() && (static_cast<unsigned char>(label[length]) & 0xc0) == 0x80)
                --length;
            // a prefix that lost every byte leaves only the ellipsis
            if (length == 0)
                break;
            // the candidate run with the ellipsis appended
            const std::string candidate = label.substr(0, length) + ellipsis;
            // the candidate layout the width check reads
            albor::TextLayout layout = text.measure(candidate, albor::TextEngine::UNBOUNDED_WIDTH, font_size);
            // the first fitting prefix is the elided run
            if (layout.size.width <= max_width)
                return layout;
        }
        // nothing but the ellipsis fits
        return text.measure(ellipsis, albor::TextEngine::UNBOUNDED_WIDTH, font_size);
    }

    // report whether a point lands inside a rectangle
    bool contains(const albor::Rect& rect, const albor::Point& point) {
        // the point must fall inside both half open edges
        return point.x >= rect.x && point.x < rect.x + rect.width && point.y >= rect.y && point.y < rect.y + rect.height;
    }

    // draw one chip plate and return nothing, because the caller draws its content next
    void draw_plate(albor::RenderContext& context, const albor::Rect& bounds, const albor::Color& fill, const albor::Color& border, const float border_width) {
        // the plate carries the chip fill and its state border
        context.draw_rounded_rect(bounds, CHIP_RADIUS, albor::Paint{.fill = fill, .stroke = border, .stroke_width = border_width});
    }

    // draw the type dot at the leading inset
    void draw_dot(albor::RenderContext& context, const albor::Rect& bounds, const albor::Color& color) {
        // the dot centers on the chip row
        const albor::Rect dot = albor::Rect::from_origin_size(albor::Point{.x = bounds.x + CHIP_PADDING, .y = bounds.y + (bounds.height - DOT_SIZE) * 0.5f}, albor::Size{.width = DOT_SIZE, .height = DOT_SIZE});
        // the dot is a filled circle
        context.draw_rounded_rect(dot, DOT_SIZE * 0.5f, albor::Paint{.fill = color});
    }
} // namespace

studio::ExpressionChip::ExpressionChip(albor::BindingEngine* engine, albor::TextEngine* text, std::string label, std::string name, const albor::Color dot_color, const bool selected, std::string explicit_id) :
    albor::Widget(engine, std::move(explicit_id)), m_text(text), m_label(std::move(label)), m_name(std::move(name)), m_dot_color(dot_color), m_selected(selected) {}

void studio::ExpressionChip::set_selected(const bool selected) {
    // an unchanged state repaints nothing
    if (m_selected == selected)
        return;
    // the new state changes the plate and the check
    m_selected = selected;
    // the layout changes because the check takes room
    fit_label();
    // the chip repaints with its new state
    invalidate();
}

bool studio::ExpressionChip::selected() const { return m_selected; }

void studio::ExpressionChip::set_on_clicked(std::function<void()> handler) { m_on_clicked = std::move(handler); }

void studio::ExpressionChip::measure(const albor::SizeConstraints& constraints, albor::Size& out_size) {
    // the chip takes the offered cell width and the fixed row height
    const albor::Size content{.width = constraints.maximum.width, .height = CHIP_HEIGHT};
    // the offered constraints are final
    m_measured_size = constraints.constrain(content);
    // the preferred size follows the measured one
    m_preferred_size = m_measured_size;
    // hand the grid the cell extent
    out_size = m_measured_size;
}

void studio::ExpressionChip::arrange(const albor::Rect& bounds) {
    // the base keeps the assigned cell
    albor::Widget::arrange(bounds);
    // the label fits the cell the grid assigned
    fit_label();
}

void studio::ExpressionChip::fit_label() {
    // a chip without a text engine draws no run
    if (m_text == nullptr) {
        m_layout = {};
        return;
    }
    // the label starts after the dot and ends before the check or the trailing inset
    const float leading = CHIP_PADDING + DOT_SIZE + DOT_GAP;
    // the check reserves its own room when the chip is selected
    const float trailing = CHIP_PADDING + (m_selected ? 18.0f : 0.0f);
    // the run fits what the insets leave
    const float available = std::max(0.0f, bounds().width - leading - trailing);
    // shape the run to the available width
    m_layout = fit_text(*m_text, m_label, available, CHIP_FONT_SIZE);
}

void studio::ExpressionChip::paint_content(albor::RenderContext& context) {
    // an unarranged chip draws nothing
    if (bounds().empty())
        return;
    // the theme the chip resolves through, null while none is installed
    const albor::Theme* installed = theme();
    // the selected chip takes the accent plate and its label
    albor::Color fill = m_selected ? albor::Color::rgba(0.08f, 0.47f, 1.0f, 1.0f) : albor::Color::rgba(0.94f, 0.94f, 0.95f, 1.0f);
    // the border follows the state, with the accent marking a hover
    albor::Color border = m_selected ? fill : (hovered() ? albor::Color::rgba(0.08f, 0.47f, 1.0f, 1.0f) : albor::Color::rgba(0.84f, 0.84f, 0.86f, 1.0f));
    // the label sits over the plate
    albor::Color text = m_selected ? albor::Color::rgba(1.0f, 1.0f, 1.0f, 1.0f) : albor::Color::rgba(0.11f, 0.12f, 0.14f, 1.0f);
    // a themed chip reads the palette roles
    if (installed != nullptr) {
        // the accent plate and its label
        fill = m_selected ? resolve(installed->palette().checkbox_fill, fill) : resolve(installed->palette().panel_background, fill);
        // the border is the accent on hover and the panel edge otherwise
        border = m_selected ? fill : (hovered() ? resolve(installed->palette().checkbox_fill, border) : resolve(installed->palette().panel_border, border));
        // the label follows the plate
        text = m_selected ? resolve(installed->palette().button_label, text) : resolve(installed->palette().label_foreground, text);
    }
    // the dot fades slightly over the accent plate
    const float dot_alpha = m_selected ? 0.9f : 1.0f;
    // the identity dot in the unit color
    const albor::Color dot = albor::Color::rgba(m_dot_color.red, m_dot_color.green, m_dot_color.blue, dot_alpha);
    // record the plate, the dot and the label
    draw_plate(context, bounds(), fill, border, m_selected ? 1.5f : 1.0f);
    // the dot marks the unit
    draw_dot(context, bounds(), dot);
    // a shaped label draws over the plate
    if (!m_layout.empty()) {
        // the label sits after the dot, centred on the row
        const albor::Point origin{.x = bounds().x + CHIP_PADDING + DOT_SIZE + DOT_GAP, .y = bounds().y + (bounds().height - m_layout.size.height) * 0.5f};
        // the run clips to the chip so a long name cannot paint past its plate
        context.save();
        // the clip keeps the elided run inside the chip
        context.clip_rect(bounds());
        // the run takes the state ink
        context.draw_text(m_layout, origin, albor::Paint{.fill = text});
        // the clip ends with the run
        context.restore();
    }
    // a selected chip carries the check mark at its trailing edge
    if (!m_selected)
        return;
    // the check sits near the trailing inset, centred on the row
    const float check_x = bounds().x + bounds().width - CHIP_PADDING - 10.0f;
    // the vertical centre the check pivots around
    const float check_y = bounds().y + bounds().height * 0.5f;
    // the three points of the tick
    const std::array<albor::Point, 3> check{{{.x = check_x, .y = check_y}, {.x = check_x + 3.0f, .y = check_y + 3.0f}, {.x = check_x + 9.0f, .y = check_y - 5.0f}}};
    // the tick strokes with the label ink
    context.draw_polyline(check, albor::StrokeStyle{.color = text, .width = 1.6f});
}

void studio::ExpressionChip::dispatch_event(albor::Event& event) {
    // a hidden chip takes no input
    if (!visible())
        return;
    // the hover transitions repaint the plate
    handle_hover_transitions(event);
    // only a primary press inside the chip activates it
    if (event.kind != albor::EventKind::PointerDown || event.button != albor::PointerButton::Left || !hit_test(event.position))
        return;
    // the chip consumed the press
    event.handled = true;
    // report the press to the dialog
    if (m_on_clicked)
        m_on_clicked();
}

albor::SemanticNode studio::ExpressionChip::semantic_node() const {
    // the chip is a checkable expression row
    albor::SemanticNode node{albor::SemanticRole::Checkbox};
    // the author id lets the automation select the chip
    node.set_explicit_id(identity().explicit_id);
    // the label is the accessible name
    node.set_name(m_label);
    // the bounds place the announcement
    node.set_bounds(bounds());
    // the check state mirrors the selection
    node.set_check(m_selected ? albor::CheckState::Checked : albor::CheckState::Unchecked);
    // a press toggles the selection
    (void)node.add_action("toggle", [control = albor::mutable_owner(this)] {
        // the toggle runs the press path
        control->m_on_clicked();
    });
    // hand the announcement to the adapter
    return node;
}

studio::ScopeChip::ScopeChip(albor::BindingEngine* engine, albor::TextEngine* text, std::string label, std::string kind, const int count, std::string explicit_id) :
    albor::Widget(engine, std::move(explicit_id)), m_text(text), m_label(std::move(label)), m_kind(std::move(kind)), m_count(count) {}

void studio::ScopeChip::set_on_clicked(std::function<void()> handler) { m_on_clicked = std::move(handler); }

void studio::ScopeChip::measure(const albor::SizeConstraints& constraints, albor::Size& out_size) {
    // the scope chip takes the offered cell width and the fixed row height
    const albor::Size content{.width = constraints.maximum.width, .height = CHIP_HEIGHT};
    // the offered constraints are final
    m_measured_size = constraints.constrain(content);
    // the preferred size follows the measured one
    m_preferred_size = m_measured_size;
    // hand the grid the cell extent
    out_size = m_measured_size;
}

void studio::ScopeChip::arrange(const albor::Rect& bounds) {
    // the base keeps the assigned cell
    albor::Widget::arrange(bounds);
    // the label fits the cell the grid assigned
    fit_label();
}

void studio::ScopeChip::fit_label() {
    // a chip without a text engine draws no run
    if (m_text == nullptr) {
        m_layout = {};
        m_count_layout = {};
        return;
    }
    // the count and the chevron take the trailing room
    m_count_layout = m_text->measure(std::to_string(m_count), albor::TextEngine::UNBOUNDED_WIDTH, LEGEND_FONT_SIZE);
    // the label starts after the dot and ends before the count
    const float leading = CHIP_PADDING + DOT_SIZE + DOT_GAP;
    // the trailing room the count, the gaps and the chevron need
    const float trailing = CHIP_PADDING + 14.0f + 6.0f + m_count_layout.size.width + 8.0f;
    // the run fits what the insets leave
    m_layout = fit_text(*m_text, m_label, std::max(0.0f, bounds().width - leading - trailing), CHIP_FONT_SIZE);
}

void studio::ScopeChip::paint_content(albor::RenderContext& context) {
    // an unarranged chip draws nothing
    if (bounds().empty())
        return;
    // the theme the chip resolves through, null while none is installed
    const albor::Theme* installed = theme();
    // the scope dot color
    const albor::Color dot_color = scope_color(m_kind);
    // the plate fill and border read the panel roles
    albor::Color fill = albor::Color::rgba(0.94f, 0.94f, 0.95f, 1.0f);
    // the border turns into the scope ink on hover
    albor::Color border = hovered() ? dot_color : albor::Color::rgba(0.84f, 0.84f, 0.86f, 1.0f);
    // the label ink
    albor::Color text = albor::Color::rgba(0.11f, 0.12f, 0.14f, 1.0f);
    // a themed chip reads the palette roles
    if (installed != nullptr) {
        // the panel plate the scope sits on
        fill = resolve(installed->palette().panel_background, fill);
        // the hover border takes the accent plate
        border = hovered() ? dot_color : resolve(installed->palette().panel_border, border);
        // the label reads the foreground
        text = resolve(installed->palette().label_foreground, text);
    }
    // record the plate, the dot and the label
    draw_plate(context, bounds(), fill, border, 1.0f);
    // the dot marks the scope kind
    draw_dot(context, bounds(), dot_color);
    // a shaped label draws over the plate
    if (!m_layout.empty()) {
        // the label sits after the dot, centred on the row
        const albor::Point origin{.x = bounds().x + CHIP_PADDING + DOT_SIZE + DOT_GAP, .y = bounds().y + (bounds().height - m_layout.size.height) * 0.5f};
        // the run clips to the chip
        context.save();
        // the clip keeps the elided run inside the chip
        context.clip_rect(bounds());
        // the run takes the label ink
        context.draw_text(m_layout, origin, albor::Paint{.fill = text});
        // the clip ends with the run
        context.restore();
    }
    // the count sits before the chevron
    const float chevron_x = bounds().x + bounds().width - CHIP_PADDING - 6.0f;
    // the count's trailing edge precedes the chevron gap
    const float count_x = chevron_x - 8.0f - m_count_layout.size.width;
    // the count draws at the row centre in a dimmer ink
    if (!m_count_layout.empty()) {
        // the count origin
        const albor::Point origin{.x = count_x, .y = bounds().y + (bounds().height - m_count_layout.size.height) * 0.5f};
        // the count takes a faded label ink
        context.draw_text(m_count_layout, origin, albor::Paint{.fill = albor::Color::rgba(text.red, text.green, text.blue, 0.6f)});
    }
    // the chevron marks the drill-in affordance
    const float centre_y = bounds().y + bounds().height * 0.5f;
    // the two strokes of the chevron
    const std::array<albor::Point, 3> chevron{{{.x = chevron_x, .y = centre_y - 4.0f}, {.x = chevron_x + 4.0f, .y = centre_y}, {.x = chevron_x, .y = centre_y + 4.0f}}};
    // the chevron strokes with the scope ink
    context.draw_polyline(chevron, albor::StrokeStyle{.color = dot_color, .width = 1.5f});
}

void studio::ScopeChip::dispatch_event(albor::Event& event) {
    // a hidden chip takes no input
    if (!visible())
        return;
    // the hover transitions repaint the plate
    handle_hover_transitions(event);
    // only a primary press inside the chip drills in
    if (event.kind != albor::EventKind::PointerDown || event.button != albor::PointerButton::Left || !hit_test(event.position))
        return;
    // the chip consumed the press
    event.handled = true;
    // report the press to the dialog
    if (m_on_clicked)
        m_on_clicked();
}

albor::SemanticNode studio::ScopeChip::semantic_node() const {
    // the scope chip is a drill-in button
    albor::SemanticNode node{albor::SemanticRole::Button};
    // the author id lets the automation select the chip
    node.set_explicit_id(identity().explicit_id);
    // the label is the accessible name
    node.set_name(m_label);
    // the bounds place the announcement
    node.set_bounds(bounds());
    // a press drills into the scope
    (void)node.add_action("activate", [control = albor::mutable_owner(this)] {
        // the activation runs the press path
        control->m_on_clicked();
    });
    // hand the announcement to the adapter
    return node;
}

studio::PlotBreadcrumb::PlotBreadcrumb(albor::BindingEngine* engine, albor::TextEngine* text) :
    albor::Widget(engine, "add_plot_breadcrumb"), m_text(text) {}

void studio::PlotBreadcrumb::set_entries(std::vector<BreadcrumbEntry> entries, const bool show_selected) {
    // the segments the bar draws, in path order
    m_segments.clear();
    // every entry becomes a segment with its own label and level
    for (const BreadcrumbEntry& entry : entries)
        m_segments.push_back(Segment{.label = entry.label, .level = entry.level, .bounds = {}, .layout = {}});
    // the selected-only state mirrors the tree
    m_show_selected = show_selected;
    // the new segments need a layout pass
    layout_segments();
    // the bar repaints with the new path
    invalidate();
}

void studio::PlotBreadcrumb::set_on_segment_clicked(std::function<void(std::size_t)> handler) { m_on_segment_clicked = std::move(handler); }

void studio::PlotBreadcrumb::set_on_selected_clicked(std::function<void()> handler) { m_on_selected_clicked = std::move(handler); }

void studio::PlotBreadcrumb::measure(const albor::SizeConstraints& constraints, albor::Size& out_size) {
    // the bar takes the offered width and a fixed height
    const albor::Size content{.width = constraints.maximum.width, .height = 18.0f};
    // the offered constraints are final
    m_measured_size = constraints.constrain(content);
    // the preferred size follows the measured one
    m_preferred_size = m_measured_size;
    // hand the dialog the bar extent
    out_size = m_measured_size;
}

void studio::PlotBreadcrumb::arrange(const albor::Rect& bounds) {
    // the base keeps the assigned bounds
    albor::Widget::arrange(bounds);
    // the segments are laid out once the extent is known
    layout_segments();
}

void studio::PlotBreadcrumb::layout_segments() {
    // a bar without a text engine lays nothing out
    if (m_text == nullptr)
        return;
    // a bar without bounds has no line to lay out
    if (bounds().empty()) {
        for (Segment& segment : m_segments)
            segment.bounds = {};
        m_selected_bounds = {};
        return;
    }
    // the cursor walks the segments from the leading edge
    float cursor = bounds().x;
    // the separator run between two segments
    const albor::TextLayout separator = m_text->measure("\u203A", albor::TextEngine::UNBOUNDED_WIDTH, BREADCRUMB_FONT_SIZE);
    // lay every segment out in path order
    for (std::size_t index = 0; index < m_segments.size(); ++index) {
        // the separator precedes every segment but the first
        if (index > 0) {
            // the cursor moves past the separator run plus its gaps
            cursor += 6.0f + separator.size.width + 6.0f;
        }
        // shape the segment label
        Segment& segment = m_segments[index];
        segment.layout = m_text->measure(segment.label, albor::TextEngine::UNBOUNDED_WIDTH, BREADCRUMB_FONT_SIZE);
        // the segment takes its label extent
        segment.bounds = albor::Rect::from_origin_size(albor::Point{.x = cursor, .y = bounds().y}, albor::Size{.width = segment.layout.size.width, .height = bounds().height});
        // the cursor advances past the segment
        cursor += segment.layout.size.width;
    }
    // the selected-only link hugs the trailing edge
    m_selected_layout = m_text->measure("(selected)", albor::TextEngine::UNBOUNDED_WIDTH, BREADCRUMB_FONT_SIZE);
    // the link takes the trailing inset
    m_selected_bounds = albor::Rect::from_origin_size(albor::Point{.x = bounds().x + bounds().width - m_selected_layout.size.width, .y = bounds().y}, albor::Size{.width = m_selected_layout.size.width, .height = bounds().height});
}

void studio::PlotBreadcrumb::paint_content(albor::RenderContext& context) {
    // an unarranged bar draws nothing
    if (bounds().empty() || m_text == nullptr)
        return;
    // the theme the bar resolves through, null while none is installed
    const albor::Theme* installed = theme();
    // the accent ink an earlier segment uses
    albor::Color accent = albor::Color::rgba(0.08f, 0.47f, 1.0f, 1.0f);
    // the ink the current segment and the dim separator use
    albor::Color text = albor::Color::rgba(0.11f, 0.12f, 0.14f, 1.0f);
    // a themed bar reads the palette roles
    if (installed != nullptr) {
        // the accent ink the links carry
        accent = resolve(installed->palette().checkbox_fill, accent);
        // the readable ink
        text = resolve(installed->palette().label_foreground, text);
    }
    // the separator run the segments share
    const albor::TextLayout separator = m_text->measure("\u203A", albor::TextEngine::UNBOUNDED_WIDTH, BREADCRUMB_FONT_SIZE);
    // draw every segment with the separator before it
    for (std::size_t index = 0; index < m_segments.size(); ++index) {
        // the segment to draw
        const Segment& segment = m_segments[index];
        // the last segment is the current scope, the earlier ones are links
        const albor::Color ink = index + 1 == m_segments.size() ? text : accent;
        // the separator precedes every segment but the first
        if (index > 0) {
            // the separator origin sits between the two segments
            const albor::Point origin{.x = segment.bounds.x - 6.0f - separator.size.width, .y = bounds().y + (bounds().height - separator.size.height) * 0.5f};
            // the separator takes a dim ink
            context.draw_text(separator, origin, albor::Paint{.fill = albor::Color::rgba(text.red, text.green, text.blue, 0.5f)});
        }
        // the segment label is centred on the bar
        const albor::Point origin{.x = segment.bounds.x, .y = bounds().y + (bounds().height - segment.layout.size.height) * 0.5f};
        // the segment draws with its link ink
        context.draw_text(segment.layout, origin, albor::Paint{.fill = ink});
    }
    // the selected-only link sits at the trailing edge
    const albor::Point selected_origin{.x = m_selected_bounds.x, .y = bounds().y + (bounds().height - m_selected_layout.size.height) * 0.5f};
    // the selected-only link always reads in the accent ink
    context.draw_text(m_selected_layout, selected_origin, albor::Paint{.fill = accent});
}

void studio::PlotBreadcrumb::dispatch_event(albor::Event& event) {
    // a hidden bar takes no input
    if (!visible())
        return;
    // the hover transitions repaint nothing, because the bar has no plate
    handle_hover_transitions(event);
    // only a primary press activates a link
    if (event.kind != albor::EventKind::PointerDown || event.button != albor::PointerButton::Left)
        return;
    // a press on the selected-only link toggles the view
    if (contains(m_selected_bounds, event.position)) {
        // the bar consumed the press
        event.handled = true;
        // report the toggle to the dialog
        if (m_on_selected_clicked)
            m_on_selected_clicked();
        // the press is settled
        return;
    }
    // a press on an earlier segment restores its scope
    for (std::size_t index = 0; index + 1 < m_segments.size(); ++index) {
        // only the segment under the press reacts
        if (!contains(m_segments[index].bounds, event.position))
            continue;
        // the bar consumed the press
        event.handled = true;
        // report the level to the dialog
        if (m_on_segment_clicked)
            m_on_segment_clicked(m_segments[index].level);
        // the press is settled
        return;
    }
}

albor::SemanticNode studio::PlotBreadcrumb::semantic_node() const {
    // the bar announces itself as a group
    albor::SemanticNode node{albor::SemanticRole::Group};
    // the author id lets the automation select the bar
    node.set_explicit_id(identity().explicit_id);
    // the name names the path
    node.set_name("Breadcrumb");
    // the bounds place the announcement
    node.set_bounds(bounds());
    // every earlier segment exposes its restore action
    for (std::size_t index = 0; index + 1 < m_segments.size(); ++index) {
        // the level the action restores
        const std::size_t level = m_segments[index].level;
        // the action restores the scope
        (void)node.add_action("segment-" + std::to_string(level), [control = albor::mutable_owner(this), level] {
            // report the level to the dialog
            if (control->m_on_segment_clicked)
                control->m_on_segment_clicked(level);
        });
    }
    // the selected-only link exposes its toggle
    (void)node.add_action("selected-only", [control = albor::mutable_owner(this)] {
        // report the toggle to the dialog
        if (control->m_on_selected_clicked)
            control->m_on_selected_clicked();
    });
    // hand the announcement to the adapter
    return node;
}

studio::PlotLegend::PlotLegend(albor::BindingEngine* engine, albor::TextEngine* text, std::string explicit_id) :
    albor::Widget(engine, std::move(explicit_id)), m_text(text) {}

void studio::PlotLegend::set_entries(std::vector<expression_colors::LegendEntry> entries) {
    // the rows the legend draws
    m_entries = std::move(entries);
    // the rows are shaped once the text engine is known
    m_layouts.clear();
    // shape every label at the legend size
    if (m_text != nullptr) {
        for (const expression_colors::LegendEntry& entry : m_entries)
            m_layouts.push_back(m_text->measure(entry.label, albor::TextEngine::UNBOUNDED_WIDTH, LEGEND_FONT_SIZE));
    }
    // the legend repaints with the new rows
    invalidate();
}

void studio::PlotLegend::measure(const albor::SizeConstraints& constraints, albor::Size& out_size) {
    // the legend takes the offered width and a fixed height
    const albor::Size content{.width = constraints.maximum.width, .height = 16.0f};
    // the offered constraints are final
    m_measured_size = constraints.constrain(content);
    // the preferred size follows the measured one
    m_preferred_size = m_measured_size;
    // hand the dialog the legend extent
    out_size = m_measured_size;
}

void studio::PlotLegend::paint_content(albor::RenderContext& context) {
    // an unarranged legend draws nothing
    if (bounds().empty())
        return;
    // the theme the legend resolves through, null while none is installed
    const albor::Theme* installed = theme();
    // the label ink
    albor::Color text = albor::Color::rgba(0.11f, 0.12f, 0.14f, 0.65f);
    // a themed legend fades the label ink
    if (installed != nullptr) {
        // the label foreground the legend fades
        const albor::Color foreground = resolve(installed->palette().label_foreground, text);
        // the legend rows sit dimmer than a label
        text = albor::Color::rgba(foreground.red, foreground.green, foreground.blue, 0.65f);
    }
    // the cursor walks the rows from the leading edge
    float cursor = bounds().x;
    // draw every row
    for (std::size_t index = 0; index < m_entries.size() && index < m_layouts.size(); ++index) {
        // the dot sits at the row centre
        const albor::Rect dot = albor::Rect::from_origin_size(albor::Point{.x = cursor, .y = bounds().y + (bounds().height - DOT_SIZE) * 0.5f}, albor::Size{.width = DOT_SIZE, .height = DOT_SIZE});
        // the dot is a filled circle in the unit color
        context.draw_rounded_rect(dot, DOT_SIZE * 0.5f, albor::Paint{.fill = m_entries[index].color});
        // the label follows the dot
        const albor::Point origin{.x = cursor + DOT_SIZE + 4.0f, .y = bounds().y + (bounds().height - m_layouts[index].size.height) * 0.5f};
        // the label takes the faded ink
        context.draw_text(m_layouts[index], origin, albor::Paint{.fill = text});
        // the next row starts after the label
        cursor += DOT_SIZE + 4.0f + m_layouts[index].size.width + 16.0f;
    }
}

albor::SemanticNode studio::PlotLegend::semantic_node() const {
    // the legend announces itself as one group
    albor::SemanticNode node{albor::SemanticRole::Group};
    // the author id lets the automation select the legend
    node.set_explicit_id(identity().explicit_id);
    // the name names the legend and the categories it lists
    std::string name = "Legend";
    // every listed category joins the announcement
    for (const expression_colors::LegendEntry& entry : m_entries)
        name += ", " + entry.label;
    // keep the announcement the adapter reads
    node.set_name(std::move(name));
    // the bounds place the announcement
    node.set_bounds(bounds());
    // hand the announcement to the adapter
    return node;
}

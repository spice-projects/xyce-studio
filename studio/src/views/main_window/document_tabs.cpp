#include "views/main_window/document_tabs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>

#include "albor/core/paint.h"
#include "albor/render/glyph_outline.h"
#include "albor/text/text_engine.h"
#include "charts/chart_style.h"
#include "charts/chart_ui_style.h"

namespace
{
    // the strip inset at both edges
    constexpr float STRIP_INSET = 8.0f;

    // the gap between neighbouring tabs
    constexpr float STRIP_TAB_GAP = 4.0f;

    // the narrowest tab the strip draws
    constexpr float STRIP_MIN_TAB_WIDTH = 24.0f;

    // the horizontal padding inside one tab
    constexpr float STRIP_TAB_PADDING = 12.0f;

    // the room the close affordance keeps inside its tab
    constexpr float STRIP_CLOSE_SLOT = 16.0f;

    // the close cross box
    constexpr float STRIP_CLOSE_SIZE = 12.0f;

    // the narrowest tab that still shows the close affordance
    constexpr float STRIP_CLOSE_MIN_WIDTH = 32.0f;
} // namespace

studio::DocumentTabs::DocumentTabs(albor::BindingEngine* engine, albor::TextEngine* text) :
    albor::Widget(engine, "document_tabs"), m_outline_resolver(text != nullptr ? text->glyph_outline_resolver() : albor::GlyphOutlineResolver{}), m_text(text) {}

void studio::DocumentTabs::set_documents(std::vector<DocumentTabSpec> documents) {
    // keep the documents the strip lists
    m_documents = std::move(documents);
    // a shrunk set cannot keep a selection past its end
    if (m_documents.empty())
        m_active = 0;
    else
        m_active = std::min(m_active, m_documents.size() - 1);
    // the tab widths moved, so the hit rectangles follow at once
    refresh_bounds();
    // the strip changed, so it redraws
    invalidate();
}

void studio::DocumentTabs::set_active(const int index) {
    // a negative index selects the first document
    const std::size_t active = index < 0 ? 0 : static_cast<std::size_t>(index);
    // clamp the selection into the list
    const std::size_t clamped = m_documents.empty() ? 0 : std::min(active, m_documents.size() - 1);
    // an unchanged selection redraws nothing
    if (m_active == clamped)
        return;
    // remember the selected document
    m_active = clamped;
    // the active plate and underline moved
    invalidate();
}

void studio::DocumentTabs::set_palette(const ChartPalette& palette) {
    // keep the palette the next frame paints with
    m_palette = palette;
    // the whole strip changed
    invalidate();
}

void studio::DocumentTabs::set_on_selected(std::function<void(int)> handler) { m_on_selected = std::move(handler); }

void studio::DocumentTabs::set_on_closed(std::function<void(int)> handler) { m_on_closed = std::move(handler); }

void studio::DocumentTabs::measure(const albor::SizeConstraints& constraints, albor::Size& out_size) {
    // the strip has a fixed height and fills the offered width
    albor::Size content{.width = 640.0f, .height = CHART_TAB_HEIGHT};
    // a bounded width is what the strip fills
    if (std::isfinite(constraints.maximum.width))
        content.width = constraints.maximum.width;
    // clamp the fill into the offered constraints
    m_measured_size = constraints.constrain(content);
    // the preferred size follows the measured one
    m_preferred_size = m_measured_size;
    // hand the parent the extent
    out_size = m_measured_size;
}

void studio::DocumentTabs::arrange(const albor::Rect& bounds) {
    // let the base component arrange the bounds
    albor::Component::arrange(bounds);
    // the width decides where every tab sits
    refresh_bounds();
}

const albor::TextLayout& studio::DocumentTabs::label_layout(const std::string& text) {
    // an empty cache entry stands in for a strip without a text engine
    static const albor::TextLayout empty;
    // a strip without a text engine shapes nothing
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

void studio::DocumentTabs::draw_text(albor::RenderContext& context, const albor::TextLayout& layout, const albor::Point origin, const albor::Paint& paint) {
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

void studio::DocumentTabs::refresh_bounds() {
    // the previous rectangles no longer describe the current documents
    m_tab_bounds.clear();
    m_close_bounds.clear();
    // a strip without documents or width carries no rectangles
    if (m_documents.empty() || bounds().width <= 0.0f)
        return;
    // the natural width of every tab
    std::vector<float> widths;
    // reserve one width per document
    widths.reserve(m_documents.size());
    // the sum of the natural widths
    float total_width = 0.0f;
    // measure every title once through the shared cache
    for (const DocumentTabSpec& document : m_documents) {
        // the label width
        const float label_width = label_layout(document.title).size.width;
        // the natural tab width keeps the label and the close slot
        const float natural = STRIP_TAB_PADDING + label_width + STRIP_TAB_PADDING + (document.closable ? STRIP_CLOSE_SLOT : 0.0f);
        // keep the natural width
        widths.push_back(natural);
        // accumulate the total
        total_width += natural;
    }
    // the room the inset and the gaps leave for the tabs
    const float available = std::max(0.0f, bounds().width - 2.0f * STRIP_INSET - STRIP_TAB_GAP * static_cast<float>(m_documents.size() - 1));
    // shrink the tabs proportionally when they do not fit
    if (total_width > available && total_width > 0.0f) {
        // the scale every natural width takes
        const float scale = available / total_width;
        // shrink every width while keeping a usable minimum
        for (float& width : widths)
            width = std::max(STRIP_MIN_TAB_WIDTH, width * scale);
        // a strip too narrow for the minimum widths still keeps every tab reachable
        float floored_total = 0.0f;
        for (const float width : widths)
            floored_total += width;
        if (floored_total > available && floored_total > 0.0f) {
            // the second scale drops the minimum, so no document loses its rectangle
            const float exact = available / floored_total;
            for (float& width : widths)
                width = std::max(0.0f, width * exact);
        }
    }
    // the first tab starts at the strip inset
    float x = STRIP_INSET;
    // one rectangle pair is recorded per visible tab
    for (std::size_t index = 0; index < m_documents.size(); ++index) {
        // stop when the strip has no room left
        if (x >= bounds().width - STRIP_INSET)
            break;
        // the tab width fits the remaining room
        const float tab_width = std::min(widths[index], std::max(0.0f, bounds().width - STRIP_INSET - x));
        // the tab rectangle
        const albor::Rect tab = albor::Rect::from_origin_size(albor::Point{.x = x, .y = 0.0f}, albor::Size{.width = tab_width, .height = CHART_TAB_HEIGHT});
        // the close affordance sits at the tab east edge
        const albor::Rect close = albor::Rect::from_origin_size(albor::Point{.x = x + tab_width - STRIP_CLOSE_SLOT, .y = (CHART_TAB_HEIGHT - STRIP_CLOSE_SIZE) * 0.5f}, albor::Size{.width = STRIP_CLOSE_SIZE, .height = STRIP_CLOSE_SIZE});
        // remember the tab for hit tests and automation
        m_tab_bounds.push_back(tab);
        // remember the close affordance when the tab draws it
        m_close_bounds.push_back(m_documents[index].closable && tab_width >= STRIP_CLOSE_MIN_WIDTH ? close : albor::Rect{});
        // advance past the tab and the gap
        x += tab_width + STRIP_TAB_GAP;
    }
}

albor::Rect studio::DocumentTabs::tab_bounds(const std::size_t index) const {
    // a tab past the laid out rows has no rectangle
    return index < m_tab_bounds.size() ? m_tab_bounds[index] : albor::Rect{};
}

albor::Rect studio::DocumentTabs::close_bounds(const std::size_t index) const {
    // a tab past the laid out rows has no close rectangle
    return index < m_close_bounds.size() ? m_close_bounds[index] : albor::Rect{};
}

std::uint64_t studio::DocumentTabs::strip_key(const float width, const float height) const {
    // the key under construction
    std::uint64_t key = 0;
    // mix one value into the running key
    const auto mix = [&key](const std::uint64_t value) { key ^= value + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2); };
    // the strip extent decides the tab layout
    mix(static_cast<std::uint64_t>(std::hash<float>{}(width)));
    mix(static_cast<std::uint64_t>(std::hash<float>{}(height)));
    // every document decides the content and the close affordances
    for (const DocumentTabSpec& document : m_documents) {
        // the title length keeps concatenated names apart
        mix(document.title.size());
        // every character contributes to the key
        for (const char character : document.title)
            mix(static_cast<unsigned char>(character));
        // a closable tab draws the cross
        mix(document.closable ? 1U : 0U);
    }
    // the active tab draws its plate and underline
    mix(static_cast<std::uint64_t>(m_active));
    // every channel of every role the strip paints changes the picture
    const auto mix_color = [&mix](const ChartColor& color) {
        // every channel contributes to the key
        mix(static_cast<std::uint64_t>(std::hash<float>{}(color.r)));
        mix(static_cast<std::uint64_t>(std::hash<float>{}(color.g)));
        mix(static_cast<std::uint64_t>(std::hash<float>{}(color.b)));
        mix(static_cast<std::uint64_t>(std::hash<float>{}(color.a)));
    };
    // the palette roles the strip reads
    mix_color(m_palette.text);
    mix_color(m_palette.muted);
    mix_color(m_palette.background);
    mix_color(m_palette.panel);
    mix_color(m_palette.border);
    mix_color(m_palette.grid);
    // avoid the empty-cache sentinel
    return key != 0 ? key : 1;
}

std::shared_ptr<const albor::PictureResource> studio::DocumentTabs::record_strip(const float width, const float height) {
    // the recorder collects the strip-local commands
    albor::PictureRecorder recorder;
    // record the plate, hairline and tabs
    paint_strip(recorder, width, height);
    // count the recorded picture so the cache tests can inspect reuse
    m_record_count++;
    // freeze the strip picture
    return m_resources.create_picture(recorder);
}

void studio::DocumentTabs::paint_content(albor::RenderContext& context) {
    // an empty strip draws nothing
    if (bounds().empty())
        return;
    // compute the state key of the strip picture
    const std::uint64_t key = strip_key(bounds().width, bounds().height);
    // record only when the strip state changed
    if (m_layer.key != key || m_layer.picture == nullptr) {
        // record the strip-local picture
        m_layer.picture = record_strip(bounds().width, bounds().height);
        // remember the state the picture was recorded for
        m_layer.key = key;
    }
    // replay the strip at the widget origin
    if (m_layer.picture != nullptr)
        context.draw_picture(m_layer.picture, albor::Transform::translation(bounds().x, bounds().y));
}

void studio::DocumentTabs::paint_strip(albor::RenderContext& context, const float width, const float height) {
    // the strip plate separates the tabs from the body
    context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = width, .height = height}), albor::Paint{.fill = albor::Color::rgba(m_palette.background.r, m_palette.background.g, m_palette.background.b, m_palette.background.a)});
    // the hairline under the strip closes it against the body
    context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = height - 1.0f}, albor::Size{.width = width, .height = 1.0f}), albor::Paint{.fill = albor::Color::rgba(m_palette.border.r, m_palette.border.g, m_palette.border.b, m_palette.border.a)});
    // one plate, label and close affordance is painted per tab
    for (std::size_t index = 0; index < m_documents.size() && index < m_tab_bounds.size(); ++index) {
        // the tab rectangle, in strip-local coordinates
        const albor::Rect tab = m_tab_bounds[index];
        // the active tab takes the panel face
        if (index == m_active)
            context.draw_rect(tab, albor::Paint{.fill = albor::Color::rgba(m_palette.panel.r, m_palette.panel.g, m_palette.panel.b, m_palette.panel.a)});
        // the active tab carries a short underline
        if (index == m_active)
            context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = tab.x, .y = height - 2.0f}, albor::Size{.width = tab.width, .height = 2.0f}), albor::Paint{.fill = albor::Color::rgba(m_palette.text.r, m_palette.text.g, m_palette.text.b, m_palette.text.a)});
        // a recorded close box draws the cross
        const albor::Rect close = index < m_close_bounds.size() ? m_close_bounds[index] : albor::Rect{};
        // a recorded close box draws the cross
        if (close.width > 0.0f) {
            // the close cross color
            const albor::Color cross = albor::Color::rgba(m_palette.muted.r, m_palette.muted.g, m_palette.muted.b, m_palette.muted.a);
            // the first diagonal
            const std::array<albor::Point, 2> first_cross{{albor::Point{.x = close.x + 3.0f, .y = close.y + 3.0f}, albor::Point{.x = close.x + 9.0f, .y = close.y + 9.0f}}};
            // the second diagonal
            const std::array<albor::Point, 2> second_cross{{albor::Point{.x = close.x + 9.0f, .y = close.y + 3.0f}, albor::Point{.x = close.x + 3.0f, .y = close.y + 9.0f}}};
            // draw the first diagonal
            context.draw_polyline(first_cross, albor::StrokeStyle{.color = cross, .width = 1.0f});
            // draw the second diagonal
            context.draw_polyline(second_cross, albor::StrokeStyle{.color = cross, .width = 1.0f});
        }
        // the label stays inside the tab
        context.save();
        // clip the label to the tab plate
        context.clip_rect(tab);
        // measure the title once
        const albor::TextLayout& label = label_layout(m_documents[index].title);
        // centre the label vertically in the strip
        const albor::Point origin{.x = tab.x + STRIP_TAB_PADDING, .y = (height - label.size.height) * 0.5f};
        // the active label is emphasized
        draw_text(context, label, origin, albor::Paint{.fill = albor::Color::rgba(index == m_active ? m_palette.text.r : m_palette.muted.r, index == m_active ? m_palette.text.g : m_palette.muted.g, index == m_active ? m_palette.text.b : m_palette.muted.b, index == m_active ? m_palette.text.a : m_palette.muted.a)});
        // the tab clip ends
        context.restore();
    }
}

std::size_t studio::DocumentTabs::strip_record_count() const { return m_record_count; }

void studio::DocumentTabs::dispatch_event(albor::Event& event) {
    // a hidden strip takes no input
    if (!visible())
        return;
    // a plain key moves or activates the selection, which is what a focused tab strip does
    if (event.kind == albor::EventKind::KeyDown && event.modifiers.none()) {
        // the document the key selects
        std::size_t target = m_active;
        // the arrows and the ends move the selection when there is room
        if (event.key == albor::Key::ArrowLeft && m_active > 0)
            target = m_active - 1;
        else if (event.key == albor::Key::ArrowRight && m_active + 1 < m_documents.size())
            target = m_active + 1;
        else if (event.key == albor::Key::Home)
            target = 0;
        else if (event.key == albor::Key::End)
            target = m_documents.empty() ? 0 : m_documents.size() - 1;
        // a moved selection is reported and consumed
        if (target != m_active) {
            m_active = target;
            invalidate();
            event.handled = true;
            if (m_on_selected)
                m_on_selected(static_cast<int>(target));
            return;
        }
        // return and space activate the document that already holds the selection
        if (event.key == albor::Key::Return || event.key == albor::Key::Space) {
            event.handled = true;
            if (m_on_selected)
                m_on_selected(static_cast<int>(m_active));
            return;
        }
        // any other key belongs to an ancestor
        return;
    }
    // only a press selects or closes a tab
    if (event.kind != albor::EventKind::PointerDown)
        return;
    // the press in strip-local coordinates
    const albor::Point local{.x = event.position.x - bounds().x, .y = event.position.y - bounds().y};
    // a press outside the strip belongs to whatever is behind it
    if (local.y < 0.0f || local.y >= bounds().height || local.x < 0.0f || local.x >= bounds().width)
        return;
    // a press on a close affordance closes the tab
    for (std::size_t index = 0; index < m_close_bounds.size(); ++index) {
        // the close rect of this tab
        const albor::Rect& close = m_close_bounds[index];
        // an empty rect or a miss keeps looking
        if (close.width <= 0.0f || local.x < close.x || local.x > close.x + close.width || local.y < close.y || local.y > close.y + close.height)
            continue;
        // the strip consumed the close press
        event.handled = true;
        // report the close to the shell
        if (m_on_closed)
            m_on_closed(static_cast<int>(index));
        // the press is settled
        return;
    }
    // a press on a tab body selects it
    for (std::size_t index = 0; index < m_tab_bounds.size(); ++index) {
        // the tab rect of this index
        const albor::Rect& tab = m_tab_bounds[index];
        // a miss keeps looking
        if (local.x < tab.x || local.x > tab.x + tab.width)
            continue;
        // the strip consumed the selection press
        event.handled = true;
        // report the selection to the shell
        if (m_on_selected)
            m_on_selected(static_cast<int>(index));
        // the press is settled
        return;
    }
    // the strip consumes any other press so it cannot reach the body
    event.handled = true;
}

albor::SemanticNode studio::DocumentTabs::semantic_node() const {
    // the strip groups the documents and owns their single selection
    albor::SemanticNode node{albor::SemanticRole::TabView};
    // carry the author id so the automation can select the strip
    node.set_explicit_id(identity().explicit_id);
    // the strip is named for what it selects, with the active document when there is one
    node.set_name(m_documents.empty() ? "Documents" : "Documents, active: " + m_documents[m_active].title);
    // place the announcement where the strip was arranged
    node.set_bounds(bounds());
    // expose one select and close action per document
    for (std::size_t index = 0; index < m_documents.size(); ++index) {
        // the actions address the document by its recorded position
        const int tab_index = static_cast<int>(index);
        // select the document the action names
        (void)node.add_action("select-tab-" + std::to_string(index), [this, tab_index] {
            // report the selection to the shell
            if (m_on_selected)
                m_on_selected(tab_index);
        });
        // a closable document also exposes its close affordance
        if (m_documents[index].closable)
            (void)node.add_action("close-tab-" + std::to_string(index), [this, tab_index] {
                // report the close to the shell
                if (m_on_closed)
                    m_on_closed(tab_index);
            });
    }
    // hand the node to the adapter
    return node;
}

std::size_t studio::DocumentTabs::active_index() const { return m_active; }

albor::Rect studio::DocumentTabs::focus_ring_rect() const {
    // the ring frames the active tab, which is the document the keys act on
    if (m_active < m_tab_bounds.size()) {
        // the tab in view coordinates
        const albor::Rect tab = m_tab_bounds[m_active];
        // hand the ring the tab, inset from the strip edges
        return albor::Rect::from_origin_size(albor::Point{.x = tab.x + bounds().x, .y = tab.y + bounds().y + 3.0f}, albor::Size{.width = tab.width, .height = std::max(0.0f, tab.height - 6.0f)});
    }
    // a strip without a laid out tab falls back to the whole strip
    return albor::Widget::focus_ring_rect();
}

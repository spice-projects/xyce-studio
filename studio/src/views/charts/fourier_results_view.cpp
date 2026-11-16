#include "views/charts/fourier_results_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "albor/core/constraints.h"
#include "albor/core/paint.h"
#include "albor/semantics/semantic_node.h"
#include "albor/semantics/semantic_provider.h"
#include "albor/text/text_engine.h"
#include "albor/widgets/scroll_thumb.h"
#include "albor/widgets/scroll_wheel.h"
#include "charts/chart_style.h"

namespace
{
    // the horizontal inset that keeps the text off the panel edges
    constexpr float TABLE_PADDING = 14.0f;

    // the height of the title band above the scrollable body
    constexpr float TITLE_HEIGHT = 42.0f;

    // the height of the signal line of one signal and step band
    constexpr float BLOCK_TITLE_HEIGHT = 30.0f;

    // the height of the settings line under the signal line
    constexpr float BLOCK_SETTINGS_HEIGHT = 18.0f;

    // the height of the whole band, which carries the signal and the analysis settings
    constexpr float BLOCK_HEIGHT = BLOCK_TITLE_HEIGHT + BLOCK_SETTINGS_HEIGHT;

    // the height of the column header row of one block
    constexpr float COLUMN_HEIGHT = 26.0f;

    // the height of one harmonic row
    constexpr float ROW_HEIGHT = 24.0f;

    // the gap that separates two blocks
    constexpr float BLOCK_GAP = 12.0f;

    // the width of the harmonic index column, which the numeric columns share the rest with
    constexpr float HARMONIC_WIDTH = 78.0f;

    // how much of a narrow table the harmonic index column takes
    constexpr float HARMONIC_SHARE = 0.16f;

    // the numeric columns every row carries: frequency, magnitude, phase and the two normalized cells
    constexpr float NUMERIC_COLUMNS = 5.0f;

    // the size of the dataset title, which is the only label larger than the body
    constexpr float TITLE_FONT_SIZE = 15.0f;

    // the opacity of a striped harmonic row
    constexpr float STRIPE_ALPHA = 0.4f;

    // the line the header band of one block shows, which names the signal, the step and the THD
    std::string block_heading(const fourier::SignalTable& table) {
        // a single-step file names the signal and its THD only
        if (table.step.empty())
            return table.signal + "  -  THD: " + table.thd;
        // a stepped file names the step between the signal and the THD
        return table.signal + "  -  " + table.step + "  -  THD: " + table.thd;
    }

    // the line the settings strip of one block shows, which names what the analysis did
    std::string settings_line(const fourier::SignalTable& table) {
        // the parts the file recorded, joined as one strip
        std::string line;
        // the harmonic count heads the strip when the file records one
        if (!table.harmonics.empty())
            line += "No. Harmonics: " + table.harmonics;
        // the grid points follow the harmonic count
        if (!table.gridsize.empty())
            line += (line.empty() ? "" : "  -  ") + std::string("Gridsize: ") + table.gridsize;
        // the interpolation type closes the strip
        if (!table.interpolation.empty())
            line += (line.empty() ? "" : "  -  ") + table.interpolation;
        // hand the strip back
        return line;
    }

    // the line one harmonic row announces, which reads the cells in the column order
    std::string row_heading(const fourier::ResultRow& row) {
        // the harmonic index, the frequency and the value in one line
        return "Harmonic " + std::to_string(row.harmonic) + ", " + row.frequency + " Hz, " + row.magnitude + ", " + row.phase + " deg, normalized " + row.norm_magnitude + ", " + row.norm_phase + " deg";
    }

    // the semantic node of one signal and step block, which carries its harmonic rows
    class BlockProvider final : public albor::SemanticProvider
    {
    public:
        // build the provider over the block it describes, its identity and the announced extents of its rows
        BlockProvider(const fourier::SignalTable* table, albor::SemanticNode node, std::string id, albor::Rect settings, std::vector<albor::Rect> rows) :
            m_table(table), m_node(std::move(node)), m_settings(settings), m_id(std::move(id)), m_rows(std::move(rows)) {}

        [[nodiscard]] albor::SemanticNode semantic_node() const override { return m_node; }

        [[nodiscard]] std::size_t semantic_child_count() const override { return m_table != nullptr ? m_table->rows.size() + 1 : 0; }

        [[nodiscard]] std::unique_ptr<albor::SemanticProvider> semantic_child(const std::size_t index) const override {
            // a block that lost its table announces no child
            if (m_table == nullptr || index > m_table->rows.size())
                return nullptr;
            // the settings strip is the first child of the block
            if (index == 0) {
                // the strip is announced as a label carrying what the analysis did
                albor::SemanticNode node{albor::SemanticRole::Label};
                // the author id keeps the strip addressable by automation
                node.set_explicit_id(m_id + "_settings");
                // the strip text names the harmonics, the grid and the interpolation
                node.set_name(settings_line(*m_table));
                // the extent places the announcement where the strip is drawn
                node.set_bounds(m_settings);
                // hand the strip to the walk
                return std::make_unique<albor::SemanticNodeProvider>(std::move(node));
            }
            // the harmonic rows follow the settings strip
            const std::size_t harmonic = index - 1;
            // the row is announced as a label carrying its cells
            albor::SemanticNode node{albor::SemanticRole::Label};
            // the author id keeps every row addressable by automation
            node.set_explicit_id(m_id + "_row_" + std::to_string(harmonic));
            // the row text names the harmonic and its values
            node.set_name(row_heading(m_table->rows[harmonic]));
            // the extent places the announcement where the row is drawn
            node.set_bounds(harmonic < m_rows.size() ? m_rows[harmonic] : albor::Rect{});
            // hand the node to the walk
            return std::make_unique<albor::SemanticNodeProvider>(std::move(node));
        }

    private:
        // the block the provider describes, which the view owns
        const fourier::SignalTable* m_table;

        // the description of the block itself
        albor::SemanticNode m_node;

        // the announced extent of the settings strip
        albor::Rect m_settings;

        // the author id the block and its rows are addressed by
        std::string m_id;

        // the announced extent of every row
        std::vector<albor::Rect> m_rows;
    };

    // mix one float into the running key
    void mix_float(std::uint64_t& key, const float value) {
        // combine the value with the running seed
        key ^= static_cast<std::uint64_t>(std::hash<float>{}(value)) + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2);
    }

    // mix one string into the running key
    void mix_text(std::uint64_t& key, const std::string& text) {
        // the length keeps concatenated names distinct
        mix_float(key, static_cast<float>(text.size()));
        // every character contributes to the key
        for (const char character : text)
            mix_float(key, static_cast<float>(static_cast<unsigned char>(character)));
    }
} // namespace

studio::FourierResultsView::FourierResultsView(albor::BindingEngine* engine, albor::TextEngine* text) :
    albor::Widget(engine, "fourier_results_view"), m_outline_resolver(text != nullptr ? text->glyph_outline_resolver() : albor::GlyphOutlineResolver{}), m_text(text) {}

studio::FourierResultsView::~FourierResultsView() {
    // tell the presenter that borrowed this view that it is gone
    if (m_on_destroyed)
        m_on_destroyed();
}

void studio::FourierResultsView::set_title(std::string title) {
    // an unchanged title redraws nothing
    if (m_title == title)
        return;
    // keep the title the header band shows
    m_title = std::move(title);
    // the shaped run no longer describes the title
    m_title_shaped = false;
    // the header band changed
    invalidate();
}

void studio::FourierResultsView::set_tables(std::vector<fourier::SignalTable> tables) {
    // keep the blocks the table lists
    m_tables = std::move(tables);
    // a new dataset opens at its first block
    m_offset = 0.0f;
    // the drag belonged to the previous content
    m_dragging = false;
    // the shaped labels belonged to the previous content
    m_labels.clear();
    // the shaped cells belonged to the previous content
    m_cell_labels.clear();
    // the table changed, so it redraws
    invalidate();
}

void studio::FourierResultsView::set_palette(const ChartPalette& palette) {
    // keep the palette the next frame paints with
    m_palette = palette;
    // the whole table changed
    invalidate();
}

void studio::FourierResultsView::set_on_destroyed(std::function<void()> handler) { m_on_destroyed = std::move(handler); }

float studio::FourierResultsView::offset() const { return m_offset; }

float studio::FourierResultsView::maximum_offset() const { return std::max(0.0f, content_height() - body_height()); }

float studio::FourierResultsView::body_top() const { return TITLE_HEIGHT + 1.0f; }

float studio::FourierResultsView::body_height() const { return std::max(0.0f, bounds().height - body_top()); }

float studio::FourierResultsView::block_height(const fourier::SignalTable& table) const { return BLOCK_HEIGHT + COLUMN_HEIGHT + ROW_HEIGHT * static_cast<float>(table.rows.size()); }

float studio::FourierResultsView::content_height() const {
    // an empty table has nothing to scroll
    if (m_tables.empty())
        return 0.0f;
    // the gap after the last block is not part of the content
    const float stacked = block_top(m_tables.size()) - BLOCK_GAP;
    // the content keeps a bottom inset below the last block
    return stacked + TABLE_PADDING;
}

float studio::FourierResultsView::block_top(const std::size_t index) const {
    // accumulate the height of every block above the requested one
    float top = 0.0f;
    // walk the blocks in order
    for (std::size_t block = 0; block < index && block < m_tables.size(); ++block)
        top += block_height(m_tables[block]) + BLOCK_GAP;
    // hand out the accumulated top
    return top;
}

float studio::FourierResultsView::scrollbar_extent() const {
    // a themed shell sizes the bar with its own metrics
    if (const albor::Theme* installed = theme())
        return installed->metrics().scrollbar_extent;
    // an unthemed table falls back to the framework default
    return albor::SCROLLBAR_EXTENT;
}

float studio::FourierResultsView::scroll_line_extent() const {
    // a themed shell states how far one notch travels
    if (const albor::Theme* installed = theme())
        return installed->metrics().scroll_line_extent;
    // an unthemed table falls back to the framework default
    return albor::SCROLL_LINE_EXTENT;
}

float studio::FourierResultsView::scrollbar_thumb_minimum() const {
    // a themed shell states the smallest thumb it draws
    if (const albor::Theme* installed = theme())
        return installed->metrics().scrollbar_thumb_minimum;
    // an unthemed table falls back to the framework default
    return albor::SCROLLBAR_THUMB_MINIMUM;
}

albor::Rect studio::FourierResultsView::scrollbar_track() const {
    // a table that fits carries no bar
    if (maximum_offset() <= 0.0f)
        return {};
    // the bar sits against the east edge, inside the body
    return albor::Rect::from_origin_size(albor::Point{.x = std::max(0.0f, bounds().width - scrollbar_extent() - 2.0f), .y = body_top()}, albor::Size{.width = scrollbar_extent(), .height = body_height()});
}

albor::Rect studio::FourierResultsView::scrollbar_thumb() const {
    // the track of a table that fits is empty, and so is its thumb
    const albor::Rect track = scrollbar_track();
    // an empty track has no thumb
    if (track.empty())
        return {};
    // the thumb covers the share of the content the viewport shows
    return albor::scroll_thumb_rect(m_offset, maximum_offset(), track, scrollbar_thumb_minimum());
}

void studio::FourierResultsView::clamp_offset() {
    // the offset never leaves the scrollable range
    m_offset = std::clamp(m_offset, 0.0f, maximum_offset());
}

bool studio::FourierResultsView::scroll_to(const float destination) {
    // remember the offset the move started from
    const float previous = m_offset;
    // clamp the requested destination into the range
    m_offset = std::clamp(destination, 0.0f, maximum_offset());
    // an unchanged offset moves nothing
    if (m_offset == previous)
        return false;
    // the visible rows changed
    invalidate();
    // report the move
    return true;
}

std::size_t studio::FourierResultsView::block_count() const { return m_tables.size(); }

std::size_t studio::FourierResultsView::record_count() const { return m_record_count; }

void studio::FourierResultsView::measure(const albor::SizeConstraints& constraints, albor::Size& out_size) {
    // measure the children of the widget, which the table has none of
    albor::Size children{};
    // let the base widget measure its children
    albor::UiNode::measure(constraints, children);
    // the table asks for a useful default when the host leaves it unbounded
    albor::Size content{.width = 640.0f, .height = 480.0f};
    // a bounded width is what the table fills
    if (std::isfinite(constraints.maximum.width))
        content.width = constraints.maximum.width;
    // a bounded height is what the table fills
    if (std::isfinite(constraints.maximum.height))
        content.height = constraints.maximum.height;
    // clamp the fill into the offered constraints
    m_measured_size = constraints.constrain(content);
    // the preferred size follows the measured one
    m_preferred_size = m_measured_size;
    // hand the parent the extent
    out_size = m_measured_size;
}

void studio::FourierResultsView::arrange(const albor::Rect& bounds) {
    // let the base component arrange the bounds
    albor::Component::arrange(bounds);
    // a shorter panel scrolls less
    clamp_offset();
}

const albor::TextLayout& studio::FourierResultsView::body_layout(const std::string& text) {
    // an empty cache entry stands in for a table without a text engine
    static const albor::TextLayout empty;
    // a table without a text engine shapes nothing
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

const albor::TextLayout& studio::FourierResultsView::cell_layout(const std::string& text) {
    // an empty cache entry stands in for a table without a text engine
    static const albor::TextLayout empty;
    // a table without a text engine shapes nothing
    if (m_text == nullptr)
        return empty;
    // reuse the run a previous paint shaped
    if (const auto found = m_cell_labels.find(text); found != m_cell_labels.end())
        return found->second;
    // a backend that resolves no family measures every cell with the body face
    if (!m_text->resolves_font_selection())
        return body_layout(text);
    // the numbers line up by column through a monospaced face
    const albor::TextLayout shaped = m_text->measure(text, albor::TextEngine::UNBOUNDED_WIDTH, CHART_LABEL_FONT_SIZE, albor::FontSpec{.family = "JetBrains Mono"});
    // keep the shaped run for the next paint
    return m_cell_labels.emplace(text, shaped).first->second;
}

const albor::TextLayout& studio::FourierResultsView::title_layout() {
    // an empty cache entry stands in for a table without a text engine
    static const albor::TextLayout empty;
    // a table without a text engine shapes nothing
    if (m_text == nullptr)
        return empty;
    // shape the title once per title
    if (!m_title_shaped) {
        // the title is the only label at its size
        m_title_layout = m_text->measure(m_title, albor::TextEngine::UNBOUNDED_WIDTH, TITLE_FONT_SIZE);
        // remember the run describes the current title
        m_title_shaped = true;
    }
    // hand out the shaped title
    return m_title_layout;
}

void studio::FourierResultsView::draw_text(albor::RenderContext& context, const albor::TextLayout& layout, const albor::Point origin, const albor::Paint& paint) {
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

void studio::FourierResultsView::draw_right(albor::RenderContext& context, const std::string& text, const float left, const float right, const float centre_y, const albor::Color color) {
    // measure the label once
    const albor::TextLayout& layout = body_layout(text);
    // the run ends at the column edge
    const albor::Point origin{.x = right - layout.size.width, .y = centre_y - layout.size.height * 0.5f};
    // a narrow column clips its own label instead of drawing over its neighbour
    context.save();
    // the clip spans the column the label belongs to, in table-local coordinates
    context.clip_rect(albor::Rect::from_origin_size(albor::Point{.x = left, .y = 0.0f}, albor::Size{.width = std::max(0.0f, right - left), .height = bounds().height}));
    // draw the label in the role the row names
    draw_text(context, layout, origin, albor::Paint{.fill = color});
    // the clip ends with the label
    context.restore();
}

void studio::FourierResultsView::draw_cell(albor::RenderContext& context, const std::string& text, const float left, const float right, const float centre_y, const albor::Color color) {
    // measure the cell once
    const albor::TextLayout& layout = cell_layout(text);
    // the run ends at the column edge
    const albor::Point origin{.x = right - layout.size.width, .y = centre_y - layout.size.height * 0.5f};
    // a narrow column clips its own cell instead of drawing over its neighbour
    context.save();
    // the clip spans the column the cell belongs to, in table-local coordinates
    context.clip_rect(albor::Rect::from_origin_size(albor::Point{.x = left, .y = 0.0f}, albor::Size{.width = std::max(0.0f, right - left), .height = bounds().height}));
    // draw the cell in the role the row names
    draw_text(context, layout, origin, albor::Paint{.fill = color});
    // the clip ends with the cell
    context.restore();
}

void studio::FourierResultsView::draw_left(albor::RenderContext& context, const std::string& text, const float left, const float right, const float centre_y, const albor::Color color) {
    // measure the label once
    const albor::TextLayout& layout = body_layout(text);
    // the run starts at the column edge
    const albor::Point origin{.x = left, .y = centre_y - layout.size.height * 0.5f};
    // a narrow panel clips its own label instead of drawing past the edge
    context.save();
    // the clip spans the label the caller names, in table-local coordinates
    context.clip_rect(albor::Rect::from_origin_size(albor::Point{.x = left, .y = 0.0f}, albor::Size{.width = std::max(0.0f, right - left), .height = bounds().height}));
    // draw the label in the role the row names
    draw_text(context, layout, origin, albor::Paint{.fill = color});
    // the clip ends with the label
    context.restore();
}

std::uint64_t studio::FourierResultsView::layer_key(const float width, const float height) const {
    // the key under construction
    std::uint64_t key = 0;
    // the arranged extent decides where every column and row sits
    mix_float(key, width);
    mix_float(key, height);
    // the scroll offset decides which rows are visible
    mix_float(key, m_offset);
    // the dataset title sits in the header band
    mix_text(key, m_title);
    // every block and row decides the content
    for (const fourier::SignalTable& table : m_tables) {
        // the block header names the signal, its step, its THD and its settings
        mix_text(key, table.signal);
        mix_text(key, table.step);
        mix_text(key, table.thd);
        mix_text(key, table.harmonics);
        mix_text(key, table.gridsize);
        mix_text(key, table.interpolation);
        // every harmonic row carries five cells
        for (const fourier::ResultRow& row : table.rows) {
            // the index and the cells change the picture
            mix_float(key, static_cast<float>(row.harmonic));
            mix_text(key, row.frequency);
            mix_text(key, row.magnitude);
            mix_text(key, row.phase);
            mix_text(key, row.norm_magnitude);
            mix_text(key, row.norm_phase);
        }
    }
    // every channel of every role the table paints changes the picture
    const std::array<ChartColor, 6> roles{m_palette.text, m_palette.muted, m_palette.background, m_palette.panel, m_palette.border, m_palette.grid};
    // mix every role the table reads
    for (const ChartColor& color : roles) {
        mix_float(key, color.r);
        mix_float(key, color.g);
        mix_float(key, color.b);
        mix_float(key, color.a);
    }
    // avoid the empty-cache sentinel
    return key != 0 ? key : 1;
}

std::shared_ptr<const albor::PictureResource> studio::FourierResultsView::record_layer(const float width, const float height) {
    // the recorder collects the table-local commands
    albor::PictureRecorder recorder;
    // record the header, the blocks and the scrollbar
    paint_layer(recorder, width, height);
    // count the recorded picture so the cache tests can inspect reuse
    m_record_count++;
    // freeze the table picture
    return m_resources.create_picture(recorder);
}

void studio::FourierResultsView::paint_content(albor::RenderContext& context) {
    // an empty table draws nothing
    if (bounds().empty())
        return;
    // compute the state key of the table picture
    const std::uint64_t key = layer_key(bounds().width, bounds().height);
    // record only when the table state changed
    if (m_layer.key != key || m_layer.picture == nullptr) {
        // record the table-local picture
        m_layer.picture = record_layer(bounds().width, bounds().height);
        // remember the state the picture was recorded for
        m_layer.key = key;
    }
    // replay the table picture at the widget origin
    if (m_layer.picture != nullptr)
        context.draw_picture(m_layer.picture, albor::Transform::translation(bounds().x, bounds().y));
}

void studio::FourierResultsView::paint_layer(albor::RenderContext& context, const float width, const float height) {
    // the page color fills the whole panel
    const albor::Color surface = albor::Color::rgba(m_palette.background.r, m_palette.background.g, m_palette.background.b, m_palette.background.a);
    // the panel face separates the bands and the striped rows
    const albor::Color face = albor::Color::rgba(m_palette.panel.r, m_palette.panel.g, m_palette.panel.b, m_palette.panel.a);
    // the text role names the values and the title
    const albor::Color ink = albor::Color::rgba(m_palette.text.r, m_palette.text.g, m_palette.text.b, m_palette.text.a);
    // the muted role names the captions
    const albor::Color quiet = albor::Color::rgba(m_palette.muted.r, m_palette.muted.g, m_palette.muted.b, m_palette.muted.a);
    // the border role draws the hairlines and the scrollbar
    const albor::Color line = albor::Color::rgba(m_palette.border.r, m_palette.border.g, m_palette.border.b, m_palette.border.a);
    // the panel behind the table
    context.draw_rect(albor::Rect::from_origin_size(albor::Point{}, albor::Size{.width = width, .height = height}), albor::Paint{.fill = surface});
    // the title band sits above the body
    const albor::TextLayout& title = title_layout();
    // the clip keeps a title longer than the band inside it
    context.save();
    // the clip spans the header band
    context.clip_rect(albor::Rect::from_origin_size(albor::Point{}, albor::Size{.width = width, .height = TITLE_HEIGHT}));
    // the title is centred in its band
    draw_text(context, title, albor::Point{.x = TABLE_PADDING, .y = (TITLE_HEIGHT - title.size.height) * 0.5f}, albor::Paint{.fill = ink});
    // the clip ends with the title
    context.restore();
    // the hairline closes the title band against the body
    context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = TITLE_HEIGHT}, albor::Size{.width = width, .height = 1.0f}), albor::Paint{.fill = line});
    // the column edges every block shares
    const Columns edges = columns(width);
    // an empty table still explains itself under the title
    if (m_tables.empty()) {
        // the readout names the missing content
        draw_left(context, "This dataset carries no Fourier results.", TABLE_PADDING, width - TABLE_PADDING, body_top() + 12.0f, quiet);
        // an empty table has nothing else to paint
        return;
    }
    // the body clips every block to the scrollable span
    context.save();
    // the body starts under the title band
    context.clip_rect(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = body_top()}, albor::Size{.width = width, .height = body_height()}));
    // one block is painted per signal and step
    for (std::size_t index = 0; index < m_tables.size(); ++index) {
        // the block this pass paints
        const fourier::SignalTable& table = m_tables[index];
        // the block-local top under the scrolled offset
        const float top = body_top() - m_offset + block_top(index);
        // a block below the visible span ends the paint, because every later block is lower
        if (top > height)
            break;
        // a block above the visible span is skipped without painting
        if (top + block_height(table) < body_top())
            continue;
        // the band carries the signal, its step, its THD and the analysis settings
        context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = top}, albor::Size{.width = width, .height = BLOCK_HEIGHT}), albor::Paint{.fill = face});
        // the signal names the block on its leading edge
        draw_left(context, table.signal, TABLE_PADDING, edges.harmonic_right, top + BLOCK_TITLE_HEIGHT * 0.5f, ink);
        // the THD closes the signal line on its trailing edge
        draw_right(context, "THD: " + table.thd, edges.phase_right, edges.right, top + BLOCK_TITLE_HEIGHT * 0.5f, quiet);
        // the settings line names what the analysis did
        draw_left(context, settings_line(table), TABLE_PADDING, edges.right, top + BLOCK_TITLE_HEIGHT + BLOCK_SETTINGS_HEIGHT * 0.5f, quiet);
        // the caption row names the harmonic index and the five numeric columns
        const float column_y = top + BLOCK_HEIGHT + COLUMN_HEIGHT * 0.5f;
        // the harmonic index caption is right aligned like its cells
        draw_right(context, "Harmonic", edges.left, edges.harmonic_right, column_y, quiet);
        // the frequency caption is right aligned like its cells
        draw_right(context, "Frequency (Hz)", edges.harmonic_right, edges.frequency_right, column_y, quiet);
        // the magnitude caption is right aligned like its cells
        draw_right(context, "Magnitude", edges.frequency_right, edges.magnitude_right, column_y, quiet);
        // the phase caption is right aligned like its cells
        draw_right(context, "Phase (deg)", edges.magnitude_right, edges.phase_right, column_y, quiet);
        // the normalized magnitude caption is right aligned like its cells
        draw_right(context, "Norm. Mag", edges.phase_right, edges.norm_magnitude_right, column_y, quiet);
        // the normalized phase caption closes the caption row
        draw_right(context, "Norm. Phase", edges.norm_magnitude_right, edges.right, column_y, quiet);
        // the hairline separates the captions from the rows
        context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = top + BLOCK_HEIGHT + COLUMN_HEIGHT - 1.0f}, albor::Size{.width = width, .height = 1.0f}), albor::Paint{.fill = line});
        // one row is painted per harmonic
        for (std::size_t harmonic = 0; harmonic < table.rows.size(); ++harmonic) {
            // the row this pass paints
            const fourier::ResultRow& row = table.rows[harmonic];
            // the row top under the caption row
            const float row_top = top + BLOCK_HEIGHT + COLUMN_HEIGHT + ROW_HEIGHT * static_cast<float>(harmonic);
            // every second row carries a tint, which keeps a long block readable
            if (harmonic % 2 == 1) {
                // the stripe keeps the panel color at a low alpha
                const albor::Color stripe = albor::Color::rgba(m_palette.panel.r, m_palette.panel.g, m_palette.panel.b, m_palette.panel.a * STRIPE_ALPHA);
                // the stripe spans the table width
                context.draw_rect(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = row_top}, albor::Size{.width = width, .height = ROW_HEIGHT}), albor::Paint{.fill = stripe});
            }
            // the row centre is where every cell is centred
            const float row_y = row_top + ROW_HEIGHT * 0.5f;
            // the harmonic index closes on its column edge
            draw_cell(context, std::to_string(row.harmonic), edges.left, edges.harmonic_right, row_y, ink);
            // the harmonic frequency closes on its column edge
            draw_cell(context, row.frequency, edges.harmonic_right, edges.frequency_right, row_y, ink);
            // the spectrum magnitude closes on its column edge
            draw_cell(context, row.magnitude, edges.frequency_right, edges.magnitude_right, row_y, ink);
            // the spectrum phase closes on its column edge
            draw_cell(context, row.phase, edges.magnitude_right, edges.phase_right, row_y, ink);
            // the magnitude relative to the fundamental closes on its column edge
            draw_cell(context, row.norm_magnitude, edges.phase_right, edges.norm_magnitude_right, row_y, ink);
            // the phase relative to the fundamental closes on the last column
            draw_cell(context, row.norm_phase, edges.norm_magnitude_right, edges.right, row_y, ink);
        }
    }
    // the clip ends with the body
    context.restore();
    // the scrollbar travels the body span while the content overflows it
    const albor::Rect track = scrollbar_track();
    // a table that fits draws no bar
    if (track.empty())
        return;
    // the track is a quiet rounded gutter
    context.draw_rounded_rect(track, track.width * 0.5f, albor::Paint{.fill = albor::Color::rgba(m_palette.grid.r, m_palette.grid.g, m_palette.grid.b, m_palette.grid.a)});
    // the thumb names how much of the content the viewport shows
    const albor::Rect thumb = scrollbar_thumb();
    // a thumb always spans something
    if (!thumb.empty())
        context.draw_rounded_rect(thumb, thumb.width * 0.5f, albor::Paint{.fill = quiet});
}

studio::FourierResultsView::Columns studio::FourierResultsView::columns(const float width) const {
    // the column edges of the arranged table
    Columns edges;
    // the scrollbar gutter narrows the content while the table scrolls
    const float gutter = maximum_offset() > 0.0f ? scrollbar_extent() + TABLE_PADDING * 0.5f : 0.0f;
    // the first column starts inside the leading inset
    edges.left = TABLE_PADDING;
    // the last column ends inside the trailing inset
    edges.right = std::max(edges.left, width - TABLE_PADDING - gutter);
    // the harmonic index column keeps a fixed share of the width
    const float available = edges.right - edges.left;
    // the harmonic index column narrows with a narrow table
    const float harmonic = std::min(HARMONIC_WIDTH, available * HARMONIC_SHARE);
    // the numeric columns share the rest
    const float numeric = std::max(0.0f, available - harmonic) / NUMERIC_COLUMNS;
    // the harmonic index column closes on its edge
    edges.harmonic_right = edges.left + harmonic;
    // the frequency column closes on its edge
    edges.frequency_right = edges.harmonic_right + numeric;
    // the magnitude column closes on its edge
    edges.magnitude_right = edges.frequency_right + numeric;
    // the phase column closes on its edge
    edges.phase_right = edges.magnitude_right + numeric;
    // the normalized magnitude column closes on its edge
    edges.norm_magnitude_right = edges.phase_right + numeric;
    // hand the edges back, the last one closing the normalized phase column
    return edges;
}

void studio::FourierResultsView::dispatch_event(albor::Event& event) {
    // a hidden table takes no input
    if (!visible())
        return;
    // the pointer in table-local coordinates
    const albor::Point local{.x = event.position.x - bounds().x, .y = event.position.y - bounds().y};
    // whether the pointer sits inside the table
    const bool inside = local.x >= 0.0f && local.x <= bounds().width && local.y >= 0.0f && local.y <= bounds().height;
    // a wheel turn scrolls the body under the pointer
    if (event.kind == albor::EventKind::Wheel && inside) {
        // the framework converts a notch into the distance the theme states
        const float step = albor::wheel_scale(event, scroll_line_extent());
        // a positive delta moves the content toward its start
        if (scroll_to(m_offset - event.scroll * step))
            event.handled = true;
        // the wheel is settled
        return;
    }
    // the thumb and the track decide what a press does
    const albor::Rect thumb = scrollbar_thumb();
    // the track is where the thumb travels
    const albor::Rect track = scrollbar_track();
    // a press inside the table claims the pointer, which is what moves the focus onto the table
    if (event.kind == albor::EventKind::PointerDown && event.button == albor::PointerButton::Left) {
        // a press outside the table belongs to the host
        if (!inside)
            return;
        // whether the pointer landed on the bar
        const bool on_bar = !track.empty() && local.x >= track.x && local.x <= track.x + track.width;
        // a press on the thumb grabs it where the pointer landed
        if (on_bar && !thumb.empty() && local.y >= thumb.y && local.y <= thumb.y + thumb.height) {
            // the drag owns the pointer until it ends or is canceled
            m_dragging = true;
            // keep the thumb under the pointer
            m_drag_grab = local.y - thumb.y;
        }
        // a press on the track pages toward the pointer
        else if (on_bar && local.y >= track.y && local.y <= track.y + track.height) {
            // a press below the thumb pages forward and one above pages back
            const bool forward = local.y > thumb.y + thumb.height;
            // page by most of the visible span
            scroll_to(m_offset + (forward ? 1.0f : -1.0f) * body_height() * 0.9f);
        }
        // the table owns the press, which takes the focus and reads the paging keys
        event.handled = true;
        // the press is settled
        return;
    }
    // a dragged thumb follows the pointer along its track
    if (event.kind == albor::EventKind::PointerMove && m_dragging && !track.empty()) {
        // the distance the thumb travels along the track
        const float travel = std::max(1.0f, track.height - thumb.height);
        // the thumb position under the pointer
        const float position = std::clamp(local.y - m_drag_grab - track.y, 0.0f, travel);
        // the offset the thumb position names
        scroll_to(position / travel * maximum_offset());
        // the move is settled
        event.handled = true;
        return;
    }
    // a released or canceled drag leaves the bar
    if (event.kind == albor::EventKind::PointerUp || event.kind == albor::EventKind::PointerCancel) {
        // the drag ends with the pointer
        m_dragging = false;
        // the release is settled
        return;
    }
    // the paging keys move the body while the table holds the focus
    if (event.kind == albor::EventKind::KeyDown && focused() && event.modifiers.none()) {
        // whether the key moved the body
        bool moved = false;
        // the key names the move
        switch (event.key) {
        case albor::Key::PageDown:
            moved = scroll_to(m_offset + body_height() * 0.9f);
            break;
        case albor::Key::PageUp:
            moved = scroll_to(m_offset - body_height() * 0.9f);
            break;
        case albor::Key::Home:
            moved = scroll_to(0.0f);
            break;
        case albor::Key::End:
            moved = scroll_to(maximum_offset());
            break;
        default:
            break;
        }
        // a key the table consumed is settled
        if (moved)
            event.handled = true;
    }
}

albor::SemanticNode studio::FourierResultsView::semantic_node() const {
    // the table is a group of signal and step blocks
    albor::SemanticNode node{albor::SemanticRole::Group};
    // carry the author id so the automation can select the table
    node.set_explicit_id(identity().explicit_id);
    // name the dataset the table presents
    node.set_name(m_title.empty() ? "Fourier results" : "Fourier results: " + m_title);
    // place the announcement where the table was arranged
    node.set_bounds(bounds());
    // the paging actions scroll the body like the keys do
    (void)node.add_action("scroll-page-down", [view = const_cast<FourierResultsView*>(this)] { view->scroll_to(view->offset() + view->body_height() * 0.9f); });
    // the paging actions scroll the body like the keys do
    (void)node.add_action("scroll-page-up", [view = const_cast<FourierResultsView*>(this)] { view->scroll_to(view->offset() - view->body_height() * 0.9f); });
    // the start action returns to the first block
    (void)node.add_action("scroll-to-start", [view = const_cast<FourierResultsView*>(this)] { view->scroll_to(0.0f); });
    // the end action reaches the last block
    (void)node.add_action("scroll-to-end", [view = const_cast<FourierResultsView*>(this)] { view->scroll_to(view->maximum_offset()); });
    // hand the node to the adapter
    return node;
}

std::size_t studio::FourierResultsView::semantic_child_count() const { return m_tables.size(); }

std::unique_ptr<albor::SemanticProvider> studio::FourierResultsView::semantic_child(const std::size_t index) const {
    // a block past the end names no child
    if (index >= m_tables.size())
        return nullptr;
    // the block this child describes
    const fourier::SignalTable& table = m_tables[index];
    // the block is announced as a group carrying its THD
    albor::SemanticNode node{albor::SemanticRole::Group};
    // the author id keeps every block addressable by automation
    const std::string id = "fourier_block_" + std::to_string(index);
    // the block identity travels with the node
    node.set_explicit_id(id);
    // the block heading names the signal, the step and the THD
    node.set_name(block_heading(table));
    // the block starts under the scrolled offset
    const float top = body_top() - m_offset + block_top(index);
    // the announced span stops at the body, because a scrolled away row is not on screen
    const albor::Rect body = albor::Rect::from_origin_size(albor::Point{.x = bounds().x, .y = bounds().y + body_top()}, albor::Size{.width = bounds().width, .height = body_height()});
    // the announced extent covers the band and the rows
    node.set_bounds(albor::Rect::from_origin_size(albor::Point{.x = bounds().x, .y = bounds().y + top}, albor::Size{.width = bounds().width, .height = block_height(table)}).intersect(body));
    // the announced extent of the settings strip
    const albor::Rect settings = albor::Rect::from_origin_size(albor::Point{.x = bounds().x, .y = bounds().y + top + BLOCK_TITLE_HEIGHT}, albor::Size{.width = bounds().width, .height = BLOCK_SETTINGS_HEIGHT}).intersect(body);
    // the announced extent of every harmonic row
    std::vector<albor::Rect> rows;
    // reserve one extent per row
    rows.reserve(table.rows.size());
    // every row sits under the band and the caption row
    for (std::size_t harmonic = 0; harmonic < table.rows.size(); ++harmonic) {
        // the row top inside the block
        const float row_top = top + BLOCK_HEIGHT + COLUMN_HEIGHT + ROW_HEIGHT * static_cast<float>(harmonic);
        // the row spans the table width and stops at the body
        rows.push_back(albor::Rect::from_origin_size(albor::Point{.x = bounds().x, .y = bounds().y + row_top}, albor::Size{.width = bounds().width, .height = ROW_HEIGHT}).intersect(body));
    }
    // hand the block, its settings strip and its rows to the walk
    return std::make_unique<BlockProvider>(&table, std::move(node), std::move(id), settings, std::move(rows));
}

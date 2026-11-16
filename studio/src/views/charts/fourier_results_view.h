#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "albor/render/glyph_outline.h"
#include "albor/render/resource_factory.h"
#include "albor/widgets/widget.h"
#include "charts/chart_palette.h"
#include "charts/fourier_results.h"

namespace albor
{
    class TextEngine;
} // namespace albor

namespace studio
{
    // the read-only table that presents a Fourier analysis output: one block per signal and step with its THD and rows
    class FourierResultsView final : public albor::Widget
    {
    public:
        // build the table over the window engine and the text engine its labels shape with
        FourierResultsView(albor::BindingEngine* engine, albor::TextEngine* text);

        ~FourierResultsView() override;

        FourierResultsView(const FourierResultsView&) = delete;

        FourierResultsView& operator=(const FourierResultsView&) = delete;

        // replace the dataset title the header band shows
        void set_title(std::string title);

        // replace the blocks the table lists, which resets the scroll to the top
        void set_tables(std::vector<fourier::SignalTable> tables);

        // replace the palette the table paints with
        void set_palette(const ChartPalette& palette);

        // install the callback the destructor runs before the widget leaves the tree
        void set_on_destroyed(std::function<void()> handler);

        // hand out the offset the viewport is scrolled to, in content pixels
        [[nodiscard]] float offset() const;

        // hand out how far the content can scroll, zero while it fits
        [[nodiscard]] float maximum_offset() const;

        // scroll to an offset, clamped into the content range, reporting whether it moved
        bool scroll_to(float offset);

        // hand out how many blocks the table lists
        [[nodiscard]] std::size_t block_count() const;

        // hand out how many table pictures have been recorded, which the cache tests inspect
        [[nodiscard]] std::size_t record_count() const;

        // override: fill the offered bounds so the host page gives the table the whole area
        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override;

        // override: paint the header band, the blocks and the scrollbar
        void paint_content(albor::RenderContext& context) override;

        // override: keep the scroll inside the content the new extent allows
        void arrange(const albor::Rect& bounds) override;

        // override: scroll the table with the wheel, the paging keys and the bar
        void dispatch_event(albor::Event& event) override;

        // override: announce the table and how it is scrolled
        [[nodiscard]] albor::SemanticNode semantic_node() const override;

        // override: expose one block per signal and step
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: build the block at an index, which carries its harmonic rows
        [[nodiscard]] std::unique_ptr<albor::SemanticProvider> semantic_child(std::size_t index) const override;

    private:
        // the retained picture of the table and the state key it was recorded for
        struct Layer
        {
            // the state key the picture was recorded for
            std::uint64_t key = 0;

            // the table-local picture
            std::shared_ptr<const albor::PictureResource> picture;
        };

        // the column edges the header and every row share
        struct Columns
        {
            // the left edge of the harmonic index column
            float left = 0.0f;

            // the right edge of the harmonic index column
            float harmonic_right = 0.0f;

            // the right edge of the frequency column
            float frequency_right = 0.0f;

            // the right edge of the magnitude column
            float magnitude_right = 0.0f;

            // the right edge of the phase column
            float phase_right = 0.0f;

            // the right edge of the normalized magnitude column
            float norm_magnitude_right = 0.0f;

            // the right edge of the normalized phase column, which is the last edge
            float right = 0.0f;
        };

        // the vertical span the body scrolls inside
        [[nodiscard]] float body_top() const;

        [[nodiscard]] float body_height() const;

        // hand out the measured height of one block, its headers and its rows
        [[nodiscard]] float block_height(const fourier::SignalTable& table) const;

        // hand out the width the installed theme draws the bar at, falling back to the framework default
        [[nodiscard]] float scrollbar_extent() const;

        // hand out the distance one wheel notch travels under the installed theme
        [[nodiscard]] float scroll_line_extent() const;

        // hand out the smallest thumb the installed theme draws
        [[nodiscard]] float scrollbar_thumb_minimum() const;

        // hand out the height of every block stacked, without the trailing gap
        [[nodiscard]] float content_height() const;

        // hand out the top of one block in content pixels
        [[nodiscard]] float block_top(std::size_t index) const;

        // hand out the column edges one arranged width names
        [[nodiscard]] Columns columns(float width) const;

        // hand out the track the scrollbar thumb travels in, empty while the content fits
        [[nodiscard]] albor::Rect scrollbar_track() const;

        // hand out the thumb of the current offset, empty while the content fits
        [[nodiscard]] albor::Rect scrollbar_thumb() const;

        // clamp the scroll into the range the current extent allows
        void clamp_offset();

        // hand out the shaped run of one body label, shaping it only on the first use
        [[nodiscard]] const albor::TextLayout& body_layout(const std::string& text);

        // hand out the shaped run of one numeric cell, which a monospaced face lines up by column
        [[nodiscard]] const albor::TextLayout& cell_layout(const std::string& text);

        // hand out the shaped run of the dataset title
        [[nodiscard]] const albor::TextLayout& title_layout();

        // draw one run, expanding an embedded face into glyph outlines
        void draw_text(albor::RenderContext& context, const albor::TextLayout& layout, albor::Point origin, const albor::Paint& paint);

        // draw one body label right aligned at a column edge, clipped to the column it belongs to
        void draw_right(albor::RenderContext& context, const std::string& text, float left, float right, float centre_y, albor::Color color);

        // draw one numeric cell right aligned at a column edge, clipped to the column it belongs to
        void draw_cell(albor::RenderContext& context, const std::string& text, float left, float right, float centre_y, albor::Color color);

        // draw one body label left aligned at an edge, clipped to the span it belongs to
        void draw_left(albor::RenderContext& context, const std::string& text, float left, float right, float centre_y, albor::Color color);

        // compute the state key of the table picture
        [[nodiscard]] std::uint64_t layer_key(float width, float height) const;

        // record the table into a picture, in table-local coordinates
        [[nodiscard]] std::shared_ptr<const albor::PictureResource> record_layer(float width, float height);

        // paint the header, the blocks and the scrollbar, in table-local coordinates
        void paint_layer(albor::RenderContext& context, float width, float height);

        // the resolver that turns an embedded glyph into an outline
        albor::GlyphOutlineResolver m_outline_resolver;

        // the outlines already resolved
        albor::GlyphOutlineCache m_outline_cache;

        // the text engine the labels shape with
        albor::TextEngine* m_text;

        // the shaped runs of the body labels, keyed by their text
        std::unordered_map<std::string, albor::TextLayout> m_labels;

        // the shaped runs of the numeric cells, keyed by their text
        std::unordered_map<std::string, albor::TextLayout> m_cell_labels;

        // the shaped run of the dataset title, which is the only label at its size
        albor::TextLayout m_title_layout;

        // whether the title run was shaped for the current title
        bool m_title_shaped = false;

        // the factory that stamps the retained picture
        albor::ResourceFactory m_resources;

        // the retained table picture
        Layer m_layer;

        // how many table pictures have been recorded, which the cache tests inspect
        std::size_t m_record_count = 0;

        // the palette the table paints with
        ChartPalette m_palette = CHART_PALETTE_LIGHT;

        // the dataset title the header band shows
        std::string m_title;

        // the blocks the table lists
        std::vector<fourier::SignalTable> m_tables;

        // the scroll offset, in content pixels
        float m_offset = 0.0f;

        // whether the pointer drags the scrollbar thumb
        bool m_dragging = false;

        // the grab offset inside the thumb, which keeps it under the pointer
        float m_drag_grab = 0.0f;

        // the destructor callback, which drops the presenter's borrowed pointer
        std::function<void()> m_on_destroyed;
    };
} // namespace studio

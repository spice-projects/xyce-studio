#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "albor/render/glyph_outline.h"
#include "albor/render/resource_factory.h"
#include "albor/widgets/widget.h"
#include "charts/chart_palette.h"

namespace albor
{
    class BindingEngine;
    class TextEngine;
} // namespace albor

namespace studio
{
    // one document tab the strip paints
    struct DocumentTabSpec
    {
        // the label the tab shows
        std::string title;

        // whether the tab carries a close affordance
        bool closable = false;
    };

    // the body tab strip, which selects the netlist or one open output dataset
    class DocumentTabs final : public albor::Widget
    {
    public:
        // build the strip over the window engine and the text engine its labels shape with
        DocumentTabs(albor::BindingEngine* engine, albor::TextEngine* text);

        DocumentTabs(const DocumentTabs&) = delete;

        DocumentTabs& operator=(const DocumentTabs&) = delete;

        // replace the documents the strip lists
        void set_documents(std::vector<DocumentTabSpec> documents);

        // select one document, clamping an index past the list
        void set_active(int index);

        // replace the palette the strip paints with
        void set_palette(const ChartPalette& palette);

        // install the callbacks a tab press reaches
        void set_on_selected(std::function<void(int)> handler);

        void set_on_closed(std::function<void(int)> handler);

        // override: the strip asks for its fixed height and the offered width
        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override;

        // override: assign the strip bounds and lay the tabs out at once
        void arrange(const albor::Rect& bounds) override;

        // override: paint the strip and its tabs
        void paint_content(albor::RenderContext& context) override;

        // override: select or close the tab under a press
        void dispatch_event(albor::Event& event) override;

        // override: announce the strip as a tab view
        [[nodiscard]] albor::SemanticNode semantic_node() const override;

        // override: the focus ring frames the active tab, not the whole strip
        [[nodiscard]] albor::Rect focus_ring_rect() const override;

        // hand out the tab rectangle of one document, in strip coordinates
        [[nodiscard]] albor::Rect tab_bounds(std::size_t index) const;

        // hand out the close rectangle of one document, empty when the tab carries none
        [[nodiscard]] albor::Rect close_bounds(std::size_t index) const;

        // hand out how many strip pictures have been recorded, which the cache tests inspect
        [[nodiscard]] std::size_t strip_record_count() const;

        // hand out the active document index
        [[nodiscard]] std::size_t active_index() const;

    private:
        // lay the tabs out at the arranged width
        void refresh_bounds();

        // compute the state key of the strip picture
        [[nodiscard]] std::uint64_t strip_key(float width, float height) const;

        // record the strip into a picture, in strip-local coordinates
        [[nodiscard]] std::shared_ptr<const albor::PictureResource> record_strip(float width, float height);

        // paint the strip into the recorder, in strip-local coordinates
        void paint_strip(albor::RenderContext& context, float width, float height);

        // hand out the shaped run for one label, shaping it only on the first use
        [[nodiscard]] const albor::TextLayout& label_layout(const std::string& text);

        // draw one run, expanding an embedded face into glyph outlines
        void draw_text(albor::RenderContext& context, const albor::TextLayout& layout, albor::Point origin, const albor::Paint& paint);

        // the resolver that turns an embedded glyph into an outline
        albor::GlyphOutlineResolver m_outline_resolver;

        // the outlines already resolved
        albor::GlyphOutlineCache m_outline_cache;

        // the text engine the labels shape with
        albor::TextEngine* m_text;

        // the shaped runs of the tab titles
        std::unordered_map<std::string, albor::TextLayout> m_labels;

        // the documents the strip lists
        std::vector<DocumentTabSpec> m_documents;

        // the active document index
        std::size_t m_active = 0;

        // the tab rectangles in strip coordinates
        std::vector<albor::Rect> m_tab_bounds;

        // the close rectangles, empty for a non closable tab
        std::vector<albor::Rect> m_close_bounds;

        // the retained strip picture and the key it was recorded for
        struct Layer
        {
            // the state key the picture was recorded for
            std::uint64_t key = 0;

            // the strip-local picture
            std::shared_ptr<const albor::PictureResource> picture;
        };

        // the retained strip picture
        Layer m_layer;

        // the factory that stamps the retained picture
        albor::ResourceFactory m_resources;

        // how many strip pictures have been recorded, which the cache tests inspect
        std::size_t m_record_count = 0;

        // the palette the strip paints with
        ChartPalette m_palette = CHART_PALETTE_LIGHT;

        // the tab selection callback
        std::function<void(int)> m_on_selected;

        // the tab close callback
        std::function<void(int)> m_on_closed;
    };
} // namespace studio

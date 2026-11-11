#pragma once

#include <memory>
#include <span>
#include <vector>

#include "albor/render/glyph_outline.h"
#include "albor/render/resource_factory.h"
#include "charts/chart_layout.h"
#include "charts/chart_scene.h"
#include "charts/chart_text_measurer.h"

namespace studio
{
    // translates chart frames into the retained pictures a charts view replays
    class ChartSceneBuilder final
    {
    public:
        // borrow the factory and measurer the pictures are recorded with
        ChartSceneBuilder(albor::ResourceFactory& resources, AlborChartTextMeasurer& text);

        ChartSceneBuilder(const ChartSceneBuilder&) = delete;

        ChartSceneBuilder& operator=(const ChartSceneBuilder&) = delete;

        // record one base picture for a stack of chart frames and the dataset tab strip
        [[nodiscard]] std::shared_ptr<const ChartScene> build(std::span<const ChartFrame> frames, float width, float height, const ChartPalette& palette, std::span<const ChartTabSpec> tabs, float tab_bar_height) const;

        // record the zoom selection overlay for one rectangle
        [[nodiscard]] std::shared_ptr<const ChartOverlay> build_zoom_overlay(const albor::Rect& selection) const;

    private:
        // record one text run, expanding an embedded face into glyph outlines
        void draw_text(albor::PictureRecorder& recorder, const albor::TextLayout& layout, albor::Point origin, const albor::Paint& paint) const;

        // record the dataset tab strip and hand out the hit-test rectangles
        [[nodiscard]] std::vector<ChartTab> draw_tabs(albor::PictureRecorder& recorder, std::span<const ChartTabSpec> tabs, float width, float height, const ChartPalette& palette) const;

        // record the shared legend block of one chart
        void draw_legend(albor::PictureRecorder& recorder, const ChartFrame& frame, const ChartPalette& palette, float offset_y) const;

        // the factory that stamps every retained path and line batch
        albor::ResourceFactory* m_resources;

        // the measurer that shapes and caches every label
        AlborChartTextMeasurer* m_text;

        // the resolver that turns embedded glyphs into outlines
        albor::GlyphOutlineResolver m_outline_resolver;

        // the outline cache, which keeps a label's glyphs from being rebuilt
        mutable albor::GlyphOutlineCache m_outline_cache;
    };
} // namespace studio

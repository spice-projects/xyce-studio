#pragma once

#include <string>
#include <unordered_map>

#include "albor/render/text_layout.h"
#include "albor/text/text_engine.h"

namespace studio
{
    // measures chart labels through the installed text engine and caches their shaped runs
    class AlborChartTextMeasurer final
    {
    public:
        // borrow the engine the charts shape their labels with
        explicit AlborChartTextMeasurer(albor::TextEngine& engine);

        AlborChartTextMeasurer(const AlborChartTextMeasurer&) = delete;

        AlborChartTextMeasurer& operator=(const AlborChartTextMeasurer&) = delete;

        // measure one label, shaping it only when the cache misses
        [[nodiscard]] float measure(const std::string& text);

        // hand out the shaped run for one label, which the scene builder draws
        [[nodiscard]] const albor::TextLayout& layout(const std::string& text);

        // hand out the resolver that turns an embedded run's glyphs into outlines
        [[nodiscard]] albor::GlyphOutlineResolver glyph_outline_resolver() const;

        // drop every cached run after a font, scale or theme change
        void clear();

    private:
        // the engine the labels are shaped with
        albor::TextEngine* m_engine;

        // the shaped runs keyed by label text at the chart font size
        std::unordered_map<std::string, albor::TextLayout> m_cache;
    };
} // namespace studio

#include "charts/chart_text_measurer.h"

#include "charts/chart_style.h"

studio::AlborChartTextMeasurer::AlborChartTextMeasurer(albor::TextEngine& engine) :
    m_engine(&engine) {}

float studio::AlborChartTextMeasurer::measure(const std::string& text) {
    // shape the label once and reuse its cached width
    return layout(text).size.width;
}

const albor::TextLayout& studio::AlborChartTextMeasurer::layout(const std::string& text) {
    // reuse the run a previous measurement shaped
    if (const auto found = m_cache.find(text); found != m_cache.end())
        return found->second;
    // shape the label at the shared chart font size
    const albor::TextLayout shaped = m_engine->measure(text, albor::TextEngine::UNBOUNDED_WIDTH, CHART_LABEL_FONT_SIZE);
    // keep the shaped run so drawing and the next measure share it
    return m_cache.emplace(text, shaped).first->second;
}

albor::GlyphOutlineResolver studio::AlborChartTextMeasurer::glyph_outline_resolver() const { return m_engine->glyph_outline_resolver(); }

void studio::AlborChartTextMeasurer::clear() {
    // release every shaped run, which a font or scale change invalidates
    m_cache.clear();
}

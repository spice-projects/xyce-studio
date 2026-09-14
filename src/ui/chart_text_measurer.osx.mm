#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>

#include <core/SkData.h>
#include <core/SkFont.h>
#include <core/SkFontMetrics.h>
#include <core/SkFontMgr.h>
#include <core/SkFontTypes.h>
#include <core/SkTypeface.h>
#include <ports/SkFontMgr_mac_ct.h>

#include "chart_text_measurer.h"
#include "charts/chart_style.h"
#include "font_data.h"

std::function<float(const std::string&)> make_chart_text_measurer() {
    // embedded font data wrapped as skia data; the array keeps it alive
    static const std::array<sk_sp<SkData>, 1> font_data = {SkData::MakeWithCopy(Inter_Regular_ttf, sizeof(Inter_Regular_ttf))};
    // core text font manager able to build typefaces from font data
    static const sk_sp<SkFontMgr> font_mgr = SkFontMgr_New_CoreText(nullptr);
    // typeface from the embedded inter regular font data
    static const sk_sp<SkTypeface> typeface = font_mgr->makeFromData(font_data[0]);
    // chart text size in logical px, matching the implot font setup
    static const SkFont font(typeface, CHART_TEXT_HEIGHT);
    // imgui (stb truetype) scales glyph advances by 1/(hhea ascent+descent)
    // instead of 1/units-per-em, and ImGui::CalcTextSize rounds the result up
    // to a whole pixel; reproduce both so the chart layout matches the implot
    // render path exactly
    static const SkFontMetrics metrics = [] {
        // font metrics at the chart text size
        SkFontMetrics value;
        (void)font.getMetrics(&value);
        return value;
    }();
    // stb pixel height (ascent + descent) at the chart text size
    const float advance_scale = CHART_TEXT_HEIGHT / (-metrics.fAscent + metrics.fDescent);
    // measure the advance width of one single-line text; measurements are
    // memoized because the layout rebuilds every frame and core text shaping
    // is expensive enough to stall resize-driven redraws
    auto cache = std::make_shared<std::unordered_map<std::string, float>>();
    return [advance_scale, cache](const std::string& text) {
        // cached measurement when the label was measured before
        const auto it = cache->find(text);
        if (it != cache->end())
            return it->second;
        // continuous advance width in logical px
        const SkScalar width = font.measureText(text.data(), text.size(), SkTextEncoding::kUTF8);
        // scale to the stb advance and round up like ImGui::CalcTextSize
        const float scaled = static_cast<float>(std::floor(width * advance_scale + 0.99999f));
        cache->emplace(text, scaled);
        return scaled;
    };
}

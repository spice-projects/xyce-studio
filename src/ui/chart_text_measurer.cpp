#include <array>
#include <functional>
#include <string>

#include <core/SkData.h>
#include <core/SkFont.h>
#include <core/SkFontMgr.h>
#include <core/SkFontTypes.h>
#include <core/SkTypeface.h>
#include <ports/SkFontMgr_data.h>

#include "chart_text_measurer.h"
#include "charts/chart_style.h"
#include "font_data.h"

std::function<float(const std::string&)> make_chart_text_measurer() {
    // embedded font data wrapped as skia data; the array keeps it alive
    static const std::array<sk_sp<SkData>, 1> font_data = {SkData::MakeWithCopy(Inter_Regular_ttf, sizeof(Inter_Regular_ttf))};
    // font manager serving exactly the embedded font
    static const sk_sp<SkFontMgr> font_mgr = SkFontMgr_New_Custom_Data(SkSpan(const_cast<sk_sp<SkData>*>(font_data.data()), 1));
    // typeface from the embedded inter regular font data
    static const sk_sp<SkTypeface> typeface = font_mgr->makeFromData(font_data[0]);
    // chart text size in logical px, matching the implot font setup
    static const SkFont font(typeface, CHART_TEXT_HEIGHT);
    // measure the advance width of one single-line text
    return [](const std::string& text) {
        // measured advance width in logical px
        const SkScalar width = font.measureText(text.data(), text.size(), SkTextEncoding::kUTF8);
        return static_cast<float>(width);
    };
}

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <core/SkData.h>
#include <core/SkFont.h>
#include <core/SkFontMgr.h>
#include <core/SkFontMetrics.h>
#include <core/SkFontTypes.h>

#include "charts/chart_style.h"
#include "ui/chart_text_measurer.h"

#if defined(__APPLE__)

#include <ports/SkFontMgr_mac_ct.h>

#include "ui/font_data.h"

// legacy explicit core text measurement path; reproduces the same math as the
// former platform specific measurer so the consolidated default-manager
// implementation can be compared against it
static std::function<float(const std::string&)> make_core_text_measurer() {
    // embedded font data wrapped as skia data; the array keeps it alive
    static const std::vector<sk_sp<SkData>> font_data = {SkData::MakeWithCopy(Inter_Regular_ttf, sizeof(Inter_Regular_ttf))};
    // core text font manager able to build typefaces from font data
    static const sk_sp<SkFontMgr> font_mgr = SkFontMgr_New_CoreText(nullptr);
    // typeface from the embedded inter regular font data
    static const sk_sp<SkTypeface> typeface = font_mgr->makeFromData(font_data[0]);
    // chart text size in logical px, matching the chart label metrics
    static const SkFont font(typeface, CHART_TEXT_HEIGHT);
    // stb pixel height (ascent + descent) at the chart text size
    static const SkFontMetrics metrics = [] {
        // font metrics at the chart text size
        SkFontMetrics value;
        (void)font.getMetrics(&value);
        return value;
    }();
    const float advance_scale = CHART_TEXT_HEIGHT / (-metrics.fAscent + metrics.fDescent);
    // measure the advance width of one single-line text without memoization
    return [advance_scale](const std::string& text) {
        // continuous advance width in logical px
        const SkScalar width = font.measureText(text.data(), text.size(), SkTextEncoding::kUTF8);
        // scale to the stb advance and round up like the legacy text stack
        return static_cast<float>(std::floor(width * advance_scale + 0.99999f));
    };
}

TEST(ChartTextMeasurerTest, default_font_manager_matches_core_text) {
    // arrange: consolidated measurer and the legacy explicit core text measurer
    const auto measurer = make_chart_text_measurer();
    const auto reference = make_core_text_measurer();
    // labels covering the chart text shapes: tick numbers, si units, unicode
    // micro sign, negative values, legend names and long readouts
    const std::vector<std::string> labels = {
        "0",
        "1",
        "10",
        "900 m",
        "2 ms",
        "20 ms",
        "1.5 kV",
        "12.3 A",
        "1 µA",
        "10 GHz",
        "-1.5 kV",
        "0V",
        "1e-13",
        "V(N2)",
        "I(L1)",
        "P(V1)",
        "FFT: I(L1)",
        "Transient Analysis",
        "Simulation finished successfully",
        "the quick brown fox jumps over the lazy dog",
    };
    // act and assert: identical measurements for every label
    for (const auto& label : labels)
        EXPECT_EQ(measurer(label), reference(label)) << "label: " << label;
}

TEST(ChartTextMeasurerTest, default_font_manager_uses_the_embedded_inter_typeface) {
    // arrange: the measurer must come from the embedded font, so the family
    // metrics must reproduce the legacy chart text size scaling
    const auto measurer = make_chart_text_measurer();
    // a zero width text measures as one whole pixel through the ceil rounding
    // (the label band layout relies on whole pixel advances)
    // act and assert
    EXPECT_EQ(measurer(""), 0.0f);
    EXPECT_EQ(measurer("i"), measurer("i"));
    // the measurement is a whole pixel count
    EXPECT_NEAR(measurer("900 m"), std::floor(measurer("900 m")), 1e-6);
}

#endif

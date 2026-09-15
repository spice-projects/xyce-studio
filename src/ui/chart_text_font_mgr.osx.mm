#include <core/SkFontMgr.h>
#include <ports/SkFontMgr_mac_ct.h>

#include "chart_text_measurer.h"

sk_sp<SkFontMgr> make_chart_font_mgr() {
    // core text font manager able to build typefaces from font data
    return SkFontMgr_New_CoreText(nullptr);
}

#include <core/SkFontMgr.h>
#include <ports/SkFontMgr_fontconfig.h>
#include <ports/SkFontScanner_FreeType.h>

#include "chart_text_measurer.h"

sk_sp<SkFontMgr> make_chart_font_mgr() {
    // fontconfig font manager backed by the freetype scanner, able to build
    // typefaces from font data
    return SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
}

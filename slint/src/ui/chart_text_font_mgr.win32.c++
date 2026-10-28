#include <core/SkFontMgr.h>
#include <ports/SkTypeface_win.h>

#include "chart_text_measurer.h"

sk_sp<SkFontMgr> make_chart_font_mgr() {
    // directwrite font manager able to build typefaces from font data
    return SkFontMgr_New_DirectWrite();
}

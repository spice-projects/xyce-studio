#pragma once

#include <functional>
#include <string>

#include <core/SkRefCnt.h>

class SkFontMgr;

// text width measured with the embedded inter font at the chart text size,
// in logical px; injected into ChartLayout so chart layout math stays ui free
std::function<float(const std::string&)> make_chart_text_measurer();

// platform font manager used to build the chart typeface from the embedded
// font data; one implementation for every platform
sk_sp<SkFontMgr> make_chart_font_mgr();

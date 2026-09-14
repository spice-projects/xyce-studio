#pragma once

#include <functional>
#include <string>

// text width measured with the embedded inter font at the chart text size,
// in logical px; injected into ChartLayout so chart layout math stays ui free
std::function<float(const std::string&)> make_chart_text_measurer();

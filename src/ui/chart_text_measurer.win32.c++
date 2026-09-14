#include <functional>
#include <string>

#include "chart_text_measurer.h"

std::function<float(const std::string&)> make_chart_text_measurer() {
    // platform port pending: approximate the label width from the character
    // count until the windows font stack is wired (skia directwrite)
    return [](const std::string& text) {
        // seven logical px per character approximates the inter font advance
        return static_cast<float>(text.size()) * 7.0f;
    };
}

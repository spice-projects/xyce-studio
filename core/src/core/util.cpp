#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <cstdlib>
#endif

#include "util.h"

#ifdef _WIN32
// note: _dupenv_s is the CRT-recommended, secure replacement for std::getenv
std::optional<std::string> get_environment_variable(const std::string& name) {
    // output value and size
    char* value = nullptr;
    size_t size = 0;
    // query the variable, allocating a CRT-owned buffer for the value
    const errno_t err = _dupenv_s(&value, &size, name.c_str());
    if (err != 0)
        return std::nullopt;
    // the variable is not set when no buffer is returned
    if (value == nullptr)
        return std::nullopt;
    // copy the value (size includes the null terminator) and release the buffer
    std::string result{value, size - 1};
    std::free(value);
    // exit
    return result;
}
#else
std::optional<std::string> get_environment_variable(const std::string& name) {
    // the variable is not set when no value is returned
    const char* value = std::getenv(name.c_str());
    if (value == nullptr)
        return std::nullopt;
    // return a copy so the result does not depend on later getenv calls
    return std::string{value};
}
#endif

std::string trim(std::string_view view) {
    // result
    std::string result{view};
    // erase leading whitespace
    result.erase(result.begin(), std::ranges::find_if(result, [](const unsigned char ch) { return !std::isspace(ch); }));
    // erase trailing whitespace
    result.erase(std::find_if(result.rbegin(), result.rend(), [](const unsigned char ch) { return !std::isspace(ch); }).base(), result.end());
    // return converted string
    return result;
}

std::string to_upper(std::string_view view) {
    // result
    std::string result{view};
    // convert each character to upper case
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::toupper(c); });
    // return converted string
    return result;
}

std::string to_lower(std::string_view view) {
    // result
    std::string result{view};
    // convert each character to lower case
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    // return converted string
    return result;
}

std::vector<std::string_view> tokenize(std::string_view view) {
    // output
    std::vector<std::string_view> tokens;
    // reserve space for a few tokens to avoid reallocations
    tokens.reserve(8);
    // pointers to the current position and end of the string
    const char* p = view.data();
    const char* end = p + view.size();
    // iterate through the string
    while (p != end) {
        // skip leading whitespace
        while (p != end && std::isspace(static_cast<unsigned char>(*p)))
            ++p;
        // mark the start of the token
        const char* first = p;
        // find the end of the token
        while (p != end && !std::isspace(static_cast<unsigned char>(*p)))
            ++p;
        // add the token to the list if it's not empty
        if (first != p)
            tokens.emplace_back(first, static_cast<size_t>(p - first));
    }
    return tokens;
}

std::vector<std::string> tokenize_owned(std::string_view text) {
    // output token list
    std::vector<std::string> tokens;
    // borrow views into the source buffer
    const auto views = tokenize(text);
    // reserve space for all tokens up front
    tokens.reserve(views.size());
    // copy each borrowed view into an owning string
    for (const auto& view : views)
        tokens.emplace_back(view);
    // return the owned tokens
    return tokens;
}

std::vector<std::string_view> split_by(std::string_view view, char delimiter) {
    // init result list
    std::vector<std::string_view> parts;
    // begin and end pointers for the string view
    const char* begin = view.data();
    const char* end = begin + view.size();
    // iterate through the string view and split by the delimiter
    while (begin < end) {
        // skip delimeter
        while (begin < end && *begin == delimiter)
            ++begin;
        // exit if we reached the end
        if (begin == end)
            break;
        // current position pointer
        const char* pos = begin;
        // find the next occurrence of the delimiter
        while (pos != end && *pos != delimiter)
            ++pos;
        // add non-empty token
        parts.emplace_back(begin, static_cast<std::size_t>(pos - begin));
        // move the begin pointer
        begin = pos;
    }
    return parts;
}

std::string strip_chars(std::string_view view, std::string_view chars) {
    // result
    std::string result;
    // reserve space for the result to avoid reallocations
    result.reserve(view.size());
    // loop characters in view
    for (char ch : view) {
        // append char if not in chars
        if (chars.find(ch) == std::string_view::npos)
            result += ch;
    }
    return result;
}

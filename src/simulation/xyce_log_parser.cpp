#include "xyce_log_parser.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>

// marker Xyce opens its completion percentage with
constexpr std::string_view PERCENT_PREFIX = "***** Percent complete:";
// marker Xyce opens its estimated time to completion with
constexpr std::string_view ETA_PREFIX = "***** Estimated time to completion:";
// marker Xyce opens every analysis phase announcement with
constexpr std::string_view PHASE_PREFIX = "***** Beginning ";
// marker Xyce opens the closing summary of an aborted run with
constexpr std::string_view ABORT_SUMMARY_PREFIX = "Simulation aborted due to error.";
// the banner closing an aborted run
constexpr std::string_view ABORT_BANNER = "*** Xyce Abort ***";
// text separating the fatal and the error count inside the abort summary
constexpr std::string_view FATAL_COUNT_MARKER = "There are ";
// text in front of the error count inside the abort summary
constexpr std::string_view ERROR_COUNT_MARKER = "errors and ";
// prefixes Xyce puts in front of a reported message; the message body follows the prefix inline, or on the
// next line when the message carries a netlist location
constexpr std::array<std::pair<std::string_view, LogSeverity>, 4> MESSAGE_PREFIXES = {{{"Netlist error", LogSeverity::error}, {"Netlist warning", LogSeverity::warning}, {"Application error", LogSeverity::error}, {"Application warning", LogSeverity::warning}}};

// trim the leading and trailing whitespace of a log line
[[nodiscard]] std::string_view trimmed(std::string_view line) {
    const auto first = line.find_first_not_of(" \t\r\n");
    // the line holds nothing but whitespace
    if (first == std::string_view::npos)
        return {};
    const auto last = line.find_last_not_of(" \t\r\n");
    return line.substr(first, last - first + 1);
}

// true when the line starts with the given prefix
[[nodiscard]] bool starts_with(std::string_view line, std::string_view prefix) { return line.size() >= prefix.size() && line.compare(0, prefix.size(), prefix) == 0; }

// drop the dot runs Xyce closes its analysis phase announcements with
[[nodiscard]] std::string_view without_trailing_dots(std::string_view value) {
    while (!value.empty() && value.back() == '.')
        value.remove_suffix(1);
    return value;
}

// classify a reported message by its prefix, returns nullopt for a line carrying no report prefix
[[nodiscard]] std::optional<LogSeverity> message_severity(std::string_view line) {
    for (const auto& [prefix, severity] : MESSAGE_PREFIXES) {
        if (starts_with(line, prefix))
            return severity;
    }
    return std::nullopt;
}

// parse the number at the front of a value, returns nullopt when there is none
[[nodiscard]] std::optional<double> leading_number(std::string_view value) {
    const std::string text(trimmed(value));
    char* end = nullptr;
    const double number = std::strtod(text.c_str(), &end);
    // strtod reports a parse failure by consuming no character at all
    if (end == text.c_str())
        return std::nullopt;
    return number;
}

// parse the integer behind a marker, returns nullopt when the marker or the number is absent
[[nodiscard]] std::optional<int> count_after(std::string_view line, std::string_view marker) {
    const auto position = line.find(marker);
    // the marker is not part of the line
    if (position == std::string_view::npos)
        return std::nullopt;
    int count = 0;
    const auto* const begin = line.data() + position + marker.size();
    const auto result = std::from_chars(begin, line.data() + line.size(), count);
    // the marker is not followed by a number
    if (result.ec != std::errc{})
        return std::nullopt;
    return count;
}

// count a reported message and remember the log row of the first occurrence
void record_message(XyceLogState& state, LogSeverity severity, std::size_t index) {
    // a warning is counted on its own row
    if (severity == LogSeverity::warning) {
        ++state.warnings;
        // remember the row of the first warning so the output panel can scroll to it
        if (!state.first_warning_line.has_value())
            state.first_warning_line = index;
        return;
    }
    ++state.errors;
    // remember the row of the first error so the output panel can scroll to it
    if (!state.first_error_line.has_value())
        state.first_error_line = index;
}

LogSeverity XyceLogParser::feed(std::string_view line, std::size_t index) {
    // the progress markers and the report prefixes are indented inside the message blocks
    const std::string_view text = trimmed(line);
    // a reported message carries its severity in its prefix
    if (const auto severity = message_severity(text); severity.has_value()) {
        record_message(m_state, *severity, index);
        return *severity;
    }
    // a transient run reports the completion percentage of the whole run, the analysis phase and the log
    // line it was already reporting are kept, only the percentage moves forward
    if (starts_with(text, PERCENT_PREFIX)) {
        if (const auto percentage = leading_number(text.substr(PERCENT_PREFIX.size())); percentage.has_value())
            m_state.percentage = std::clamp(*percentage, 0.0, 100.0);
        return LogSeverity::info;
    }
    // the estimated time to completion belongs to the percentage reported right before it
    if (starts_with(text, ETA_PREFIX)) {
        m_state.eta = std::string(trimmed(text.substr(ETA_PREFIX.size())));
        return LogSeverity::info;
    }
    // the analysis phase is announced before the first percentage of a run reaches it
    if (starts_with(text, PHASE_PREFIX)) {
        m_state.phase = std::string(without_trailing_dots(text.substr(PHASE_PREFIX.size())));
        return LogSeverity::info;
    }
    // the closing summary of an aborted run holds the authoritative error counts, it repeats the counted
    // messages and its trailing "errors" word wraps onto the next line
    if (starts_with(text, ABORT_SUMMARY_PREFIX)) {
        const auto fatal = count_after(text, FATAL_COUNT_MARKER);
        const auto errors = count_after(text, ERROR_COUNT_MARKER);
        // replace the running count only when the summary raises it and both counts were readable
        if (fatal.has_value() && errors.has_value() && *fatal + *errors > m_state.errors) {
            m_state.errors = *fatal + *errors;
            // remember the row of the abort when no message was counted before it
            if (!m_state.first_error_line.has_value())
                m_state.first_error_line = index;
        }
        return LogSeverity::error;
    }
    // the banner closing an aborted run repeats the summary, so it only counts when nothing was counted yet
    if (text == ABORT_BANNER) {
        if (m_state.errors == 0) {
            ++m_state.errors;
            m_state.first_error_line = index;
        }
        return LogSeverity::error;
    }
    // everything else is progress or informational output
    return LogSeverity::info;
}

const XyceLogState& XyceLogParser::state() const {
    // return the distilled run information
    return m_state;
}

void XyceLogParser::reset() {
    // drop the run information of the previous run
    m_state = XyceLogState{};
}

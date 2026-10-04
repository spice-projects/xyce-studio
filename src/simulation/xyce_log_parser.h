#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// severity of a single Xyce console log line
enum class LogSeverity
{
    info,
    warning,
    error
};

// run information distilled from the Xyce console log of a simulation
struct XyceLogState
{
    // completion percentage Xyce reported, empty for the analyses it reports no progress for (a DC sweep, AC, harmonic balance and operating point analyses print no progress at all)
    std::optional<double> percentage;
    // estimated time to completion exactly as Xyce spelled it, e.g. "14 sec." or "1 min., 53 sec."
    std::string eta;
    // analysis phase Xyce announced last, e.g. "Transient Calculation"
    std::string phase;
    // number of reported warnings and errors so far
    int warnings = 0;
    int errors = 0;
    // row of the first reported warning and error in the log, so the output panel can scroll to it
    std::optional<std::size_t> first_warning_line;
    std::optional<std::size_t> first_error_line;
};

// parser for the Xyce console log
//
// a transient run reports its progress as a block of markers every time the completion percentage moves by
// more than a percent: "***** Percent complete: 88.0017 %", "***** Current system time: ..." and "*****
// Estimated time to completion: 14 sec.".  every message Xyce reports carries a severity in its prefix
// ("Netlist error: ...", "Application warning in file ... at or near line 4"), and an aborted run closes
// with a summary line holding the authoritative error counts.  Xyce prints no percentage for a DC sweep,
// AC, harmonic balance or operating point analysis, so the percentage stays empty for those runs
class XyceLogParser
{
public:
    // consume one log line and return its severity; the index is the row the line was appended at, it is
    // recorded for the first warning and error so the caller can scroll the output panel to them
    LogSeverity feed(std::string_view line, std::size_t index);

    // the run information distilled so far
    [[nodiscard]] const XyceLogState& state() const;

    // drop everything distilled so far, called when a new run starts
    void reset();

private:
    XyceLogState m_state;
};

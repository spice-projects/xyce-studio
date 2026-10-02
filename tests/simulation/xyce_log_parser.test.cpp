#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "simulation/xyce_log_parser.h"

// ========================================================================================
// XyceLogParser
// ========================================================================================

TEST(XyceLogParserChecks, feed_reports_the_completion_percentage) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("***** Percent complete: 32.0064 %", 40);
    // assert
    EXPECT_EQ(severity, LogSeverity::info);
    ASSERT_TRUE(parser.state().percentage.has_value());
    EXPECT_DOUBLE_EQ(*parser.state().percentage, 32.0064);
}

TEST(XyceLogParserChecks, feed_clips_the_completion_percentage) {
    // arrange
    XyceLogParser parser;
    // act
    parser.feed("***** Percent complete: 140 %", 0);
    // assert
    ASSERT_TRUE(parser.state().percentage.has_value());
    EXPECT_DOUBLE_EQ(*parser.state().percentage, 100.0);
}

TEST(XyceLogParserChecks, feed_keeps_the_last_reported_completion_percentage) {
    // arrange
    XyceLogParser parser;
    parser.feed("***** Percent complete: 10 %", 0);
    // act
    parser.feed("***** Percent complete: 88.0017 %", 1);
    // assert
    ASSERT_TRUE(parser.state().percentage.has_value());
    EXPECT_DOUBLE_EQ(*parser.state().percentage, 88.0017);
}

TEST(XyceLogParserChecks, feed_ignores_a_completion_percentage_without_a_number) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("***** Percent complete: n/a %", 0);
    // assert
    EXPECT_EQ(severity, LogSeverity::info);
    EXPECT_FALSE(parser.state().percentage.has_value());
}

TEST(XyceLogParserChecks, feed_reports_the_estimated_time_to_completion) {
    // arrange
    XyceLogParser parser;
    // act
    parser.feed("***** Estimated time to completion:  1 min., 53 sec.", 12);
    // assert
    EXPECT_EQ(parser.state().eta, "1 min., 53 sec.");
}

TEST(XyceLogParserChecks, feed_reports_the_analysis_phase) {
    // arrange
    XyceLogParser parser;
    // act
    parser.feed("***** Beginning DC Operating Point Calculation...", 20);
    // act
    parser.feed("***** Beginning Transient Calculation...", 21);
    // assert
    EXPECT_EQ(parser.state().phase, "Transient Calculation");
}

TEST(XyceLogParserChecks, feed_reports_the_analysis_phase_without_a_body) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("***** Beginning ", 0);
    // assert
    EXPECT_EQ(severity, LogSeverity::info);
    EXPECT_EQ(parser.state().phase, "");
}

TEST(XyceLogParserChecks, feed_reports_an_analysis_phase_holding_only_the_closing_dots) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("***** Beginning ....", 0);
    // assert
    EXPECT_EQ(severity, LogSeverity::info);
    EXPECT_EQ(parser.state().phase, "");
}

TEST(XyceLogParserChecks, feed_ignores_the_system_time_marker) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("***** Current system time: Fri Oct  2 14:46:05 2026", 41);
    // assert
    EXPECT_EQ(severity, LogSeverity::info);
    EXPECT_FALSE(parser.state().percentage.has_value());
    EXPECT_EQ(parser.state().warnings, 0);
    EXPECT_EQ(parser.state().errors, 0);
}

TEST(XyceLogParserChecks, feed_counts_a_netlist_warning) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("Netlist warning: No print specified", 11);
    // assert
    EXPECT_EQ(severity, LogSeverity::warning);
    EXPECT_EQ(parser.state().warnings, 1);
    ASSERT_TRUE(parser.state().first_warning_line.has_value());
    EXPECT_EQ(*parser.state().first_warning_line, 11U);
}

TEST(XyceLogParserChecks, feed_counts_a_located_netlist_warning) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("Netlist warning in file /tmp/xyce_1_2_3.cir at or near line 4", 12);
    // assert
    EXPECT_EQ(severity, LogSeverity::warning);
    EXPECT_EQ(parser.state().warnings, 1);
    ASSERT_TRUE(parser.state().first_warning_line.has_value());
    EXPECT_EQ(*parser.state().first_warning_line, 12U);
}

TEST(XyceLogParserChecks, feed_counts_an_application_warning) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("Application warning in file /tmp/xyce.cir at or near line 9", 3);
    // assert
    EXPECT_EQ(severity, LogSeverity::warning);
    EXPECT_EQ(parser.state().warnings, 1);
}

TEST(XyceLogParserChecks, feed_keeps_the_first_warning_row_only) {
    // arrange
    XyceLogParser parser;
    parser.feed("Netlist warning: No print specified", 11);
    // act
    parser.feed("Netlist warning: Voltage Node (N1) does not have a DC path to ground", 12);
    // assert
    EXPECT_EQ(parser.state().warnings, 2);
    ASSERT_TRUE(parser.state().first_warning_line.has_value());
    EXPECT_EQ(*parser.state().first_warning_line, 11U);
}

TEST(XyceLogParserChecks, feed_counts_a_netlist_error) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("Netlist error: There was 1 undefined symbol in .PRINT command: node 1", 11);
    // assert
    EXPECT_EQ(severity, LogSeverity::error);
    EXPECT_EQ(parser.state().errors, 1);
    ASSERT_TRUE(parser.state().first_error_line.has_value());
    EXPECT_EQ(*parser.state().first_error_line, 11U);
}

TEST(XyceLogParserChecks, feed_counts_an_application_error) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("Application error: unable to open the output file", 3);
    // assert
    EXPECT_EQ(severity, LogSeverity::error);
    EXPECT_EQ(parser.state().errors, 1);
}

TEST(XyceLogParserChecks, feed_keeps_the_first_error_row_only) {
    // arrange
    XyceLogParser parser;
    parser.feed("Netlist error: There was 1 undefined symbol in .PRINT command: node 1", 11);
    // act
    parser.feed("Netlist error in file /tmp/xyce_1_2_3.cir at or near line 3", 12);
    // assert
    EXPECT_EQ(parser.state().errors, 2);
    ASSERT_TRUE(parser.state().first_error_line.has_value());
    EXPECT_EQ(*parser.state().first_error_line, 11U);
}

TEST(XyceLogParserChecks, feed_reports_the_error_count_of_the_abort_summary) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("Simulation aborted due to error.  There are 2 MSG_FATAL errors and 3 MSG_ERROR", 30);
    // assert
    EXPECT_EQ(severity, LogSeverity::error);
    EXPECT_EQ(parser.state().errors, 5);
    ASSERT_TRUE(parser.state().first_error_line.has_value());
    EXPECT_EQ(*parser.state().first_error_line, 30U);
}

TEST(XyceLogParserChecks, feed_keeps_the_higher_error_count_of_the_abort_summary) {
    // arrange
    XyceLogParser parser;
    parser.feed("Netlist error: There was 1 undefined symbol in .PRINT command: node 1", 11);
    // act
    parser.feed("Simulation aborted due to error.  There are 1 MSG_FATAL errors and 1 MSG_ERROR", 30);
    // assert
    EXPECT_EQ(parser.state().errors, 2);
    ASSERT_TRUE(parser.state().first_error_line.has_value());
    EXPECT_EQ(*parser.state().first_error_line, 11U);
}

TEST(XyceLogParserChecks, feed_keeps_the_error_count_above_the_abort_summary) {
    // arrange
    XyceLogParser parser;
    parser.feed("Netlist error in file /tmp/xyce.cir at or near line 3", 11);
    parser.feed("Netlist error in file /tmp/xyce.cir at or near line 3", 12);
    parser.feed("Netlist error in file /tmp/xyce.cir at or near line 3", 13);
    // act — the summary counts fewer errors than the run already reported
    parser.feed("Simulation aborted due to error.  There are 0 MSG_FATAL errors and 1 MSG_ERROR", 30);
    // assert
    EXPECT_EQ(parser.state().errors, 3);
    ASSERT_TRUE(parser.state().first_error_line.has_value());
    EXPECT_EQ(*parser.state().first_error_line, 11U);
}

TEST(XyceLogParserChecks, feed_ignores_an_abort_summary_without_counts) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("Simulation aborted due to error.", 30);
    // assert
    EXPECT_EQ(severity, LogSeverity::error);
    EXPECT_EQ(parser.state().errors, 0);
}

TEST(XyceLogParserChecks, feed_ignores_an_abort_summary_with_an_unreadable_error_count) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("Simulation aborted due to error.  There are 0 MSG_FATAL errors and many MSG_ERROR", 30);
    // assert
    EXPECT_EQ(severity, LogSeverity::error);
    EXPECT_EQ(parser.state().errors, 0);
}

TEST(XyceLogParserChecks, feed_counts_the_abort_banner_without_a_summary) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("*** Xyce Abort ***", 31);
    // assert
    EXPECT_EQ(severity, LogSeverity::error);
    EXPECT_EQ(parser.state().errors, 1);
    ASSERT_TRUE(parser.state().first_error_line.has_value());
    EXPECT_EQ(*parser.state().first_error_line, 31U);
}

TEST(XyceLogParserChecks, feed_ignores_the_abort_banner_after_a_counted_error) {
    // arrange
    XyceLogParser parser;
    parser.feed("Netlist error: There was 1 undefined symbol in .PRINT command: node 1", 11);
    // act
    const LogSeverity severity = parser.feed("*** Xyce Abort ***", 31);
    // assert
    EXPECT_EQ(severity, LogSeverity::error);
    EXPECT_EQ(parser.state().errors, 1);
    ASSERT_TRUE(parser.state().first_error_line.has_value());
    EXPECT_EQ(*parser.state().first_error_line, 11U);
}

TEST(XyceLogParserChecks, feed_reports_an_empty_line_as_informational) {
    // arrange
    XyceLogParser parser;
    // act
    const LogSeverity severity = parser.feed("   \t", 0);
    // assert
    EXPECT_EQ(severity, LogSeverity::info);
    EXPECT_EQ(parser.state().warnings, 0);
    EXPECT_EQ(parser.state().errors, 0);
}

TEST(XyceLogParserChecks, feed_trims_the_indentation_of_a_progress_marker) {
    // arrange
    XyceLogParser parser;
    // act
    parser.feed("  ***** Percent complete: 55.5 %\r", 7);
    // assert
    ASSERT_TRUE(parser.state().percentage.has_value());
    EXPECT_DOUBLE_EQ(*parser.state().percentage, 55.5);
}

TEST(XyceLogParserChecks, reset_drops_the_distilled_run_information) {
    // arrange
    XyceLogParser parser;
    parser.feed("***** Percent complete: 88.0017 %", 40);
    parser.feed("***** Estimated time to completion: 14 sec.", 41);
    parser.feed("***** Beginning Transient Calculation...", 21);
    parser.feed("Netlist warning: No print specified", 11);
    parser.feed("Netlist error: There was 1 undefined symbol in .PRINT command: node 1", 12);
    // act
    parser.reset();
    // assert
    EXPECT_FALSE(parser.state().percentage.has_value());
    EXPECT_EQ(parser.state().eta, "");
    EXPECT_EQ(parser.state().phase, "");
    EXPECT_EQ(parser.state().warnings, 0);
    EXPECT_EQ(parser.state().errors, 0);
    EXPECT_FALSE(parser.state().first_warning_line.has_value());
    EXPECT_FALSE(parser.state().first_error_line.has_value());
}

TEST(XyceLogParserChecks, feed_reads_a_complete_transient_run_log) {
    // arrange
    XyceLogParser parser;
    // arrange: the console log of a transient run that reported two progress blocks and one warning
    const std::vector<std::string> log = {
        "",
        "***** Welcome to the Xyce(TM) Parallel Electronic Simulator",
        "***** Reading and parsing netlist...",
        "Netlist warning: No print specified",
        "***** Setting up topology...",
        "***** Initializing...",
        "***** Beginning DC Operating Point Calculation...",
        "***** Beginning Transient Calculation...",
        "",
        "***** Percent complete: 1.00002 %",
        "***** Current system time: Fri Oct  2 14:44:18 2026",
        "***** Estimated time to completion:  1 min., 53 sec.",
        "",
        "***** Percent complete: 97.0018 %",
        "***** Current system time: Fri Oct  2 14:46:16 2026",
        "***** Estimated time to completion: 3 sec.",
    };
    // act
    LogSeverity last_severity = LogSeverity::info;
    for (std::size_t row = 0; row < log.size(); ++row)
        last_severity = parser.feed(log[row], row);
    // assert
    EXPECT_EQ(last_severity, LogSeverity::info);
    ASSERT_TRUE(parser.state().percentage.has_value());
    EXPECT_DOUBLE_EQ(*parser.state().percentage, 97.0018);
    EXPECT_EQ(parser.state().eta, "3 sec.");
    EXPECT_EQ(parser.state().phase, "Transient Calculation");
    EXPECT_EQ(parser.state().warnings, 1);
    EXPECT_EQ(parser.state().errors, 0);
    ASSERT_TRUE(parser.state().first_warning_line.has_value());
    EXPECT_EQ(*parser.state().first_warning_line, 3U);
}
#include <gtest/gtest.h>

#include "core/util.h"
#include "netlist/netlist.h"
#include "simulation/print_parameters.h"

// ========================================================================================
// from_xyce_statement
// ========================================================================================

TEST(PrintParametersChecks, non_print_statement_returns_none) {
    // arrange
    const std::string statement = ".TRAN 1u 1m";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(PrintParametersChecks, minimal_print_statement) {
    // arrange
    const std::string statement = ".PRINT TRAN";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_type, "TRAN");
    ASSERT_EQ(result->print_format, "");
    ASSERT_EQ(result->print_file, "");
    ASSERT_EQ(result->output_variables.size(), 0);
    ASSERT_EQ(result->extra_options.size(), 0);
}

TEST(PrintParametersChecks, parses_format_file_and_variables) {
    // arrange
    const std::string statement = ".print tran FORMAT=RAW FILE=waves.raw V(OUT) I(V1)";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_type, "TRAN");
    ASSERT_EQ(result->print_format, "RAW");
    ASSERT_EQ(result->print_file, "waves.raw");
    ASSERT_EQ(result->output_variables.size(), 2);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
    ASSERT_EQ(result->output_variables[1], "I(V1)");
}

TEST(PrintParametersChecks, parses_quoted_file_without_quotes) {
    // arrange: a quoted filename carrying spaces must surface unquoted
    const std::string statement = R"(.PRINT TRAN FORMAT=RAW FILE="file with space.raw" V(OUT))";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_file, "file with space.raw");
    ASSERT_EQ(result->output_variables.size(), 1);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
}

TEST(PrintParametersChecks, to_xyce_statement_quotes_file_with_spaces) {
    // arrange: a filename entered in the dialog without quotes
    const PrintParameters params("TRAN", "RAW", "file with space.raw", {"V(OUT)"}, {});
    // act
    const std::string statement = params.to_xyce_statement();
    // assert: the emitted directive quotes the filename so it survives tokenization
    ASSERT_EQ(statement, R"(.PRINT TRAN FORMAT=RAW FILE="file with space.raw" V(OUT))");
}

TEST(PrintParametersChecks, to_xyce_statement_does_not_quote_simple_file) {
    // arrange
    const PrintParameters params("TRAN", "RAW", "waves.raw", {"V(OUT)"}, {});
    // act
    const std::string statement = params.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, ".PRINT TRAN FORMAT=RAW FILE=waves.raw V(OUT)");
}

TEST(PrintParametersChecks, to_xyce_statement_normalizes_quoted_file) {
    // arrange: a value carrying outer quotes is normalized — the model always holds the bare filename, so the emitted directive is unquoted here
    const PrintParameters params("TRAN", "RAW", R"("waves.raw")", {"V(OUT)"}, {});
    // act
    const std::string statement = params.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, ".PRINT TRAN FORMAT=RAW FILE=waves.raw V(OUT)");
}

TEST(PrintParametersChecks, to_xyce_statement_normalizes_quoted_file_with_spaces) {
    // arrange: a quoted spaced value is stripped and re-quoted once
    const PrintParameters params("TRAN", "RAW", R"("file with space.raw")", {"V(OUT)"}, {});
    // act
    const std::string statement = params.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, R"(.PRINT TRAN FORMAT=RAW FILE="file with space.raw" V(OUT))");
}

TEST(PrintParametersChecks, quoted_file_round_trip_normalizes_quotes) {
    // arrange: a netlist-authored quoted filename round-trips through parse and emit
    const std::string statement = R"(.PRINT TRAN FORMAT=RAW FILE="file with space.raw" V(OUT))";
    // act
    const auto parsed = PrintParameters::from_xyce_statement(statement);
    const std::string rebuilt = parsed->to_xyce_statement();
    const auto reparsed = PrintParameters::from_xyce_statement(rebuilt);
    // assert: the filename stays unquoted in the model and quoted in the netlist
    ASSERT_EQ(reparsed->print_file, "file with space.raw");
    ASSERT_EQ(rebuilt, statement);
}

TEST(PrintParametersChecks, parses_expression_with_spaces) {
    // arrange
    const std::string statement = ".PRINT TRAN FORMAT=RAW V(OUT) {V(OUT) * I(V1)}";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_variables.size(), 2);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
    ASSERT_EQ(result->output_variables[1], "{V(OUT) * I(V1)}");
}

TEST(PrintParametersChecks, parses_generic_options) {
    // arrange
    const std::string statement = ".PRINT TRAN WIDTH=20 PRECISION=12 V(OUT)";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->extra_options.size(), 2);
    ASSERT_EQ(result->extra_options[0], "WIDTH=20");
    ASSERT_EQ(result->extra_options[1], "PRECISION=12");
    ASSERT_EQ(result->output_variables.size(), 1);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
}

TEST(PrintParametersChecks, ignores_invalid_format_value) {
    // arrange
    const std::string statement = ".PRINT TRAN FORMAT=INVALID V(OUT)";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_format, "");
    ASSERT_EQ(result->output_variables.size(), 1);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
}

TEST(PrintParametersChecks, ignores_sample_options_for_non_sample_print_type) {
    // arrange
    const std::string statement = ".PRINT TRAN OUTPUT_SAMPLE_STATS=TRUE OUTPUT_ALL_SAMPLES=TRUE V(OUT)";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->extra_options.size(), 0);
    ASSERT_EQ(result->output_variables.size(), 1);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
}

TEST(PrintParametersChecks, accepts_sample_options_for_es_print_type) {
    // arrange
    const std::string statement = ".PRINT ES OUTPUT_SAMPLE_STATS=TRUE OUTPUT_ALL_SAMPLES=FALSE V(OUT)";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->extra_options.size(), 2);
    ASSERT_EQ(result->extra_options[0], "OUTPUT_SAMPLE_STATS=TRUE");
    ASSERT_EQ(result->extra_options[1], "OUTPUT_ALL_SAMPLES=FALSE");
    ASSERT_EQ(result->output_variables.size(), 1);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
}

TEST(PrintParametersChecks, ignores_invalid_format_value_with_warning) {
    // arrange
    const std::string statement = ".PRINT TRAN FORMAT=INVALID V(OUT)";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_format, "");
    ASSERT_EQ(result->output_variables.size(), 1);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
}

TEST(PrintParametersChecks, round_trip) {
    // arrange
    const PrintParameters params("TRAN", "RAW", "waves.raw", {"V(OUT)", "ID(M1)", "{V(OUT)*I(V1)}"}, {"WIDTH=20"});
    // act
    const std::string statement = params.to_xyce_statement();
    const auto reparsed = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->print_type, "TRAN");
    ASSERT_EQ(reparsed->print_format, "RAW");
    ASSERT_EQ(reparsed->print_file, "waves.raw");
    ASSERT_EQ(reparsed->output_variables.size(), 3);
    ASSERT_EQ(reparsed->output_variables[0], "V(OUT)");
    ASSERT_EQ(reparsed->output_variables[1], "ID(M1)");
    ASSERT_EQ(reparsed->output_variables[2], "{V(OUT)*I(V1)}");
    ASSERT_EQ(reparsed->extra_options.size(), 1);
    ASSERT_EQ(reparsed->extra_options[0], "WIDTH=20");
}

TEST(PrintParametersChecks, serializes_statement_with_all_fields) {
    // arrange
    const PrintParameters params("TRAN", "RAW", "waves.raw", {"V(OUT)", "ID(M1)", "{V(OUT)*I(V1)}"}, {"WIDTH=20", "PRECISION=12"});
    // act
    const std::string statement = params.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, ".PRINT TRAN FORMAT=RAW FILE=waves.raw WIDTH=20 PRECISION=12 V(OUT) ID(M1) {V(OUT)*I(V1)}");
}

TEST(PrintParametersChecks, serializes_statement_with_minimal_fields) {
    // arrange
    const PrintParameters params("TRAN", "", "", {}, {});
    // act
    const std::string statement = params.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, ".PRINT TRAN");
}

TEST(PrintParametersChecks, parses_expression_with_complex_parentheses) {
    // arrange
    const std::string statement = ".PRINT TRAN {V(OUT) * I(V1)} {V(IN) + V(OUT)}";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_variables.size(), 2);
    ASSERT_EQ(result->output_variables[0], "{V(OUT) * I(V1)}");
    ASSERT_EQ(result->output_variables[1], "{V(IN) + V(OUT)}");
}

TEST(PrintParametersChecks, parses_multiple_extra_options) {
    // arrange
    const std::string statement = ".PRINT TRAN WIDTH=20 PRECISION=12 HEADINGS=1 V(OUT)";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->extra_options.size(), 3);
    ASSERT_EQ(result->extra_options[0], "WIDTH=20");
    ASSERT_EQ(result->extra_options[1], "PRECISION=12");
    ASSERT_EQ(result->extra_options[2], "HEADINGS=1");
    ASSERT_EQ(result->output_variables.size(), 1);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
}

TEST(PrintParametersChecks, parses_format_lowercase) {
    // arrange
    const std::string statement = ".print tran FORMAT=raw FILE=waves.raw V(OUT)";
    // act
    const auto result = PrintParameters::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_type, "TRAN");
    ASSERT_EQ(result->print_format, "raw");
    ASSERT_EQ(result->print_file, "waves.raw");
    ASSERT_EQ(result->output_variables.size(), 1);
    ASSERT_EQ(result->output_variables[0], "V(OUT)");
}

// ========================================================================================
// to_xyce_statement
// ========================================================================================

TEST(PrintParametersChecks, serializes_statement) {
    // arrange
    const PrintParameters params("TRAN", "RAW", "waves.raw", {"V(OUT)", "ID(M1)", "{V(OUT)*I(V1)}"}, {"WIDTH=20"});
    // act
    const std::string statement = params.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, ".PRINT TRAN FORMAT=RAW FILE=waves.raw WIDTH=20 V(OUT) ID(M1) {V(OUT)*I(V1)}");
}

TEST(PrintParametersChecks, serializes_minimal_statement) {
    // arrange
    const PrintParameters params("TRAN", "", "", {}, {});
    // act
    const std::string statement = params.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, ".PRINT TRAN");
}

TEST(PrintParametersChecks, serializes_with_all_fields) {
    // arrange
    const PrintParameters params("DC", "CSV", "output.csv", {"V(*)", "I(*)"}, {"WIDTH=20", "PRECISION=12"});
    // act
    const std::string statement = params.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, ".PRINT DC FORMAT=CSV FILE=output.csv WIDTH=20 PRECISION=12 V(*) I(*)");
}

TEST(PrintParametersChecks, passes_through_V_star_with_topology) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\nR2 2 0 200\n.END\n");
    const PrintParameters params("DC", "", "", {"V(*)"}, {});
    // act
    const std::string statement = params.to_xyce_statement(&topology);
    // assert V(*) passes through verbatim for native Xyce expansion
    ASSERT_NE(statement.find("V(*)"), std::string::npos);
    // topology nodes are NOT injected by the plugin
    ASSERT_EQ(statement.find("V(0)"), std::string::npos);
    ASSERT_EQ(statement.find("V(1)"), std::string::npos);
    ASSERT_EQ(statement.find("V(2)"), std::string::npos);
}

TEST(PrintParametersChecks, passes_through_I_star_with_topology) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\nC1 2 0 1u\n.END\n");
    const PrintParameters params("DC", "", "", {"I(*)"}, {});
    // act
    const std::string statement = params.to_xyce_statement(&topology);
    // assert I(*) passes through verbatim for native Xyce expansion
    ASSERT_NE(statement.find("I(*)"), std::string::npos);
    // device currents are NOT injected by the plugin
    ASSERT_EQ(statement.find("I(R1)"), std::string::npos);
    ASSERT_EQ(statement.find("I(C1)"), std::string::npos);
}

TEST(PrintParametersChecks, passes_through_P_star_with_topology) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.END\n");
    const PrintParameters params("DC", "", "", {"P(*)"}, {});
    // act
    const std::string statement = params.to_xyce_statement(&topology);
    // assert P(*) passes through verbatim for native Xyce expansion
    ASSERT_NE(statement.find("P(*)"), std::string::npos);
    // device powers are NOT injected by the plugin
    ASSERT_EQ(statement.find("P(R1)"), std::string::npos);
}

TEST(PrintParametersChecks, passes_through_mixed_wildcards_and_explicit_with_topology) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.END\n");
    const PrintParameters params("DC", "", "", {"V(1)", "V(*)", "I(*)"}, {});
    // act
    const std::string statement = params.to_xyce_statement(&topology);
    // assert explicit V(1) preserved
    ASSERT_NE(statement.find("V(1)"), std::string::npos);
    // wildcards pass through verbatim
    ASSERT_NE(statement.find("V(*)"), std::string::npos);
    ASSERT_NE(statement.find("I(*)"), std::string::npos);
}

TEST(PrintParametersChecks, passes_through_wildcards_without_topology) {
    // arrange
    const PrintParameters params("DC", "", "", {"V(*)", "I(*)", "P(*)"}, {});
    // act
    const std::string statement = params.to_xyce_statement(nullptr);
    // assert all wildcards pass through verbatim when no topology is given
    ASSERT_NE(statement.find("V(*)"), std::string::npos);
    ASSERT_NE(statement.find("I(*)"), std::string::npos);
    ASSERT_NE(statement.find("P(*)"), std::string::npos);
}

TEST(PrintParametersChecks, passes_through_non_wildcard_variables_with_topology) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.END\n");
    const PrintParameters params("DC", "", "", {"V(OUT)", "ID(M1)", "{V(OUT)*I(V1)}"}, {});
    // act
    const std::string statement = params.to_xyce_statement(&topology);
    // assert non-wildcard variables pass through unchanged
    ASSERT_NE(statement.find("V(OUT)"), std::string::npos);
    ASSERT_NE(statement.find("ID(M1)"), std::string::npos);
    ASSERT_NE(statement.find("{V(OUT)*I(V1)}"), std::string::npos);
}

TEST(PrintParametersChecks, equality_operator_equal_params) {
    // arrange
    const PrintParameters params1("TRAN", "RAW", "file.raw", {"V(OUT)"}, {"WIDTH=20"});
    const PrintParameters params2("TRAN", "RAW", "file.raw", {"V(OUT)"}, {"WIDTH=20"});
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_TRUE(result);
}

TEST(PrintParametersChecks, equality_operator_different_type) {
    // arrange
    const PrintParameters params1("TRAN", "", "", {}, {});
    const PrintParameters params2("DC", "", "", {}, {});
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(PrintParametersChecks, equality_operator_different_format) {
    // arrange
    const PrintParameters params1("TRAN", "RAW", "", {}, {});
    const PrintParameters params2("TRAN", "CSV", "", {}, {});
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(PrintParametersChecks, equality_operator_different_file) {
    // arrange
    const PrintParameters params1("TRAN", "", "file1.raw", {}, {});
    const PrintParameters params2("TRAN", "", "file2.raw", {}, {});
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(PrintParametersChecks, equality_operator_different_variables) {
    // arrange
    const PrintParameters params1("TRAN", "", "", {"V(OUT)"}, {});
    const PrintParameters params2("TRAN", "", "", {"I(V1)"}, {});
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(PrintParametersChecks, equality_operator_different_extra_options) {
    // arrange
    const PrintParameters params1("TRAN", "", "", {}, {"WIDTH=20"});
    const PrintParameters params2("TRAN", "", "", {}, {"WIDTH=40"});
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(PrintParametersChecks, s_parameter_variable_list_survives_dialog_round_trip_shape) {
    // arrange: the variable list from netlists/lin-simple-01.cir as the dialog's additional-variables field would carry it
    const std::string line_edit_text = "SR(1,1) SI(1,1) SM(1,1) SP(1,1) SDB(1,1) SR(2,1) SI(2,1) SM(2,1) SP(2,1) SDB(2,1) SR(1,2) SI(1,2) SM(1,2) SP(1,2) SDB(1,2) SR(2,2) SI(2,2) SM(2,2) SP(2,2) SDB(2,2)";
    PrintParameters params("AC", "RAW", "lin-simple-01.raw", tokenize_owned(line_edit_text), {});
    // act
    const std::string rebuilt = params.to_xyce_statement();
    // assert: every token survives verbatim and in order after FORMAT/FILE
    const std::string expected = ".PRINT AC FORMAT=RAW FILE=lin-simple-01.raw " + line_edit_text;
    EXPECT_EQ(rebuilt, expected);
}

// ========================================================================================
// strip_print_file_option
// ========================================================================================

TEST(PrintParametersChecks, strip_print_file_removes_file_option_from_raw_statement) {
    // arrange
    const std::string statement = ".PRINT TRAN FORMAT=RAW FILE=waves.raw V(OUT) I(V1)";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, ".PRINT TRAN FORMAT=RAW V(OUT) I(V1)");
}

TEST(PrintParametersChecks, strip_print_file_removes_file_option_from_probe_statement) {
    // arrange — PROBE output is redirected to the netlist-derived .csd file as well
    const std::string statement = ".PRINT TRAN FORMAT=PROBE FILE=waves.csd V(OUT) I(V1)";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, ".PRINT TRAN FORMAT=PROBE V(OUT) I(V1)");
}

TEST(PrintParametersChecks, strip_print_file_removes_probe_file_before_format) {
    // arrange: the FILE option appearing before the FORMAT option must be stripped as well (the format is discovered in a first pass)
    const std::string statement = ".PRINT TRAN FILE=waves.csd FORMAT=PROBE V(OUT)";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, ".PRINT TRAN FORMAT=PROBE V(OUT)");
}

TEST(PrintParametersChecks, strip_print_file_removes_file_option_before_format) {
    // arrange: the FILE option appearing before the FORMAT option must be stripped as well (the format is discovered in a first pass)
    const std::string statement = ".PRINT TRAN FILE=waves.raw FORMAT=RAW V(OUT)";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, ".PRINT TRAN FORMAT=RAW V(OUT)");
}

TEST(PrintParametersChecks, strip_print_file_removes_file_option_without_format) {
    // arrange: FILE= is removed from every .PRINT statement so the run writes the default file for the format
    const std::string statement = ".PRINT TRAN FILE=waves.raw V(OUT)";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, ".PRINT TRAN V(OUT)");
}

TEST(PrintParametersChecks, strip_print_file_removes_file_option_from_csv_statement) {
    // arrange: a CSV print loses its explicit output file so the run writes the default file for the format
    const std::string statement = ".PRINT TRAN FORMAT=CSV FILE=out.csv V(OUT)";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, ".PRINT TRAN FORMAT=CSV V(OUT)");
}

TEST(PrintParametersChecks, strip_print_file_removes_file_option_from_std_statement) {
    // arrange: a STD print loses its explicit output file so the run writes the default file for the format
    const std::string statement = ".PRINT DC FORMAT=STD FILE=dc.prn V(1)";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, ".PRINT DC FORMAT=STD V(1)");
}

TEST(PrintParametersChecks, strip_print_file_keeps_statement_without_file_option) {
    // arrange
    const std::string statement = ".PRINT TRAN FORMAT=RAW V(OUT) {V(OUT) * 2}";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, statement);
}

TEST(PrintParametersChecks, strip_print_file_returns_non_print_statement_unchanged) {
    // arrange
    const std::string statement = ".TRAN 1u 1m";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, statement);
}

TEST(PrintParametersChecks, strip_print_file_keeps_braced_expressions_intact) {
    // arrange: the output variables carry brace-enclosed expressions with spaces
    const std::string statement = ".PRINT TRAN FORMAT=RAW FILE=waves.raw V(OUT) {V(OUT) * I(V1)}";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, ".PRINT TRAN FORMAT=RAW V(OUT) {V(OUT) * I(V1)}");
}

TEST(PrintParametersChecks, strip_print_file_removes_quoted_file_option) {
    // arrange: a quoted filename carrying spaces is a single token and is removed
    const std::string statement = R"(.PRINT TRAN FORMAT=RAW FILE="file with space.raw" V(OUT))";
    // act
    const std::string stripped = strip_print_file_option(statement);
    // assert
    ASSERT_EQ(stripped, ".PRINT TRAN FORMAT=RAW V(OUT)");
}

TEST(PrintParametersChecks, strip_print_file_result_reparses_without_file) {
    // arrange
    const std::string statement = ".print dc FORMAT=RAW FILE=dc.raw V(1)";
    // act
    const std::string stripped = strip_print_file_option(statement);
    const auto reparsed = PrintParameters::from_xyce_statement(stripped);
    // assert: the stripped statement round-trips through the parser without a file
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->print_file, "");
    ASSERT_EQ(reparsed->print_format, "RAW");
    ASSERT_EQ(reparsed->output_variables.size(), 1);
}

TEST(PrintParametersChecks, ac_print_strips_power_and_lead_wildcards) {
    // arrange — an AC print carrying the power and device lead wildcards the AC analysis cannot produce per the Xyce reference guide
    // act
    const auto result = PrintParameters::from_xyce_statement(".PRINT AC V(*) I(*) P(*) W(*) IB(*) IC(*) IE(*) IS(*) ID(*) IG(*)");
    // assert — only the supported wildcards survive
    ASSERT_TRUE(result.has_value());
    const std::vector<std::string> expected = {"V(*)", "I(*)"};
    ASSERT_EQ(result->output_variables, expected);
}

TEST(PrintParametersChecks, noise_print_strips_power_and_lead_wildcards) {
    // arrange — a NOISE print with the unsupported wildcards
    // act
    const auto result = PrintParameters::from_xyce_statement(".PRINT NOISE V(*) P(*) IC(*) IG(*)");
    // assert
    ASSERT_TRUE(result.has_value());
    const std::vector<std::string> expected = {"V(*)"};
    ASSERT_EQ(result->output_variables, expected);
}

TEST(PrintParametersChecks, hb_print_strips_power_and_lead_wildcards) {
    // arrange — HB prints (including the FD/TD variants) share the AC restriction
    // act
    const auto hb = PrintParameters::from_xyce_statement(".PRINT HB V(*) P(*) IE(*)");
    const auto hb_fd = PrintParameters::from_xyce_statement(".PRINT HB_FD V(*) W(*) ID(*)");
    const auto hb_td = PrintParameters::from_xyce_statement(".PRINT HB_TD V(*) IC(*)");
    // assert
    ASSERT_TRUE(hb.has_value());
    ASSERT_EQ(hb->output_variables, std::vector<std::string>{"V(*)"});
    ASSERT_TRUE(hb_fd.has_value());
    ASSERT_EQ(hb_fd->output_variables, std::vector<std::string>{"V(*)"});
    ASSERT_TRUE(hb_td.has_value());
    ASSERT_EQ(hb_td->output_variables, std::vector<std::string>{"V(*)"});
}

TEST(PrintParametersChecks, lowercase_linear_print_type_is_sanitized) {
    // arrange — a lower case print type statement
    // act
    const auto result = PrintParameters::from_xyce_statement(".print ac V(*) P(*)");
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_variables, std::vector<std::string>{"V(*)"});
}

TEST(PrintParametersChecks, ac_print_constructs_are_sanitized_in_place) {
    // arrange — a directly constructed AC print with the unsupported wildcards
    const PrintParameters print("AC", "", "", {"V(*)", "P(*)", "IC(*)", "VDB(V(out))"}, {});
    // assert — the wildcards are stripped, other tokens are preserved in order
    const std::vector<std::string> expected = {"V(*)", "VDB(V(out))"};
    ASSERT_EQ(print.output_variables, expected);
}

TEST(PrintParametersChecks, transient_print_keeps_power_and_lead_wildcards) {
    // arrange — a TRAN print carries the full wildcard set
    // act
    const auto result = PrintParameters::from_xyce_statement(".PRINT TRAN V(*) I(*) P(*) IB(*) ID(*)");
    // assert — transient prints support power and lead currents
    ASSERT_TRUE(result.has_value());
    const std::vector<std::string> expected = {"V(*)", "I(*)", "P(*)", "IB(*)", "ID(*)"};
    ASSERT_EQ(result->output_variables, expected);
}

TEST(PrintParametersChecks, dc_print_keeps_power_and_lead_wildcards) {
    // arrange — a DC print with the full wildcard set
    // act
    const auto result = PrintParameters::from_xyce_statement(".PRINT DC V(*) P(*) W(*) IC(*)");
    // assert — DC prints support power and lead currents; W(*) is a power synonym and parses as P(*)
    ASSERT_TRUE(result.has_value());
    const std::vector<std::string> expected = {"V(*)", "P(*)", "P(*)", "IC(*)"};
    ASSERT_EQ(result->output_variables, expected);
}

TEST(PrintParametersChecks, ac_print_round_trip_drops_unsupported_wildcards) {
    // arrange — a directly constructed AC print with the unsupported wildcards
    const PrintParameters print("AC", "RAW", "", {"V(*)", "P(*)", "IC(*)"}, {});
    // act
    const auto reparsed = PrintParameters::from_xyce_statement(print.to_xyce_statement());
    // assert — the serialized statement never carries the unsupported tokens
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->output_variables, std::vector<std::string>{"V(*)"});
}

// ========================================================================================
// default_print_output_file
// ========================================================================================

TEST(DefaultPrintOutputFileChecks, unspecified_format_resolves_the_fd_prn_for_ac) {
    // arrange — the default format writes the prn table and the AC print type carries the frequency-domain suffix
    const PrintParameters print("AC", "", "ac-simple-01.raw", {"V(*)"}, {});
    // act
    const auto path = default_print_output_file(print, "/tmp/ac-simple-01.cir");
    // assert
    EXPECT_EQ(path.generic_string(), "/tmp/ac-simple-01.cir.FD.prn");
}

TEST(DefaultPrintOutputFileChecks, unspecified_format_resolves_the_prn_for_tran) {
    // arrange — the default format writes the prn table next to the netlist
    const PrintParameters print("TRAN", "", "waves.raw", {"V(1)"}, {});
    // act
    const auto path = default_print_output_file(print, "/tmp/net.cir");
    // assert
    EXPECT_EQ(path.generic_string(), "/tmp/net.cir.prn");
}

TEST(DefaultPrintOutputFileChecks, std_format_resolves_the_prn_default) {
    // arrange
    const PrintParameters print("DC", "STD", "dc.prn", {"V(1)"}, {});
    // act
    const auto path = default_print_output_file(print, "/tmp/net.cir");
    // assert
    EXPECT_EQ(path.generic_string(), "/tmp/net.cir.prn");
}

TEST(DefaultPrintOutputFileChecks, prn_table_formats_resolve_the_prn_default) {
    // arrange — NOINDEX, GNUPLOT and SPLOT write the prn table like STD does
    const PrintParameters noindex("DC", "NOINDEX", "", {"V(1)"}, {});
    const PrintParameters gnuplot("DC", "GNUPLOT", "", {"V(1)"}, {});
    const PrintParameters splot("DC", "SPLOT", "", {"V(1)"}, {});
    // act
    const auto noindex_path = default_print_output_file(noindex, "/tmp/net.cir");
    const auto gnuplot_path = default_print_output_file(gnuplot, "/tmp/net.cir");
    const auto splot_path = default_print_output_file(splot, "/tmp/net.cir");
    // assert
    EXPECT_EQ(noindex_path.generic_string(), "/tmp/net.cir.prn");
    EXPECT_EQ(gnuplot_path.generic_string(), "/tmp/net.cir.prn");
    EXPECT_EQ(splot_path.generic_string(), "/tmp/net.cir.prn");
}

TEST(DefaultPrintOutputFileChecks, raw_format_resolves_the_netlist_raw_file) {
    // arrange — the format reaches the model in its original case and must compare normalized
    const PrintParameters print("TRAN", "raw", "waves.raw", {"V(1)"}, {});
    // act
    const auto path = default_print_output_file(print, "/tmp/net.cir");
    // assert
    EXPECT_EQ(path.generic_string(), "/tmp/net.cir.raw");
}

TEST(DefaultPrintOutputFileChecks, probe_format_resolves_the_netlist_csd_file) {
    // arrange
    const PrintParameters print("TRAN", "PROBE", "waves.csd", {"V(1)"}, {});
    // act
    const auto path = default_print_output_file(print, "/tmp/net.cir");
    // assert
    EXPECT_EQ(path.generic_string(), "/tmp/net.cir.csd");
}

TEST(DefaultPrintOutputFileChecks, probe_format_carries_the_td_suffix_for_ac_ic) {
    // arrange — the AC_IC print type produces time-domain output in a .TD.csd file
    const PrintParameters print("AC_IC", "PROBE", "", {"V(1)"}, {});
    // act
    const auto path = default_print_output_file(print, "/tmp/net.cir");
    // assert
    EXPECT_EQ(path.generic_string(), "/tmp/net.cir.TD.csd");
}

TEST(DefaultPrintOutputFileChecks, csv_format_resolves_the_csv_suffix) {
    // arrange — the AC print type carries the frequency-domain csv suffix
    const PrintParameters print("AC", "CSV", "", {"V(1)"}, {});
    // act
    const auto path = default_print_output_file(print, "/tmp/net.cir");
    // assert
    EXPECT_EQ(path.generic_string(), "/tmp/net.cir.FD.csv");
}

TEST(DefaultPrintOutputFileChecks, tecplot_format_resolves_the_dat_suffix) {
    // arrange
    const PrintParameters print("TRAN", "TECPLOT", "", {"V(1)"}, {});
    // act
    const auto path = default_print_output_file(print, "/tmp/net.cir");
    // assert
    EXPECT_EQ(path.generic_string(), "/tmp/net.cir.dat");
}

// ========================================================================================
// collect_print_file_copies
// ========================================================================================

TEST(CollectPrintFileCopiesChecks, print_with_file_records_the_default_produced_file) {
    // arrange — the reported netlist: an AC print with a FILE= destination and no explicit format
    const std::vector<std::string> directives = {".AC LIN 20 1 100k", ".PRINT AC FILE=ac-simple-01.raw V(*) I(*)"};
    // act
    const auto copies = collect_print_file_copies(directives, "/tmp/work/xyce_0.cir", "/tmp/work");
    // assert
    ASSERT_EQ(copies.size(), 1u);
    EXPECT_EQ(copies[0].produced_file.generic_string(), "/tmp/work/xyce_0.cir.FD.prn");
    EXPECT_EQ(copies[0].destination_file.generic_string(), "/tmp/work/ac-simple-01.raw");
}

TEST(CollectPrintFileCopiesChecks, print_without_file_records_no_copy) {
    // arrange
    const std::vector<std::string> directives = {".PRINT AC V(*) I(*)"};
    // act
    const auto copies = collect_print_file_copies(directives, "/tmp/work/xyce_0.cir", "/tmp/work");
    // assert
    EXPECT_TRUE(copies.empty());
}

TEST(CollectPrintFileCopiesChecks, non_print_directives_record_nothing) {
    // arrange
    const std::vector<std::string> directives = {".AC LIN 20 1 100k", ".PREPROCESS REPLACEGROUND TRUE", ".END"};
    // act
    const auto copies = collect_print_file_copies(directives, "/tmp/work/xyce_0.cir", "/tmp/work");
    // assert
    EXPECT_TRUE(copies.empty());
}

TEST(CollectPrintFileCopiesChecks, every_print_records_its_own_copy) {
    // arrange — the analysis print plus an unassociated print, each with its own FILE=
    const std::vector<std::string> directives = {".TRAN 1u 1m", ".PRINT TRAN FORMAT=RAW FILE=tran.raw V(1)", ".PRINT DC FORMAT=CSV FILE=dc.csv V(1)"};
    // act
    const auto copies = collect_print_file_copies(directives, "/tmp/work/xyce_0.cir", "/tmp/work");
    // assert
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_EQ(copies[0].produced_file.generic_string(), "/tmp/work/xyce_0.cir.raw");
    EXPECT_EQ(copies[0].destination_file.generic_string(), "/tmp/work/tran.raw");
    EXPECT_EQ(copies[1].produced_file.generic_string(), "/tmp/work/xyce_0.cir.csv");
    EXPECT_EQ(copies[1].destination_file.generic_string(), "/tmp/work/dc.csv");
}

TEST(CollectPrintFileCopiesChecks, pce_companion_print_records_its_statistics_file) {
    // arrange — the .PCE companion print without an explicit format writes the PCE prn table
    const std::vector<std::string> directives = {".TRAN 10u 1m", ".PRINT PCE FILE=pce.csv V(IN)"};
    // act
    const auto copies = collect_print_file_copies(directives, "/tmp/work/xyce_0.cir", "/tmp/work");
    // assert
    ASSERT_EQ(copies.size(), 1u);
    EXPECT_EQ(copies[0].produced_file.generic_string(), "/tmp/work/xyce_0.cir.PCE.prn");
    EXPECT_EQ(copies[0].destination_file.generic_string(), "/tmp/work/pce.csv");
}

TEST(CollectPrintFileCopiesChecks, quoted_file_records_the_unquoted_destination) {
    // arrange — a quoted FILE= value carrying whitespace
    const std::vector<std::string> directives = {R"(.PRINT TRAN FILE="file with space.raw" V(1))"};
    // act
    const auto copies = collect_print_file_copies(directives, "/tmp/work/xyce_0.cir", "/tmp/work");
    // assert
    ASSERT_EQ(copies.size(), 1u);
    EXPECT_EQ(copies[0].destination_file.generic_string(), "/tmp/work/file with space.raw");
}

TEST(CollectPrintFileCopiesChecks, absolute_file_stays_absolute) {
    // arrange — an absolute FILE= value
    const std::vector<std::string> directives = {".PRINT TRAN FILE=/var/results/out.raw V(1)"};
    // act
    const auto copies = collect_print_file_copies(directives, "/tmp/work/xyce_0.cir", "/tmp/work");
    // assert
    ASSERT_EQ(copies.size(), 1u);
    EXPECT_EQ(copies[0].destination_file.generic_string(), "/var/results/out.raw");
}

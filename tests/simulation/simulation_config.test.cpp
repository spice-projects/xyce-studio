#include <algorithm>

#include <gtest/gtest.h>

#include "simulation/print_parameters.h"
#include "simulation/simulation_config.h"

TEST(SimulationConfigReplaceGroundChecks, from_xyce_directives_parses_disabled_statement) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({".OP", ".PREPROCESS REPLACEGROUND FALSE"});
    // assert
    EXPECT_FALSE(config.replace_ground);
}

TEST(SimulationConfigReplaceGroundChecks, from_xyce_directives_defaults_to_true_without_statement) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({".OP"});
    // assert
    EXPECT_TRUE(config.replace_ground);
}

TEST(SimulationConfigReplaceGroundChecks, to_xyce_directives_emits_disabled_statement) {
    // arrange
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, false);
    // act
    const auto directives = config.to_xyce_directives(NetlistTopology{});
    // assert
    const auto found = std::find(directives.begin(), directives.end(), ".PREPROCESS REPLACEGROUND FALSE");
    ASSERT_NE(found, directives.end());
}

TEST(SimulationConfigReplaceGroundChecks, disabled_state_round_trips_through_directives) {
    // arrange
    const SimulationConfig input("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, false);
    // act
    const auto directives = input.to_xyce_directives(NetlistTopology{});
    const auto output = SimulationConfig::from_xyce_directives(directives);
    // assert
    EXPECT_FALSE(output.replace_ground);
}

TEST(SimulationConfigReplaceGroundChecks, enabled_state_round_trips_through_directives) {
    // arrange
    const SimulationConfig input("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto directives = input.to_xyce_directives(NetlistTopology{});
    const auto output = SimulationConfig::from_xyce_directives(directives);
    // assert
    EXPECT_TRUE(output.replace_ground);
}

// ========================================================================================
// analysis type detection
// ========================================================================================

TEST(SimulationConfigAnalysisChecks, from_xyce_directives_identifies_transient) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({".TRAN 1u 1m"});
    // assert
    EXPECT_EQ(config.analysis_type, "TRAN");
}

TEST(SimulationConfigAnalysisChecks, from_xyce_directives_identifies_ac) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({".AC LIN 100 1 1MEG"});
    // assert
    EXPECT_EQ(config.analysis_type, "AC");
}

TEST(SimulationConfigAnalysisChecks, from_xyce_directives_identifies_dc) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({".DC VIN 0 5 0.1"});
    // assert
    EXPECT_EQ(config.analysis_type, "DC");
}

TEST(SimulationConfigAnalysisChecks, from_xyce_directives_identifies_op) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({".OP"});
    // assert
    EXPECT_EQ(config.analysis_type, "OP");
}

TEST(SimulationConfigAnalysisChecks, from_xyce_directives_identifies_hb) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({".HB 1MEG"});
    // assert
    EXPECT_EQ(config.analysis_type, "HB");
}

TEST(SimulationConfigAnalysisChecks, from_xyce_directives_identifies_noise) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({".NOISE V(5) V1 LIN 100 1 1MEG"});
    // assert
    EXPECT_EQ(config.analysis_type, "NOISE");
}

TEST(SimulationConfigAnalysisChecks, lin_claims_the_match_before_ac) {
    // arrange — .LIN netlists also contain a .AC directive; the LIN parser
    // embeds the AC sweep so it must win the precedence order
    const std::vector<std::string> directives = {".AC DEC 10 1 1MEG", ".LIN"};
    // act
    const auto config = SimulationConfig::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(config.analysis_type, "LIN");
    ASSERT_TRUE(std::holds_alternative<LinSimulationParameters>(config.analysis));
    // the embedded ac sweep was captured by the LIN parameters
    EXPECT_EQ(std::get<LinSimulationParameters>(config.analysis).sweep_mode, "DEC");
}

TEST(SimulationConfigAnalysisChecks, without_analysis_yields_monostate) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({});
    // assert
    EXPECT_EQ(config.analysis_type, "");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(config.analysis));
}

// ========================================================================================
// unassociated print directives
// ========================================================================================

TEST(SimulationConfigUnassociatedPrintChecks, prints_handled_by_the_analysis_are_not_unassociated) {
    // arrange — a transient analysis handles TRAN prints itself
    const std::vector<std::string> directives = {".TRAN 1u 1m", ".PRINT TRAN V(1)"};
    // act
    const auto config = SimulationConfig::from_xyce_directives(directives);
    // assert
    EXPECT_TRUE(config.unassociated_prints.empty());
}

TEST(SimulationConfigUnassociatedPrintChecks, hb_ic_and_hb_startup_prints_are_handled_by_hb_analysis) {
    // arrange — HB_IC and HB_STARTUP prints belong to a harmonic balance analysis
    const std::vector<std::string> directives = {".HB 1MEG 10", ".PRINT HB_IC V(1)", ".PRINT HB_STARTUP I(V1)"};
    // act
    const auto config = SimulationConfig::from_xyce_directives(directives);
    // assert
    EXPECT_TRUE(config.unassociated_prints.empty());
}

TEST(SimulationConfigUnassociatedPrintChecks, pce_prints_are_handled_by_dc_analysis) {
    // arrange — the DC parser claims .PRINT PCE into its structured PCE parameters
    const std::vector<std::string> directives = {".DC 0 5 0.1", ".PCE param=R1 type=normal means=3K std_deviations=1K", ".PRINT PCE V(1)"};
    // act
    const auto config = SimulationConfig::from_xyce_directives(directives);
    // assert
    EXPECT_TRUE(config.unassociated_prints.empty());
    ASSERT_TRUE(std::holds_alternative<DCSimulationParameters>(config.analysis));
    EXPECT_TRUE(std::get<DCSimulationParameters>(config.analysis).pce.has_value());
}

TEST(SimulationConfigUnassociatedPrintChecks, pce_prints_are_handled_by_tran_analysis) {
    // arrange — the TRAN parser claims .PRINT PCE into its structured PCE parameters
    const std::vector<std::string> directives = {".TRAN 1u 1m", ".PCE param=R1 type=normal means=3K std_deviations=1K", ".PRINT PCE V(1)"};
    // act
    const auto config = SimulationConfig::from_xyce_directives(directives);
    // assert
    EXPECT_TRUE(config.unassociated_prints.empty());
    ASSERT_TRUE(std::holds_alternative<TransientSimulationParameters>(config.analysis));
    EXPECT_TRUE(std::get<TransientSimulationParameters>(config.analysis).pce.has_value());
}

TEST(SimulationConfigUnassociatedPrintChecks, other_print_types_become_unassociated) {
    // arrange — a DC print under a transient analysis is not handled by it
    const std::vector<std::string> directives = {".TRAN 1u 1m", ".PRINT DC V(1)"};
    // act
    const auto config = SimulationConfig::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(config.unassociated_prints.size(), 1);
    EXPECT_EQ(config.unassociated_prints[0].print_type, "DC");
}

TEST(SimulationConfigUnassociatedPrintChecks, prints_without_analysis_are_unassociated) {
    // arrange / act
    const auto config = SimulationConfig::from_xyce_directives({".PRINT TRAN V(1)"});
    // assert
    ASSERT_EQ(config.unassociated_prints.size(), 1);
    EXPECT_EQ(config.unassociated_prints[0].print_type, "TRAN");
}

TEST(SimulationConfigUnassociatedPrintChecks, unassociated_prints_round_trip_through_directives) {
    // arrange
    const SimulationConfig input("", std::monostate{}, {}, {}, OptionParameters({}, {}, {}, {}, {}), {PrintParameters("DC", "", "", {"V(1)"}, {})}, true);
    // act
    const auto directives = input.to_xyce_directives(NetlistTopology{});
    // assert
    const auto found = std::find_if(directives.begin(), directives.end(), [](const std::string& d) { return d.find(".PRINT DC V(1)") != std::string::npos; });
    ASSERT_NE(found, directives.end());
}

// ========================================================================================
// raw output file path computation
// ========================================================================================

TEST(SimulationConfigOutputPathChecks, raw_path_is_nullopt_for_missing_analysis) {
    // arrange
    const SimulationConfig config("", std::monostate{}, {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.raw_output_file_path("/tmp/net.cir");
    // assert
    EXPECT_FALSE(path.has_value());
}

TEST(SimulationConfigOutputPathChecks, raw_path_defaults_to_netlist_plus_raw) {
    // arrange — an OP analysis without print directives
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.raw_output_file_path("/tmp/net.cir");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.raw");
}

TEST(SimulationConfigOutputPathChecks, raw_path_ignores_print_file_uses_netlist_plus_raw) {
    // arrange — a RAW print with an explicit output file: the FILE= option is
    // stripped for the Xyce run, so the produced file is always netlist-derived
    // and the user's file only serves as the copy destination (issue: Xyce must
    // never rewrite a file the application holds mapped)
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, PrintParameters("OP", "RAW", "out.raw", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.raw_output_file_path("/tmp/net.cir");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.raw");
}

TEST(SimulationConfigOutputPathChecks, raw_path_is_nullopt_for_non_raw_format) {
    // arrange — a CSV print produces no raw output file
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, PrintParameters("OP", "CSV", "out.csv", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.raw_output_file_path("/tmp/net.cir");
    // assert
    EXPECT_FALSE(path.has_value());
}

// ========================================================================================
// raw output copy destination computation
// ========================================================================================

TEST(SimulationConfigCopyDestinationChecks, copy_destination_is_nullopt_for_missing_analysis) {
    // arrange
    const SimulationConfig config("", std::monostate{}, {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
}

TEST(SimulationConfigCopyDestinationChecks, copy_destination_resolves_print_file_against_working_directory) {
    // arrange — a RAW print with an explicit output file
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, PrintParameters("OP", "RAW", "out.raw", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    ASSERT_TRUE(destination.has_value());
    EXPECT_EQ(destination->generic_string(), "/tmp/work/out.raw");
}

TEST(SimulationConfigCopyDestinationChecks, copy_destination_is_nullopt_without_print_file) {
    // arrange — a RAW print without an explicit output file
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, PrintParameters("OP", "RAW", "", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
}

TEST(SimulationConfigCopyDestinationChecks, copy_destination_is_nullopt_for_non_raw_format) {
    // arrange — a CSV print produces no raw output file to copy
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, PrintParameters("OP", "CSV", "out.csv", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
}

TEST(SimulationConfigCopyDestinationChecks, copy_destination_strips_quoted_file) {
    // arrange — the model always carries the bare filename; a quote-carrying
    // value (direct construction) is normalized when composing the destination
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, PrintParameters("OP", "RAW", R"("out raw.raw")", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    ASSERT_TRUE(destination.has_value());
    EXPECT_EQ(destination->generic_string(), "/tmp/work/out raw.raw");
}

// ========================================================================================
// legacy OP print normalization
// ========================================================================================

TEST(SimulationConfigAnalysisPrintChecks, analysis_print_statement_normalizes_the_legacy_op_print) {
    // arrange — an OP analysis built from the legacy print_dc_* fields with a
    // duplicated variable; the normalized print must match the legacy emission
    const SimulationConfig config("OP", OpSimulationParameters(true, false, false, {"V(1)", "V(1)"}, "RAW", "dc.raw", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto statement = config.analysis_print_statement();
    // assert: variables are de-duplicated and the statement matches the legacy emission
    ASSERT_TRUE(statement.has_value());
    EXPECT_EQ(*statement, ".PRINT DC FORMAT=RAW FILE=dc.raw V(1)");
}

TEST(SimulationConfigAnalysisPrintChecks, legacy_op_print_statement_matches_the_emitted_directive) {
    // arrange — legacy OP print fields; the emitted directive must equal the
    // analysis print statement so the presenter strips its FILE= option
    const SimulationConfig config("OP", OpSimulationParameters(true, false, false, {"V(1)"}, "RAW", "dc.raw", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto statement = config.analysis_print_statement();
    const auto directives = std::get<OpSimulationParameters>(config.analysis).to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_TRUE(statement.has_value());
    const auto emitted = std::find_if(directives.begin(), directives.end(), [](const std::string& d) { return d.find(".PRINT DC") == 0; });
    ASSERT_NE(emitted, directives.end());
    EXPECT_EQ(*emitted, *statement);
}

TEST(SimulationConfigOutputPathChecks, raw_path_resolves_the_legacy_op_print) {
    // arrange — legacy OP print fields with a RAW format
    const SimulationConfig config("OP", OpSimulationParameters(true, false, false, {"V(1)"}, "RAW", "dc.raw", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.raw_output_file_path("/tmp/net.cir");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.raw");
}

TEST(SimulationConfigOutputPathChecks, raw_path_is_nullopt_for_legacy_op_print_with_non_raw_format) {
    // arrange — legacy OP print fields with a CSV format produce no raw file
    const SimulationConfig config("OP", OpSimulationParameters(true, false, false, {"V(1)"}, "CSV", "dc.csv", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.raw_output_file_path("/tmp/net.cir");
    // assert
    EXPECT_FALSE(path.has_value());
}

TEST(SimulationConfigCopyDestinationChecks, copy_destination_resolves_the_legacy_op_print) {
    // arrange — legacy OP print fields with an explicit output file
    const SimulationConfig config("OP", OpSimulationParameters(true, false, false, {"V(1)"}, "RAW", "dc.raw", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    ASSERT_TRUE(destination.has_value());
    EXPECT_EQ(destination->generic_string(), "/tmp/work/dc.raw");
}

TEST(SimulationConfigCopyDestinationChecks, copy_destination_is_nullopt_for_legacy_op_print_without_file) {
    // arrange — legacy OP print fields without an explicit output file
    const SimulationConfig config("OP", OpSimulationParameters(true, false, false, {"V(1)"}, "RAW", "", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
}

TEST(SimulationConfigCopyDestinationChecks, copy_destination_is_nullopt_for_legacy_op_print_with_non_raw_format) {
    // arrange — legacy OP print fields with a CSV format produce no raw file
    const SimulationConfig config("OP", OpSimulationParameters(true, false, false, {"V(1)"}, "CSV", "dc.csv", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
}

// ========================================================================================
// unassociated print deduplication
// ========================================================================================

TEST(SimulationConfigUnassociatedPrintChecks, op_analysis_does_not_duplicate_its_dc_print) {
    // arrange — a netlist with an .OP analysis and a .PRINT DC directive; the
    // OP parser claims the print into its structured print parameters, so the
    // directive must not be appended to the unassociated prints as well
    const auto config = SimulationConfig::from_xyce_directives({".OP", ".PRINT DC FORMAT=RAW FILE=dc.raw V(1)"});
    // assert
    ASSERT_EQ(config.analysis_type, "OP");
    ASSERT_TRUE(config.unassociated_prints.empty());
    const auto& print_parameters = std::get<OpSimulationParameters>(config.analysis).print_parameters;
    ASSERT_TRUE(print_parameters.has_value());
    EXPECT_EQ(print_parameters->print_file, "dc.raw");
}

TEST(SimulationConfigUnassociatedPrintChecks, dc_print_stays_unassociated_for_other_analyses) {
    // arrange — a .PRINT DC under a transient analysis is not claimed by the
    // analysis and must remain in the unassociated prints
    const auto config = SimulationConfig::from_xyce_directives({".TRAN 1u 1m", ".PRINT DC FORMAT=RAW FILE=dc.raw V(1)"});
    // assert
    ASSERT_EQ(config.analysis_type, "TRAN");
    ASSERT_EQ(config.unassociated_prints.size(), 1);
    EXPECT_EQ(config.unassociated_prints[0].print_type, "DC");
    EXPECT_EQ(config.unassociated_prints[0].print_file, "dc.raw");
}

// ========================================================================================
// analysis print statement serialization
// ========================================================================================

TEST(SimulationConfigAnalysisPrintChecks, analysis_print_statement_is_nullopt_for_missing_analysis) {
    // arrange
    const SimulationConfig config("", std::monostate{}, {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto statement = config.analysis_print_statement();
    // assert
    EXPECT_FALSE(statement.has_value());
}

TEST(SimulationConfigAnalysisPrintChecks, analysis_print_statement_serializes_the_analysis_print) {
    // arrange — a RAW print with an explicit output file
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, PrintParameters("OP", "RAW", "out.raw", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto statement = config.analysis_print_statement();
    // assert
    ASSERT_TRUE(statement.has_value());
    EXPECT_EQ(*statement, ".PRINT OP FORMAT=RAW FILE=out.raw V(1)");
}

TEST(SimulationConfigAnalysisPrintChecks, analysis_print_statement_is_nullopt_without_print) {
    // arrange — an OP analysis with the print disabled
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto statement = config.analysis_print_statement();
    // assert
    EXPECT_FALSE(statement.has_value());
}

TEST(SimulationConfigAnalysisPrintChecks, analysis_print_statement_is_a_prefix_of_the_expanded_noise_print) {
    // arrange — a NOISE analysis carrying device noise operators; the Xyce
    // reference guide documents DNI()/DNO() as output variables on the
    // .PRINT NOISE line, and the noise serializer appends them after the
    // base print statement
    const SimulationConfig config("NOISE", NoiseSimulationParameters("out", "", "V1", "1", "100MEG", "10", "DEC", {DeviceNoiseOperator("DNI", "R1", "")}, "", PrintParameters("NOISE", "RAW", "out.raw", {"INOISE"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto statement = config.analysis_print_statement();
    const auto directives = std::get<NoiseSimulationParameters>(config.analysis).to_xyce_directives(NetlistTopology{});
    // find the emitted noise print directive
    const auto emitted = std::find_if(directives.begin(), directives.end(), [](const std::string& d) { return d.find(".PRINT NOISE") == 0; });
    // assert: the analysis print statement is a prefix of the emitted directive
    ASSERT_TRUE(statement.has_value());
    ASSERT_NE(emitted, directives.end());
    EXPECT_EQ(emitted->compare(0, statement->size(), *statement), 0);
    EXPECT_EQ(*statement, ".PRINT NOISE FORMAT=RAW FILE=out.raw INOISE");
    EXPECT_EQ(*emitted, ".PRINT NOISE FORMAT=RAW FILE=out.raw INOISE DNI(R1)");
}

// ========================================================================================
// FFT output file path pattern computation
// ========================================================================================

TEST(SimulationConfigFftPathChecks, fft_pattern_requires_an_analysis) {
    // arrange
    const SimulationConfig config("", std::monostate{}, {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto pattern = config.fft_output_file_path_pattern("/tmp/net.cir");
    // assert
    EXPECT_FALSE(pattern.has_value());
}

TEST(SimulationConfigFftPathChecks, fft_pattern_requires_fft_parameters) {
    // arrange — a transient analysis without .FFT directives
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto pattern = config.fft_output_file_path_pattern("/tmp/net.cir");
    // assert
    EXPECT_FALSE(pattern.has_value());
}

TEST(SimulationConfigFftPathChecks, fft_pattern_matches_transient_with_fft_parameters) {
    // arrange — a transient analysis carrying one .FFT directive
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, std::nullopt, {FftParameters("V(1)", "", "", "", "", "", "", "", "", "")}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto pattern = config.fft_output_file_path_pattern("/tmp/net.cir");
    // assert
    ASSERT_TRUE(pattern.has_value());
    EXPECT_EQ(pattern->generic_string(), "/tmp/net.cir.fft*");
}

TEST(SimulationConfigFftPathChecks, fft_pattern_is_absent_for_other_analyses) {
    // arrange
    const SimulationConfig config("AC", AcSimulationParameters("DEC", "10", "1", "1MEG", "", std::nullopt, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto pattern = config.fft_output_file_path_pattern("/tmp/net.cir");
    // assert
    EXPECT_FALSE(pattern.has_value());
}

// ========================================================================================
// s-parameter output file path computation
// ========================================================================================

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_requires_an_analysis) {
    // arrange
    const SimulationConfig config("", std::monostate{}, {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    EXPECT_FALSE(path.has_value());
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_is_absent_for_other_analyses) {
    // arrange
    const SimulationConfig config("AC", AcSimulationParameters("DEC", "10", "1", "1MEG", "", std::nullopt, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    EXPECT_FALSE(path.has_value());
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_defaults_to_netlist_plus_s2p_without_file) {
    // arrange — a LIN analysis with the default touchstone format and no FILE=
    const SimulationConfig config("LIN", LinSimulationParameters(true, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "101", "1", "100k", "", std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act — without FILE= Xyce writes <netlist>.sNp next to the netlist, N being
    // the port count (2 here), so the working directory plays no role
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.s2p");
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_uses_port_count_for_default_name) {
    // arrange — a 3-port LIN run without FILE=; Xyce writes <netlist>.s3p
    const SimulationConfig config("LIN", LinSimulationParameters(true, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "101", "1", "100k", "", std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work", 3);
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.s3p");
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_resolves_file_against_working_directory) {
    // arrange — a LIN analysis with an explicit FILE=; Xyce resolves the value
    // against its process cwd, which is the run's working directory
    const SimulationConfig config("LIN", LinSimulationParameters(true, "TOUCHSTONE2", "S", "RI", "out.s2p", "", "", "LIN", "101", "1", "100k", "", std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/work/out.s2p");
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_falls_back_to_filename_when_file_missing) {
    // arrange — FILENAME= is the HSPICE synonym, accepted only when FILE= is absent
    const auto config = SimulationConfig::from_xyce_directives({".LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILENAME=synonym.s2p"});
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/work/synonym.s2p");
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_gives_file_precedence_over_filename) {
    // arrange — both FILE= and FILENAME= carried (direct construction)
    const SimulationConfig config("LIN", LinSimulationParameters(true, "TOUCHSTONE2", "S", "RI", "file.s2p", "", "filename.s2p", "LIN", "101", "1", "100k", "", std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/work/file.s2p");
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_strips_quoted_file) {
    // arrange — a quote-carrying FILE value (direct construction) is normalized
    const SimulationConfig config("LIN", LinSimulationParameters(true, "TOUCHSTONE2", "S", "RI", R"("out s2p.s2p")", "", "", "LIN", "101", "1", "100k", "", std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/work/out s2p.s2p");
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_keeps_absolute_file) {
    // arrange — an absolute FILE= value is used verbatim
    const SimulationConfig config("LIN", LinSimulationParameters(true, "TOUCHSTONE2", "S", "RI", "/tmp/abs/out.s2p", "", "", "LIN", "101", "1", "100k", "", std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/abs/out.s2p");
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_is_absent_for_non_touchstone_format) {
    // arrange — a LIN run that writes its output in a non-touchstone format
    const SimulationConfig config("LIN", LinSimulationParameters(true, "SPICE", "S", "RI", "out.s2p", "", "", "LIN", "101", "1", "100k", "", std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    EXPECT_FALSE(path.has_value());
}

TEST(SimulationConfigSParameterPathChecks, s_parameter_path_resolves_from_parsed_directives) {
    // arrange — a LIN directive set parsed end to end, mirroring the run flow
    const auto config = SimulationConfig::from_xyce_directives({".LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=lin-simple-01.s2p WIDTH=16 PRECISION=8"});
    // act
    const auto path = config.s_parameter_output_file_path("/tmp/net.cir", "/tmp/work");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/work/lin-simple-01.s2p");
}

// ========================================================================================
// produced measurements
// ========================================================================================

TEST(SimulationConfigProducedMeasurementsChecks, no_analysis_produces_nothing) {
    // arrange
    const SimulationConfig config("", std::monostate{}, {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto produced = config.produced_measurements();
    // assert
    EXPECT_FALSE(produced.s_parameters);
    EXPECT_FALSE(produced.fft);
}

TEST(SimulationConfigProducedMeasurementsChecks, lin_with_touchstone_format_produces_s_parameters) {
    // arrange
    const SimulationConfig config("LIN", LinSimulationParameters(true, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "101", "1", "100k", "", std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto produced = config.produced_measurements();
    // assert
    EXPECT_TRUE(produced.s_parameters);
    EXPECT_FALSE(produced.fft);
}

TEST(SimulationConfigProducedMeasurementsChecks, lin_without_touchstone_format_produces_no_s_parameters) {
    // arrange
    const SimulationConfig config("LIN", LinSimulationParameters(true, "SPICE", "S", "RI", "", "", "", "LIN", "101", "1", "100k", "", std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto produced = config.produced_measurements();
    // assert
    EXPECT_FALSE(produced.s_parameters);
    EXPECT_FALSE(produced.fft);
}

TEST(SimulationConfigProducedMeasurementsChecks, tran_with_fft_directives_produces_fft) {
    // arrange
    const SimulationConfig config("TRAN", TransientSimulationParameters("1n", "10u", "", "", "", {}, std::nullopt, {FftParameters("V(1)", "", "", "", "", "", "", "", "", "")}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto produced = config.produced_measurements();
    // assert
    EXPECT_TRUE(produced.fft);
    EXPECT_FALSE(produced.s_parameters);
}

TEST(SimulationConfigProducedMeasurementsChecks, tran_without_fft_directives_produces_no_fft) {
    // arrange
    const SimulationConfig config("TRAN", TransientSimulationParameters("1n", "10u", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto produced = config.produced_measurements();
    // assert
    EXPECT_FALSE(produced.fft);
    EXPECT_FALSE(produced.s_parameters);
}

TEST(SimulationConfigProducedMeasurementsChecks, other_analyses_produce_nothing) {
    // arrange
    const SimulationConfig config("AC", AcSimulationParameters("DEC", "10", "1", "1MEG", "", std::nullopt, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto produced = config.produced_measurements();
    // assert
    EXPECT_FALSE(produced.s_parameters);
    EXPECT_FALSE(produced.fft);
}

// ========================================================================================
// validate
// ========================================================================================

TEST(SimulationConfigValidationChecks, validate_passes_when_no_steps) {
    // arrange
    const SimulationConfig config("DC", DCSimulationParameters("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act / assert
    EXPECT_FALSE(config.validate().has_value());
}

TEST(SimulationConfigValidationChecks, validate_passes_when_step_is_disabled) {
    // arrange
    const StepParameters disabled_step("LIN", "R1", "1k", "10k", "1k", "", {}, "", false);
    const SimulationConfig config("DC", DCSimulationParameters("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt), {disabled_step}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act / assert
    EXPECT_FALSE(config.validate().has_value());
}

TEST(SimulationConfigValidationChecks, validate_rejects_step_without_analysis) {
    // arrange — a step is enabled but no primary analysis is configured
    const StepParameters enabled_step("LIN", "R1", "1k", "10k", "1k", "", {}, "", true);
    const SimulationConfig config("", std::monostate{}, {enabled_step}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act / assert
    const auto error = config.validate();
    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->find("primary analysis"), std::string::npos);
}

TEST(SimulationConfigValidationChecks, validate_checks_invalid_step_params) {
    // arrange — a step has empty variable
    const StepParameters bad_step("LIST", "", "", "", "", "", {}, "", true);
    const SimulationConfig config("DC", DCSimulationParameters("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt), {bad_step}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act / assert
    const auto error = config.validate();
    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->find("sweep variable"), std::string::npos);
}

TEST(SimulationConfigValidationChecks, validate_passes_with_enabled_step_and_analysis) {
    // arrange
    const StepParameters valid_step("LIN", "R1", "1k", "10k", "1k", "", {}, "", true);
    const SimulationConfig config("DC", DCSimulationParameters("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt), {valid_step}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act / assert
    EXPECT_FALSE(config.validate().has_value());
}

TEST(SimulationConfigPrintSanitizeChecks, ac_analysis_print_drops_unsupported_wildcards) {
    // arrange — a netlist directive set with an AC analysis and a print
    // carrying the power and device lead wildcards the AC analysis cannot
    // produce
    // act
    const auto config = SimulationConfig::from_xyce_directives({".AC DEC 10 1 100k", ".PRINT AC FORMAT=RAW V(*) I(*) P(*) IC(*) ID(*)"});
    // assert — the analysis print keeps only the supported wildcards
    const auto print_parameters = config.analysis_print_parameters();
    ASSERT_TRUE(print_parameters.has_value());
    ASSERT_EQ(print_parameters->output_variables, std::vector<std::string>({"V(*)", "I(*)"}));
}

TEST(SimulationConfigPrintSanitizeChecks, lin_analysis_print_keeps_the_ac_restriction) {
    // arrange — a LIN directive set whose associated AC print carries the
    // power and lead wildcards
    // act
    const auto config = SimulationConfig::from_xyce_directives({".AC DEC 10 1 100k", ".LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=lin.s2p", ".PRINT AC FORMAT=RAW V(*) P(*) IC(*)"});
    // assert — the LIN analysis print is an AC print, sanitized the same way
    const auto print_parameters = config.analysis_print_parameters();
    ASSERT_TRUE(print_parameters.has_value());
    ASSERT_EQ(print_parameters->output_variables, std::vector<std::string>({"V(*)"}));
}

TEST(SimulationConfigPrintSanitizeChecks, transient_analysis_print_keeps_power_and_leads) {
    // arrange — a transient netlist carrying the full wildcard set
    // act
    const auto config = SimulationConfig::from_xyce_directives({".TRAN 1u 1m", ".PRINT TRAN FORMAT=RAW V(*) P(*) IC(*)"});
    // assert — transient prints keep the power and lead current wildcards
    const auto print_parameters = config.analysis_print_parameters();
    ASSERT_TRUE(print_parameters.has_value());
    ASSERT_EQ(print_parameters->output_variables, std::vector<std::string>({"V(*)", "P(*)", "IC(*)"}));
}

TEST(SimulationConfigPrnPrintParametersChecks, collects_std_format_analysis_print) {
    // arrange — dc analysis with std-format print
    const auto config = SimulationConfig::from_xyce_directives({".DC V1 0 5 0.5", ".PRINT DC FORMAT=STD V(1)"});
    // act
    const auto prn_params = config.prn_print_parameters();
    // assert
    ASSERT_EQ(prn_params.size(), 1u);
    EXPECT_EQ(prn_params[0].print_type, "DC");
    EXPECT_EQ(prn_params[0].print_format, "STD");
}

TEST(SimulationConfigPrnPrintParametersChecks, collects_noindex_format_analysis_print) {
    // arrange — transient analysis with noindex-format print
    const auto config = SimulationConfig::from_xyce_directives({".TRAN 1u 1m", ".PRINT TRAN FORMAT=NOINDEX V(1)"});
    // act
    const auto prn_params = config.prn_print_parameters();
    // assert
    ASSERT_EQ(prn_params.size(), 1u);
    EXPECT_EQ(prn_params[0].print_type, "TRAN");
    EXPECT_EQ(prn_params[0].print_format, "NOINDEX");
}

TEST(SimulationConfigPrnPrintParametersChecks, excludes_raw_format) {
    // arrange — analysis with raw-format print
    const auto config = SimulationConfig::from_xyce_directives({".DC V1 0 5 0.5", ".PRINT DC FORMAT=RAW V(1)"});
    // act
    const auto prn_params = config.prn_print_parameters();
    // assert
    ASSERT_TRUE(prn_params.empty());
}

TEST(SimulationConfigPrnPrintParametersChecks, collects_unassociated_prn_prints) {
    // arrange — analysis print is raw but there is an unassociated print with std format
    const auto config = SimulationConfig::from_xyce_directives({".DC V1 0 5 0.5", ".PRINT DC FORMAT=RAW V(1)", ".PRINT DC FORMAT=GNUPLOT I(R1)"});
    // act
    const auto prn_params = config.prn_print_parameters();
    // assert — the analysis print (raw) is excluded, the unassociated gnuplot print is included
    ASSERT_EQ(prn_params.size(), 1u);
    EXPECT_EQ(prn_params[0].print_format, "GNUPLOT");
}

TEST(PrnOutputSuffixChecks, ac_produces_fd_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("AC");
    // assert
    EXPECT_EQ(suffix, ".FD.prn");
}

TEST(PrnOutputSuffixChecks, ac_ic_produces_td_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("AC_IC");
    // assert
    EXPECT_EQ(suffix, ".TD.prn");
}

TEST(PrnOutputSuffixChecks, dc_produces_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("DC");
    // assert
    EXPECT_EQ(suffix, ".prn");
}

TEST(PrnOutputSuffixChecks, tran_produces_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("TRAN");
    // assert
    EXPECT_EQ(suffix, ".prn");
}

TEST(PrnOutputSuffixChecks, noise_produces_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("NOISE");
    // assert
    EXPECT_EQ(suffix, ".prn");
}

TEST(PrnOutputSuffixChecks, homotopy_produces_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("HOMOTOPY");
    // assert
    EXPECT_EQ(suffix, ".prn");
}

TEST(PrnOutputSuffixChecks, es_produces_es_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("ES");
    // assert
    EXPECT_EQ(suffix, ".ES.prn");
}

TEST(PrnOutputSuffixChecks, hb_produces_hb_fd_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("HB");
    // assert
    EXPECT_EQ(suffix, ".HB.FD.prn");
}

TEST(PrnOutputSuffixChecks, hb_fd_produces_hb_fd_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("HB_FD");
    // assert
    EXPECT_EQ(suffix, ".HB.FD.prn");
}

TEST(PrnOutputSuffixChecks, hb_td_produces_hb_td_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("HB_TD");
    // assert
    EXPECT_EQ(suffix, ".HB.TD.prn");
}

TEST(PrnOutputSuffixChecks, hb_ic_produces_hb_ic_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("HB_IC");
    // assert
    EXPECT_EQ(suffix, ".hb_ic.prn");
}

TEST(PrnOutputSuffixChecks, hb_startup_produces_startup_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("HB_STARTUP");
    // assert
    EXPECT_EQ(suffix, ".startup.prn");
}

TEST(PrnOutputSuffixChecks, sens_produces_sens_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("SENS");
    // assert
    EXPECT_EQ(suffix, ".SENS.prn");
}

TEST(PrnOutputSuffixChecks, tranadjoint_produces_tradj_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("TRANADJOINT");
    // assert
    EXPECT_EQ(suffix, ".TRADJ.prn");
}

TEST(PrnOutputSuffixChecks, pce_produces_pce_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("PCE");
    // assert
    EXPECT_EQ(suffix, ".PCE.prn");
}

TEST(PrnOutputSuffixChecks, lin_produces_fd_dot_prn) {
    // arrange / act
    const auto suffix = prn_output_suffix("LIN");
    // assert
    EXPECT_EQ(suffix, ".FD.prn");
}

TEST(PrnOutputSuffixChecks, unknown_type_falls_back_to_dot_prn) {
    // arrange / act
    const auto unknown_suffix = prn_output_suffix("UNKNOWN_TYPE");
    const auto empty_suffix = prn_output_suffix("");
    // assert
    EXPECT_EQ(unknown_suffix, ".prn");
    EXPECT_EQ(empty_suffix, ".prn");
}

TEST(CsvOutputSuffixChecks, tran_produces_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("TRAN");
    // assert
    EXPECT_EQ(suffix, ".csv");
}

TEST(CsvOutputSuffixChecks, dc_produces_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("DC");
    // assert
    EXPECT_EQ(suffix, ".csv");
}

TEST(CsvOutputSuffixChecks, ac_produces_fd_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("AC");
    // assert
    EXPECT_EQ(suffix, ".FD.csv");
}

TEST(CsvOutputSuffixChecks, ac_ic_produces_td_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("AC_IC");
    // assert
    EXPECT_EQ(suffix, ".TD.csv");
}

TEST(CsvOutputSuffixChecks, hb_variants_produce_their_own_csv_suffixes) {
    // arrange / act
    const auto hb = csv_output_suffix("HB");
    const auto hb_fd = csv_output_suffix("HB_FD");
    const auto hb_td = csv_output_suffix("HB_TD");
    const auto hb_ic = csv_output_suffix("HB_IC");
    const auto hb_startup = csv_output_suffix("HB_STARTUP");
    // assert
    EXPECT_EQ(hb, ".HB.FD.csv");
    EXPECT_EQ(hb_fd, ".HB.FD.csv");
    EXPECT_EQ(hb_td, ".HB.TD.csv");
    EXPECT_EQ(hb_ic, ".hb_ic.csv");
    EXPECT_EQ(hb_startup, ".startup.csv");
}

TEST(CsvOutputSuffixChecks, noise_produces_noise_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("NOISE");
    // assert
    EXPECT_EQ(suffix, ".NOISE.csv");
}

TEST(CsvOutputSuffixChecks, homotopy_produces_homotopy_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("HOMOTOPY");
    // assert
    EXPECT_EQ(suffix, ".HOMOTOPY.csv");
}

TEST(CsvOutputSuffixChecks, sens_produces_sens_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("SENS");
    // assert
    EXPECT_EQ(suffix, ".SENS.csv");
}

TEST(CsvOutputSuffixChecks, tranadjoint_produces_tradj_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("TRANADJOINT");
    // assert
    EXPECT_EQ(suffix, ".TRADJ.csv");
}

TEST(CsvOutputSuffixChecks, es_produces_es_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("ES");
    // assert
    EXPECT_EQ(suffix, ".ES.csv");
}

TEST(CsvOutputSuffixChecks, pce_produces_pce_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("PCE");
    // assert
    EXPECT_EQ(suffix, ".PCE.csv");
}

TEST(CsvOutputSuffixChecks, lin_produces_fd_dot_csv) {
    // arrange / act
    const auto suffix = csv_output_suffix("LIN");
    // assert
    EXPECT_EQ(suffix, ".FD.csv");
}

TEST(CsvOutputSuffixChecks, unknown_type_falls_back_to_dot_csv) {
    // arrange / act
    const auto unknown_suffix = csv_output_suffix("UNKNOWN_TYPE");
    const auto empty_suffix = csv_output_suffix("");
    const auto lower_suffix = csv_output_suffix("ac");
    // assert
    EXPECT_EQ(unknown_suffix, ".csv");
    EXPECT_EQ(empty_suffix, ".csv");
    EXPECT_EQ(lower_suffix, ".FD.csv");
}

TEST(TecplotOutputSuffixChecks, tran_produces_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("TRAN");
    // assert
    EXPECT_EQ(suffix, ".dat");
}

TEST(TecplotOutputSuffixChecks, dc_produces_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("DC");
    // assert
    EXPECT_EQ(suffix, ".dat");
}

TEST(TecplotOutputSuffixChecks, ac_produces_fd_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("AC");
    // assert
    EXPECT_EQ(suffix, ".FD.dat");
}

TEST(TecplotOutputSuffixChecks, ac_ic_produces_td_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("AC_IC");
    // assert
    EXPECT_EQ(suffix, ".TD.dat");
}

TEST(TecplotOutputSuffixChecks, hb_variants_produce_their_own_dat_suffixes) {
    // arrange / act
    const auto hb = tecplot_output_suffix("HB");
    const auto hb_fd = tecplot_output_suffix("HB_FD");
    const auto hb_td = tecplot_output_suffix("HB_TD");
    const auto hb_ic = tecplot_output_suffix("HB_IC");
    const auto hb_startup = tecplot_output_suffix("HB_STARTUP");
    // assert
    EXPECT_EQ(hb, ".HB.FD.dat");
    EXPECT_EQ(hb_fd, ".HB.FD.dat");
    EXPECT_EQ(hb_td, ".HB.TD.dat");
    EXPECT_EQ(hb_ic, ".hb_ic.dat");
    EXPECT_EQ(hb_startup, ".startup.dat");
}

TEST(TecplotOutputSuffixChecks, noise_produces_noise_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("NOISE");
    // assert
    EXPECT_EQ(suffix, ".NOISE.dat");
}

TEST(TecplotOutputSuffixChecks, homotopy_produces_homotopy_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("HOMOTOPY");
    // assert
    EXPECT_EQ(suffix, ".HOMOTOPY.dat");
}

TEST(TecplotOutputSuffixChecks, sens_produces_sens_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("SENS");
    // assert
    EXPECT_EQ(suffix, ".SENS.dat");
}

TEST(TecplotOutputSuffixChecks, tranadjoint_produces_tradj_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("TRANADJOINT");
    // assert
    EXPECT_EQ(suffix, ".TRADJ.dat");
}

TEST(TecplotOutputSuffixChecks, es_produces_es_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("ES");
    // assert
    EXPECT_EQ(suffix, ".ES.dat");
}

TEST(TecplotOutputSuffixChecks, pce_produces_pce_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("PCE");
    // assert
    EXPECT_EQ(suffix, ".PCE.dat");
}

TEST(TecplotOutputSuffixChecks, lin_produces_fd_dot_dat) {
    // arrange / act
    const auto suffix = tecplot_output_suffix("LIN");
    // assert
    EXPECT_EQ(suffix, ".FD.dat");
}

TEST(TecplotOutputSuffixChecks, unknown_type_falls_back_to_dot_dat) {
    // arrange / act
    const auto unknown_suffix = tecplot_output_suffix("UNKNOWN_TYPE");
    const auto empty_suffix = tecplot_output_suffix("");
    const auto lower_suffix = tecplot_output_suffix("ac");
    // assert
    EXPECT_EQ(unknown_suffix, ".dat");
    EXPECT_EQ(empty_suffix, ".dat");
    EXPECT_EQ(lower_suffix, ".FD.dat");
}

// ========================================================================================
// csd output file path computation
// ========================================================================================

TEST(SimulationConfigCsdOutputPathChecks, csd_path_is_nullopt_for_missing_analysis) {
    // arrange
    const SimulationConfig config("", std::monostate{}, {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.csd_output_file_path("/tmp/net.cir");
    // assert
    EXPECT_FALSE(path.has_value());
}

TEST(SimulationConfigCsdOutputPathChecks, csd_path_defaults_to_netlist_plus_csd) {
    // arrange — a transient PROBE print produces the plain .csd file
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, PrintParameters("TRAN", "PROBE", "out.csd", {"V(1)"}, {}), {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.csd_output_file_path("/tmp/net.cir");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.csd");
}

TEST(SimulationConfigCsdOutputPathChecks, csd_path_ignores_print_file_uses_netlist_plus_csd) {
    // arrange — a PROBE print with an explicit output file: the FILE= option is
    // stripped for the Xyce run, so the produced file is always netlist-derived
    // and the user's file only serves as the copy destination
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, PrintParameters("TRAN", "PROBE", "out.csd", {"V(1)"}, {}), {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.csd_output_file_path("/tmp/net.cir");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.csd");
}

TEST(SimulationConfigCsdOutputPathChecks, csd_path_carries_the_td_suffix_for_ac_ic_prints) {
    // arrange — the AC_IC print type writes time-domain output to a .TD.csd file
    const SimulationConfig config("AC", AcSimulationParameters("DEC", "10", "1", "1MEG", "", PrintParameters("AC_IC", "PROBE", "out.csd", {"V(1)"}, {}), {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.csd_output_file_path("/tmp/net.cir");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.TD.csd");
}

TEST(SimulationConfigCsdOutputPathChecks, csd_path_is_nullopt_for_non_probe_format) {
    // arrange — a CSV print produces no csd output file
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, PrintParameters("TRAN", "CSV", "out.csv", {"V(1)"}, {}), {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.csd_output_file_path("/tmp/net.cir");
    // assert
    EXPECT_FALSE(path.has_value());
}

TEST(SimulationConfigCsdOutputPathChecks, csd_path_resolves_the_legacy_op_print) {
    // arrange — legacy OP print fields with a PROBE format normalize to a DC print
    const SimulationConfig config("OP", OpSimulationParameters(true, false, false, {"V(1)"}, "PROBE", "dc.csd", false, "NODESET", "", {}, {}), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.csd_output_file_path("/tmp/net.cir");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.csd");
}

// ========================================================================================
// csd output copy destination computation
// ========================================================================================

TEST(SimulationConfigCsdCopyDestinationChecks, copy_destination_is_nullopt_for_missing_analysis) {
    // arrange
    const SimulationConfig config("", std::monostate{}, {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.csd_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
}

TEST(SimulationConfigCsdCopyDestinationChecks, copy_destination_resolves_print_file_against_working_directory) {
    // arrange — a PROBE print with an explicit output file
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, PrintParameters("TRAN", "PROBE", "out.csd", {"V(1)"}, {}), {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.csd_output_copy_destination("/tmp/work");
    // assert
    ASSERT_TRUE(destination.has_value());
    EXPECT_EQ(destination->generic_string(), "/tmp/work/out.csd");
}

TEST(SimulationConfigCsdCopyDestinationChecks, copy_destination_is_nullopt_without_print_file) {
    // arrange — a PROBE print without an explicit output file
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, PrintParameters("TRAN", "PROBE", "", {"V(1)"}, {}), {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.csd_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
}

TEST(SimulationConfigCsdCopyDestinationChecks, copy_destination_is_nullopt_for_non_probe_format) {
    // arrange — a CSV print produces no csd output file to copy
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, PrintParameters("TRAN", "CSV", "out.csv", {"V(1)"}, {}), {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.csd_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
}

TEST(SimulationConfigCsdCopyDestinationChecks, copy_destination_strips_quoted_file) {
    // arrange — the model always carries the bare filename; a quote-carrying
    // value (direct construction) is normalized when composing the destination
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, PrintParameters("TRAN", "PROBE", R"("out probe.csd")", {"V(1)"}, {}), {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.csd_output_copy_destination("/tmp/work");
    // assert
    ASSERT_TRUE(destination.has_value());
    EXPECT_EQ(destination->generic_string(), "/tmp/work/out probe.csd");
}

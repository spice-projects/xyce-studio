#include <algorithm>

#include <gtest/gtest.h>

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
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, false);
    // act
    const auto directives = config.to_xyce_directives(NetlistTopology{});
    // assert
    const auto found = std::find(directives.begin(), directives.end(), ".PREPROCESS REPLACEGROUND FALSE");
    ASSERT_NE(found, directives.end());
}

TEST(SimulationConfigReplaceGroundChecks, disabled_state_round_trips_through_directives) {
    // arrange
    const SimulationConfig input("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, false);
    // act
    const auto directives = input.to_xyce_directives(NetlistTopology{});
    const auto output = SimulationConfig::from_xyce_directives(directives);
    // assert
    EXPECT_FALSE(output.replace_ground);
}

TEST(SimulationConfigReplaceGroundChecks, enabled_state_round_trips_through_directives) {
    // arrange
    const SimulationConfig input("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
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
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
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
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, {}, PrintParameters("OP", "RAW", "out.raw", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto path = config.raw_output_file_path("/tmp/net.cir");
    // assert
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->generic_string(), "/tmp/net.cir.raw");
}

TEST(SimulationConfigOutputPathChecks, raw_path_is_nullopt_for_non_raw_format) {
    // arrange — a CSV print produces no raw output file
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, {}, PrintParameters("OP", "CSV", "out.csv", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
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
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, {}, PrintParameters("OP", "RAW", "out.raw", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    ASSERT_TRUE(destination.has_value());
    EXPECT_EQ(destination->generic_string(), "/tmp/work/out.raw");
}

TEST(SimulationConfigCopyDestinationChecks, copy_destination_is_nullopt_without_print_file) {
    // arrange — a RAW print without an explicit output file
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, {}, PrintParameters("OP", "RAW", "", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
}

TEST(SimulationConfigCopyDestinationChecks, copy_destination_is_nullopt_for_non_raw_format) {
    // arrange — a CSV print produces no raw output file to copy
    const SimulationConfig config("OP", OpSimulationParameters(false, false, false, {}, "", "", false, "NODESET", "", {}, {}, PrintParameters("OP", "CSV", "out.csv", {"V(1)"}, {})), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto destination = config.raw_output_copy_destination("/tmp/work");
    // assert
    EXPECT_FALSE(destination.has_value());
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
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act
    const auto pattern = config.fft_output_file_path_pattern("/tmp/net.cir");
    // assert
    EXPECT_FALSE(pattern.has_value());
}

TEST(SimulationConfigFftPathChecks, fft_pattern_matches_transient_with_fft_parameters) {
    // arrange — a transient analysis carrying one .FFT directive
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, std::nullopt, {FftParameters("V(1)", "", "", "", "", "", "", "", "", "")}, {}, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
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
// validate
// ========================================================================================

TEST(SimulationConfigValidationChecks, validate_passes_when_no_steps) {
    // arrange
    const SimulationConfig config("DC", DCSimulationParameters("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act / assert
    EXPECT_FALSE(config.validate().has_value());
}

TEST(SimulationConfigValidationChecks, validate_passes_when_step_is_disabled) {
    // arrange
    const StepParameters disabled_step("LIN", "R1", "1k", "10k", "1k", "", {}, "", false);
    const SimulationConfig config("DC", DCSimulationParameters("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt), {disabled_step}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
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
    const SimulationConfig config("DC", DCSimulationParameters("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt), {bad_step}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act / assert
    const auto error = config.validate();
    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->find("sweep variable"), std::string::npos);
}

TEST(SimulationConfigValidationChecks, validate_passes_with_enabled_step_and_analysis) {
    // arrange
    const StepParameters valid_step("LIN", "R1", "1k", "10k", "1k", "", {}, "", true);
    const SimulationConfig config("DC", DCSimulationParameters("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt), {valid_step}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    // act / assert
    EXPECT_FALSE(config.validate().has_value());
}

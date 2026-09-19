#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "simulation/dc_simulation_parameters.h"

// ========================================================================================
// from_xyce_directives
// ========================================================================================

TEST(DCSimulationParametersChecks, parses_lin_sweep) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN 0 5 0.1"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "0");
    ASSERT_EQ(result->sweeps[0].stop, "5");
    ASSERT_EQ(result->sweeps[0].step, "0.1");
}

TEST(DCSimulationParametersChecks, parses_dec_sweep) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC DEC VIN 1k 100MEG 10"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DEC");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "1k");
    ASSERT_EQ(result->sweeps[0].stop, "100MEG");
    ASSERT_EQ(result->sweeps[0].points, "10");
}

TEST(DCSimulationParametersChecks, parses_oct_sweep) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC OCT VIN 1 1MEG 5"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "OCT");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "1");
    ASSERT_EQ(result->sweeps[0].stop, "1MEG");
    ASSERT_EQ(result->sweeps[0].points, "5");
}

TEST(DCSimulationParametersChecks, parses_list_sweep) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN LIST 1k 2k 5k"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIST");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].list_values.size(), 3);
    ASSERT_EQ(result->sweeps[0].list_values[0], "1k");
    ASSERT_EQ(result->sweeps[0].list_values[1], "2k");
    ASSERT_EQ(result->sweeps[0].list_values[2], "5k");
}

TEST(DCSimulationParametersChecks, parses_data_sweep) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC DATA=myTable"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DATA");
    ASSERT_EQ(result->data_table_name, "myTable");
}

TEST(DCSimulationParametersChecks, parses_secondary_sweep) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC R1 0 3.5 0.05 C1 0 3.5 0.5"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweeps[1].variable, "C1");
    ASSERT_EQ(result->sweeps[1].start, "0");
    ASSERT_EQ(result->sweeps[1].stop, "3.5");
    ASSERT_EQ(result->sweeps[1].step, "0.5");
}

TEST(DCSimulationParametersChecks, parses_sensitivity_companion_directive) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN 0 5 0.1", ".SENS objfunc={V(OUT)} param=R1:R", ".OPTIONS SENSITIVITY direct=0 adjoint=1"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->sensitivity.has_value());
    ASSERT_EQ(result->sensitivity->objective_mode, "objfunc");
    ASSERT_EQ(result->sensitivity->objective_values.size(), 1);
    ASSERT_EQ(result->sensitivity->objective_values[0], "V(OUT)");
    ASSERT_EQ(result->sensitivity->parameter_list.size(), 1);
    ASSERT_EQ(result->sensitivity->parameter_list[0], "R1:R");
    ASSERT_FALSE(result->sensitivity->direct);
    ASSERT_TRUE(result->sensitivity->adjoint);
}

TEST(DCSimulationParametersChecks, sensitivity_round_trip) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN 0 5 0.1", ".SENS objfunc={V(OUT)} param=R1:R", ".OPTIONS SENSITIVITY direct=0 adjoint=1"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    const auto serialized = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(serialized.size(), 3);
    ASSERT_EQ(serialized[0], ".DC VIN 0 5 0.1");
    ASSERT_EQ(serialized[1], ".SENS objfunc={V(OUT)} param=R1:R");
    ASSERT_EQ(serialized[2], ".OPTIONS SENSITIVITY direct=0 adjoint=1");
}

TEST(DCSimulationParametersChecks, no_dc_directive_returns_none) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1u 1m"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(DCSimulationParametersChecks, duplicate_dc_directives_last_wins_lin) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN 0 5 0.1", ".DC VGG -2 2 0.05"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps.size(), 1);
    ASSERT_EQ(result->sweeps[0].variable, "VGG");
    ASSERT_EQ(result->sweeps[0].start, "-2");
    ASSERT_EQ(result->sweeps[0].stop, "2");
    ASSERT_EQ(result->sweeps[0].step, "0.05");
}

TEST(DCSimulationParametersChecks, duplicate_dc_directives_last_wins_across_modes) {
    // arrange
    const std::vector<std::string> directives = {".DC DEC VIN 1k 100MEG 10", ".DC RLOAD 0 3.5 0.05"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps.size(), 1);
    ASSERT_EQ(result->sweeps[0].variable, "RLOAD");
    ASSERT_EQ(result->sweeps[0].start, "0");
    ASSERT_EQ(result->sweeps[0].stop, "3.5");
    ASSERT_EQ(result->sweeps[0].step, "0.05");
}

TEST(DCSimulationParametersChecks, duplicate_dc_directives_last_wins_list_over_lin) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN 0 5 0.1", ".DC VBIAS LIST 1k 2k 5k"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIST");
    ASSERT_EQ(result->sweeps.size(), 1);
    ASSERT_EQ(result->sweeps[0].variable, "VBIAS");
    ASSERT_EQ(result->sweeps[0].list_values.size(), 3);
    ASSERT_EQ(result->sweeps[0].list_values[0], "1k");
    ASSERT_EQ(result->sweeps[0].list_values[1], "2k");
    ASSERT_EQ(result->sweeps[0].list_values[2], "5k");
}

TEST(DCSimulationParametersChecks, duplicate_dc_directives_last_wins_lin_over_data) {
    // arrange
    const std::vector<std::string> directives = {".DC DATA=myTable", ".DC VDS 0 10 0.5"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->data_table_name.empty(), true);
    ASSERT_EQ(result->sweeps.size(), 1);
    ASSERT_EQ(result->sweeps[0].variable, "VDS");
}

TEST(DCSimulationParametersChecks, duplicate_dc_directives_last_wins_data_over_lin) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN 0 5 0.1", ".DC DATA=sweepData"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DATA");
    ASSERT_EQ(result->data_table_name, "sweepData");
    ASSERT_EQ(result->sweeps.empty(), true);
}

TEST(DCSimulationParametersChecks, duplicate_dc_directives_last_wins_data_over_data) {
    // arrange
    const std::vector<std::string> directives = {".DC DATA=firstTable", ".DC DATA=secondTable"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DATA");
    ASSERT_EQ(result->data_table_name, "secondTable");
}

TEST(DCSimulationParametersChecks, duplicate_dc_directives_serialize_single_line) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN 0 5 0.1", ".DC VDS 0 10 0.5"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    const auto serialized = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(serialized.size(), 1);
    ASSERT_EQ(serialized[0], ".DC VDS 0 10 0.5");
}

// ========================================================================================
// to_xyce_directives
// ========================================================================================

TEST(DCSimulationParametersChecks, generates_lin_directive) {
    // arrange
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC VIN 0 5 0.1");
}

TEST(DCSimulationParametersChecks, generates_dec_directive) {
    // arrange
    const DCSimulationParameters params("DEC", {DcSweep{"VIN", "1k", "100MEG", "", "10"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC DEC VIN 1k 100MEG 10");
}

TEST(DCSimulationParametersChecks, generates_oct_directive) {
    // arrange
    const DCSimulationParameters params("OCT", {DcSweep{"VIN", "1", "1MEG", "", "5"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC OCT VIN 1 1MEG 5");
}

TEST(DCSimulationParametersChecks, generates_list_directive) {
    // arrange
    const DCSimulationParameters params("LIST", {DcSweep{"VIN", "", "", "", "", {"1k", "2k", "5k"}}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC VIN LIST 1k 2k 5k");
}

TEST(DCSimulationParametersChecks, generates_data_directive) {
    // arrange
    const DCSimulationParameters params("DATA", {DcSweep{"", "", "", "", ""}}, "myTable", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC DATA=myTable");
}

TEST(DCSimulationParametersChecks, generates_secondary_sweep) {
    // arrange
    const DCSimulationParameters params("LIN", {DcSweep{"R1", "0", "3.5", "0.05", ""}, DcSweep{"C1", "0", "3.5", "0.5", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC R1 0 3.5 0.05 C1 0 3.5 0.5");
}

TEST(DCSimulationParametersChecks, generates_three_sweeps_roundtrip) {
    // arrange: 3 LIN sweeps
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}, DcSweep{"VOUT", "1", "10", "0.5", ""}, DcSweep{"R1", "100", "200", "10", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC VIN 0 5 0.1 VOUT 1 10 0.5 R1 100 200 10");
    // parse back
    const auto reparsed = DCSimulationParameters::from_xyce_directives({directives[0]});
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->sweep_mode, "LIN");
    ASSERT_EQ(reparsed->sweeps.size(), 3u);
    ASSERT_EQ(reparsed->sweeps[0].variable, "VIN");
    ASSERT_EQ(reparsed->sweeps[0].start, "0");
    ASSERT_EQ(reparsed->sweeps[0].stop, "5");
    ASSERT_EQ(reparsed->sweeps[0].step, "0.1");
    ASSERT_EQ(reparsed->sweeps[1].variable, "VOUT");
    ASSERT_EQ(reparsed->sweeps[1].start, "1");
    ASSERT_EQ(reparsed->sweeps[1].stop, "10");
    ASSERT_EQ(reparsed->sweeps[1].step, "0.5");
    ASSERT_EQ(reparsed->sweeps[2].variable, "R1");
    ASSERT_EQ(reparsed->sweeps[2].start, "100");
    ASSERT_EQ(reparsed->sweeps[2].stop, "200");
    ASSERT_EQ(reparsed->sweeps[2].step, "10");
}

TEST(DCSimulationParametersChecks, generates_lin_directive_without_replace_ground) {
    // arrange
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC VIN 0 5 0.1");
}

TEST(DCSimulationParametersChecks, generates_dec_directive_without_replace_ground) {
    // arrange
    const DCSimulationParameters params("DEC", {DcSweep{"VIN", "1", "100", "", "5"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC DEC VIN 1 100 5");
}

TEST(DCSimulationParametersChecks, generates_oct_directive_without_replace_ground) {
    // arrange
    const DCSimulationParameters params("OCT", {DcSweep{"VIN", "0.125", "64", "", "2"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC OCT VIN 0.125 64 2");
}

TEST(DCSimulationParametersChecks, generates_list_directive_without_replace_ground) {
    // arrange
    const DCSimulationParameters params("LIST", {DcSweep{"TEMP", "", "", "", "", {"10", "15", "18", "27", "33"}}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC TEMP LIST 10 15 18 27 33");
}

TEST(DCSimulationParametersChecks, generates_multiple_list_sweeps) {
    // arrange
    const DCSimulationParameters params("LIST", {DcSweep{"VDS", "", "", "", "", {"0", "3.5", "0.05"}}, DcSweep{"VGS", "", "", "", "", {"0", "3.5", "0.5"}}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC VDS LIST 0 3.5 0.05 VGS LIST 0 3.5 0.5");
}

TEST(DCSimulationParametersChecks, parses_multiple_list_sweeps_roundtrip) {
    // arrange — RG §2.1.3.4 example
    const DCSimulationParameters params("LIST", {DcSweep{"VDS", "", "", "", "", {"0", "3.5", "0.05"}}, DcSweep{"VGS", "", "", "", "", {"0", "3.5", "0.5"}}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    const auto reparsed = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->sweep_mode, "LIST");
    ASSERT_EQ(reparsed->sweeps.size(), 2u);
    ASSERT_EQ(reparsed->sweeps[0].variable, "VDS");
    ASSERT_EQ(reparsed->sweeps[0].list_values, std::vector<std::string>({"0", "3.5", "0.05"}));
    ASSERT_EQ(reparsed->sweeps[1].variable, "VGS");
    ASSERT_EQ(reparsed->sweeps[1].list_values, std::vector<std::string>({"0", "3.5", "0.5"}));
    ASSERT_TRUE(*reparsed == params);
}

TEST(DCSimulationParametersChecks, generates_data_directive_without_replace_ground) {
    // arrange
    const DCSimulationParameters params("DATA", {DcSweep{"", "", "", "", ""}}, "myCustomTable", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC DATA=myCustomTable");
}

TEST(DCSimulationParametersChecks, parses_lin_sweep_with_negative_step) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN 5 0 -0.1"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "5");
    ASSERT_EQ(result->sweeps[0].stop, "0");
    ASSERT_EQ(result->sweeps[0].step, "-0.1");
}

TEST(DCSimulationParametersChecks, parses_dec_sweep_with_secondary) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC DEC VIN 1 100 2 DEC R1 1 10 3"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DEC");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "1");
    ASSERT_EQ(result->sweeps[0].stop, "100");
    ASSERT_EQ(result->sweeps[0].points, "2");
    ASSERT_EQ(result->sweeps[1].variable, "R1");
    ASSERT_EQ(result->sweeps[1].start, "1");
    ASSERT_EQ(result->sweeps[1].stop, "10");
    ASSERT_EQ(result->sweeps[1].points, "3");
}

TEST(DCSimulationParametersChecks, parses_three_dec_sweeps_roundtrip) {
    // arrange
    const DCSimulationParameters params("DEC", {DcSweep{"VIN", "1", "100", "", "2"}, DcSweep{"R1", "1", "10", "", "0.5"}, DcSweep{"C1", "10", "1000", "", "1"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".DC DEC VIN 1 100 2 DEC R1 1 10 0.5 DEC C1 10 1000 1");
    // parse back
    const auto reparsed = DCSimulationParameters::from_xyce_directives({directives[0]});
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->sweep_mode, "DEC");
    ASSERT_EQ(reparsed->sweeps.size(), 3u);
    ASSERT_EQ(reparsed->sweeps[0].variable, "VIN");
    ASSERT_EQ(reparsed->sweeps[0].points, "2");
    ASSERT_EQ(reparsed->sweeps[1].variable, "R1");
    ASSERT_EQ(reparsed->sweeps[1].points, "0.5");
    ASSERT_EQ(reparsed->sweeps[2].variable, "C1");
    ASSERT_EQ(reparsed->sweeps[2].points, "1");
}

TEST(DCSimulationParametersChecks, parses_oct_sweep_with_secondary) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC OCT VIN 0.125 64 2 OCT R1 1 10 4"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "OCT");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "0.125");
    ASSERT_EQ(result->sweeps[0].stop, "64");
    ASSERT_EQ(result->sweeps[0].points, "2");
    ASSERT_EQ(result->sweeps[1].variable, "R1");
    ASSERT_EQ(result->sweeps[1].start, "1");
    ASSERT_EQ(result->sweeps[1].stop, "10");
    ASSERT_EQ(result->sweeps[1].points, "4");
}

TEST(DCSimulationParametersChecks, no_secondary_when_variable_empty) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN 0 5 0.1"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "0");
    ASSERT_EQ(result->sweeps[0].stop, "5");
    ASSERT_EQ(result->sweeps[0].step, "0.1");
    ASSERT_EQ(result->sweeps.size(), 1);
}

TEST(DCSimulationParametersChecks, parses_dec_sweep_with_fractional_step) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC R1 0 3.5 0.05"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "R1");
    ASSERT_EQ(result->sweeps[0].start, "0");
    ASSERT_EQ(result->sweeps[0].stop, "3.5");
    ASSERT_EQ(result->sweeps[0].step, "0.05");
}

TEST(DCSimulationParametersChecks, parses_list_sweep_single_value) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC TEMP LIST 27"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIST");
    ASSERT_EQ(result->sweeps[0].variable, "TEMP");
    ASSERT_EQ(result->sweeps[0].list_values.size(), 1);
    ASSERT_EQ(result->sweeps[0].list_values[0], "27");
}

TEST(DCSimulationParametersChecks, parses_list_sweep_multiple_values) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC TEMP LIST 10 15 18 27 33"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIST");
    ASSERT_EQ(result->sweeps[0].variable, "TEMP");
    ASSERT_EQ(result->sweeps[0].list_values.size(), 5);
    ASSERT_EQ(result->sweeps[0].list_values[0], "10");
    ASSERT_EQ(result->sweeps[0].list_values[1], "15");
    ASSERT_EQ(result->sweeps[0].list_values[2], "18");
    ASSERT_EQ(result->sweeps[0].list_values[3], "27");
    ASSERT_EQ(result->sweeps[0].list_values[4], "33");
}

TEST(DCSimulationParametersChecks, parses_data_sweep_with_spaces) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC DATA=myTable"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DATA");
    ASSERT_EQ(result->data_table_name, "myTable");
}

TEST(DCSimulationParametersChecks, parses_empty_directives_returns_none) {
    // arrange
    const std::vector<std::string> directives = {};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

// ========================================================================================
// to_xyce_directives with print parameters
// ========================================================================================

TEST(DCSimulationParametersChecks, generates_generic_wildcards_round_trip) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"V(*)", "I(*)", "P(*)"}, {});
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", print_params, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    const auto reparsed = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_TRUE(reparsed->print_parameters.has_value());
    ASSERT_EQ(reparsed->print_parameters->print_type, "DC");
    ASSERT_EQ(reparsed->print_parameters->output_variables.size(), 3);
    ASSERT_EQ(reparsed->print_parameters->output_variables[0], "V(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[1], "I(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[2], "P(*)");
}

TEST(DCSimulationParametersChecks, generates_bjt_lead_wildcards_round_trip) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"IB(*)", "IC(*)", "IE(*)", "IS(*)"}, {});
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", print_params, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    const auto reparsed = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_TRUE(reparsed->print_parameters.has_value());
    ASSERT_EQ(reparsed->print_parameters->output_variables.size(), 4);
    ASSERT_EQ(reparsed->print_parameters->output_variables[0], "IB(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[1], "IC(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[2], "IE(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[3], "IS(*)");
}

TEST(DCSimulationParametersChecks, generates_fet_lead_wildcards_round_trip) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"IB(*)", "ID(*)", "IG(*)", "IS(*)"}, {});
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", print_params, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    const auto reparsed = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_TRUE(reparsed->print_parameters.has_value());
    ASSERT_EQ(reparsed->print_parameters->output_variables.size(), 4);
    ASSERT_EQ(reparsed->print_parameters->output_variables[0], "IB(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[1], "ID(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[2], "IG(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[3], "IS(*)");
}

TEST(DCSimulationParametersChecks, w_star_normalizes_to_p_star_on_parse) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN 0 5 0.1", ".PRINT DC W(*)"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->print_parameters.has_value());
    ASSERT_NE(std::find(result->print_parameters->output_variables.begin(), result->print_parameters->output_variables.end(), "P(*)"), result->print_parameters->output_variables.end());
    ASSERT_EQ(std::find(result->print_parameters->output_variables.begin(), result->print_parameters->output_variables.end(), "W(*)"), result->print_parameters->output_variables.end());
}

TEST(DCSimulationParametersChecks, print_directive_uses_dc_not_tran_type) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"V(*)"}, {});
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", print_params, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    bool has_print_dc = false;
    bool has_print_tran = false;
    for (const auto& directive : directives) {
        if (directive.find(".PRINT DC") == 0)
            has_print_dc = true;
        if (directive.find(".PRINT TRAN") == 0)
            has_print_tran = true;
    }
    ASSERT_TRUE(has_print_dc);
    ASSERT_FALSE(has_print_tran);
}

TEST(DCSimulationParametersChecks, generates_with_print_parameters) {
    // arrange
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", PrintParameters("DC", "", "", {"V(*)"}, {}), {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".DC VIN 0 5 0.1");
    ASSERT_EQ(directives[1], ".PRINT DC V(*)");
}

TEST(DCSimulationParametersChecks, generates_with_measure_parameters) {
    // arrange
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {MeasureEntry("DC", "vout", "MAX", "V(OUT)")}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".DC VIN 0 5 0.1");
    ASSERT_EQ(directives[1], ".MEASURE DC vout MAX V(OUT)");
}

TEST(DCSimulationParametersChecks, generates_with_sensitivity) {
    // arrange
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, SensParameter("", "objfunc", {"V(OUT)"}, {"R1:R"}, false, true, std::nullopt), std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 3);
    ASSERT_EQ(directives[0], ".DC VIN 0 5 0.1");
    ASSERT_EQ(directives[1], ".SENS objfunc={V(OUT)} param=R1:R");
    ASSERT_EQ(directives[2], ".OPTIONS SENSITIVITY direct=0 adjoint=1");
}

// ========================================================================================
// equality operator
// ========================================================================================

TEST(DCSimulationParametersChecks, equality_operator_equal_params) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    const DCSimulationParameters params2("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_TRUE(result);
}

TEST(DCSimulationParametersChecks, equality_operator_different_sweep_mode) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    const DCSimulationParameters params2("DEC", {DcSweep{"VIN", "1k", "100MEG", "", "10"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(DCSimulationParametersChecks, equality_operator_different_primary_variable) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    const DCSimulationParameters params2("LIN", {DcSweep{"VOUT", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(DCSimulationParametersChecks, equality_operator_different_start) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    const DCSimulationParameters params2("LIN", {DcSweep{"VIN", "1", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(DCSimulationParametersChecks, equality_operator_different_stop) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    const DCSimulationParameters params2("LIN", {DcSweep{"VIN", "0", "10", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(DCSimulationParametersChecks, equality_operator_different_step) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    const DCSimulationParameters params2("LIN", {DcSweep{"VIN", "0", "5", "0.05", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(DCSimulationParametersChecks, equality_operator_different_print_parameters) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", PrintParameters("DC", "", "", {"V(*)"}, {}), {}, std::nullopt, std::nullopt);
    const DCSimulationParameters params2("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", PrintParameters("DC", "", "", {"I(*)"}, {}), {}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(DCSimulationParametersChecks, equality_operator_different_measure_parameters) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {MeasureEntry("DC", "vout", "MAX", "V(OUT)")}, std::nullopt, std::nullopt);
    const DCSimulationParameters params2("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {MeasureEntry("DC", "iout", "MAX", "I(VOUT)")}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(DCSimulationParametersChecks, equality_operator_different_sensitivity) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, SensParameter("", "objfunc", {"V(OUT)"}, {"R1:R"}, false, true, std::nullopt), std::nullopt);
    const DCSimulationParameters params2("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, SensParameter("", "objfunc", {"V(IN)"}, {"R1:R"}, false, true, std::nullopt), std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

// ========================================================================================
// Tests for from_xyce_directives edge cases
// ========================================================================================

TEST(DCSimulationParametersChecks, empty_directives_returns_none) {
    // arrange
    const std::vector<std::string> directives = {};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(DCSimulationParametersChecks, blank_directive_string_is_skipped) {
    // arrange
    const std::vector<std::string> directives = {""};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(DCSimulationParametersChecks, bare_dc_with_no_arguments_is_skipped) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_TRUE(result->sweeps.empty());
}

TEST(DCSimulationParametersChecks, non_dc_directives_are_ignored) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1ns 100ns", ".OP"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(DCSimulationParametersChecks, parses_lin_implicit) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN -10 15 1"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "-10");
    ASSERT_EQ(result->sweeps[0].stop, "15");
    ASSERT_EQ(result->sweeps[0].step, "1");
}

TEST(DCSimulationParametersChecks, parses_lin_implicit_with_secondary) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC R1 0 3.5 0.05 C1 0 3.5 0.5"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "R1");
    ASSERT_EQ(result->sweeps[1].variable, "C1");
    ASSERT_EQ(result->sweeps[1].start, "0");
    ASSERT_EQ(result->sweeps[1].stop, "3.5");
    ASSERT_EQ(result->sweeps[1].step, "0.5");
}

TEST(DCSimulationParametersChecks, parses_lin_explicit) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC LIN V1 5 25 5"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "V1");
    ASSERT_EQ(result->sweeps[0].start, "5");
    ASSERT_EQ(result->sweeps[0].stop, "25");
    ASSERT_EQ(result->sweeps[0].step, "5");
}

TEST(DCSimulationParametersChecks, parses_lin_explicit_with_secondary) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC LIN R1 0 3.5 0.05 C1 0 3.5 0.5"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "R1");
    ASSERT_EQ(result->sweeps[1].variable, "C1");
    ASSERT_EQ(result->sweeps[1].start, "0");
    ASSERT_EQ(result->sweeps[1].stop, "3.5");
    ASSERT_EQ(result->sweeps[1].step, "0.5");
}

TEST(DCSimulationParametersChecks, parses_dec) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC DEC VIN 1 100 2"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DEC");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "1");
    ASSERT_EQ(result->sweeps[0].stop, "100");
    ASSERT_EQ(result->sweeps[0].points, "2");
}

TEST(DCSimulationParametersChecks, parses_dec_with_secondary) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC DEC R1 100 10000 3 DEC VGS 0.001 1.0 2"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DEC");
    ASSERT_EQ(result->sweeps[0].variable, "R1");
    ASSERT_EQ(result->sweeps[1].variable, "VGS");
    ASSERT_EQ(result->sweeps[1].start, "0.001");
    ASSERT_EQ(result->sweeps[1].stop, "1.0");
    ASSERT_EQ(result->sweeps[1].points, "2");
}

TEST(DCSimulationParametersChecks, parses_oct) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC OCT VIN 0.125 64 2"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "OCT");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "0.125");
    ASSERT_EQ(result->sweeps[0].stop, "64");
    ASSERT_EQ(result->sweeps[0].points, "2");
}

TEST(DCSimulationParametersChecks, parses_oct_with_secondary) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC OCT R1 0.015625 512 3 OCT C1 512 4096 1"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "OCT");
    ASSERT_EQ(result->sweeps[0].variable, "R1");
    ASSERT_EQ(result->sweeps[1].variable, "C1");
    ASSERT_EQ(result->sweeps[1].start, "512");
    ASSERT_EQ(result->sweeps[1].stop, "4096");
    ASSERT_EQ(result->sweeps[1].points, "1");
}

TEST(DCSimulationParametersChecks, parses_list) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN LIST 1.0 2.0 5.0"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIST");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].list_values.size(), 3);
    ASSERT_EQ(result->sweeps[0].list_values[0], "1.0");
    ASSERT_EQ(result->sweeps[0].list_values[1], "2.0");
    ASSERT_EQ(result->sweeps[0].list_values[2], "5.0");
}

TEST(DCSimulationParametersChecks, parses_data) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC DATA=resistorValues"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DATA");
    ASSERT_EQ(result->data_table_name, "resistorValues");
}

TEST(DCSimulationParametersChecks, serializes_print_dc_directive) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"V(OUT)"}, {});
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", print_params, {}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".DC VIN 0 5 0.1");
    ASSERT_EQ(directives[1], ".PRINT DC V(OUT)");
}

TEST(DCSimulationParametersChecks, parses_print_dc_directive) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN 0 5 0.1", ".PRINT DC V(OUT) I(V1)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_parameters.has_value(), true);
    ASSERT_EQ(result->print_parameters->print_type, "DC");
    ASSERT_EQ(result->print_parameters->output_variables.size(), 2);
    ASSERT_EQ(result->print_parameters->output_variables[0], "V(OUT)");
    ASSERT_EQ(result->print_parameters->output_variables[1], "I(V1)");
}

TEST(DCSimulationParametersChecks, ignores_non_dc_print_directive) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN 0 5 0.1", ".PRINT TRAN V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_parameters.has_value(), false);
}

// ========================================================================================
// Tests for measure parameters
// ========================================================================================

TEST(DCSimulationParametersChecks, parses_single_measure_directive) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN 0 5 0.1", ".MEASURE DC vout_at_2v FIND V(OUT) WHEN VIN=2"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_parameters.size(), 1);
    ASSERT_EQ(result->measure_parameters[0].result_name, "vout_at_2v");
    ASSERT_EQ(result->measure_parameters[0].measure_type, "FIND");
    ASSERT_EQ(result->measure_parameters[0].analysis_type, "DC");
    ASSERT_EQ(result->measure_parameters[0].variable, "V(OUT)");
}

TEST(DCSimulationParametersChecks, parses_multiple_measure_directives) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN 0 5 0.1", ".MEASURE DC vout_at_2v FIND V(OUT) WHEN VIN=2", ".MEASURE DC vout_at_4v FIND V(OUT) WHEN VIN=4"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_parameters.size(), 2);
    ASSERT_EQ(result->measure_parameters[0].result_name, "vout_at_2v");
    ASSERT_EQ(result->measure_parameters[1].result_name, "vout_at_4v");
}

TEST(DCSimulationParametersChecks, ignores_non_dc_measure_directive) {
    // arrange / act
    const auto result = DCSimulationParameters::from_xyce_directives({".DC VIN 0 5 0.1", ".MEASURE TRAN avg_out AVG V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_parameters.size(), 0);
}

TEST(DCSimulationParametersChecks, emits_single_measure_directive) {
    // arrange
    const MeasureEntry measure("DC", "vout_at_2v", "FIND", "V(OUT)", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "VIN", "=2");
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {measure}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[1], ".MEASURE DC vout_at_2v FIND V(OUT) WHEN VIN=2");
}

TEST(DCSimulationParametersChecks, emits_multiple_measure_directives) {
    // arrange
    const MeasureEntry measure1("DC", "vout_at_2v", "FIND", "V(OUT)", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "VIN", "=2");
    const MeasureEntry measure2("DC", "vout_at_4v", "FIND", "V(OUT)", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "VIN", "=4");
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {measure1, measure2}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 3);
    ASSERT_EQ(directives[1], ".MEASURE DC vout_at_2v FIND V(OUT) WHEN VIN=2");
    ASSERT_EQ(directives[2], ".MEASURE DC vout_at_4v FIND V(OUT) WHEN VIN=4");
}

TEST(DCSimulationParametersChecks, measure_round_trip) {
    // arrange
    const MeasureEntry measure("DC", "vout_at_2v", "FIND", "V(OUT)", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "VIN", "=2");
    const DCSimulationParameters params("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {measure}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    const auto reparsed = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->measure_parameters.size(), 1);
    ASSERT_EQ(reparsed->measure_parameters[0].result_name, "vout_at_2v");
    ASSERT_EQ(reparsed->measure_parameters[0].measure_type, "FIND");
    ASSERT_EQ(reparsed->measure_parameters[0].analysis_type, "DC");
}

// ========================================================================================
// Tests for reference guide examples
// ========================================================================================

TEST(DCSimulationParametersChecks, reference_guide_example_lin_sweep) {
    // arrange
    const std::vector<std::string> directives = {".DC LIN V1 5 25 5"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "V1");
    ASSERT_EQ(result->sweeps[0].start, "5");
    ASSERT_EQ(result->sweeps[0].stop, "25");
    ASSERT_EQ(result->sweeps[0].step, "5");
    // verify the directive contains the expected dc line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_NE(generated[0].find("V1 5 25 5"), std::string::npos);
}

TEST(DCSimulationParametersChecks, reference_guide_example_lin_implicit) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN -10 15 1"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "-10");
    ASSERT_EQ(result->sweeps[0].stop, "15");
    ASSERT_EQ(result->sweeps[0].step, "1");
    // verify the directive contains the expected dc line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_EQ(generated[0], ".DC VIN -10 15 1");
}

TEST(DCSimulationParametersChecks, reference_guide_example_lin_with_secondary) {
    // arrange
    const std::vector<std::string> directives = {".DC R1 0 3.5 0.05 C1 0 3.5 0.5"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->sweeps[0].variable, "R1");
    ASSERT_EQ(result->sweeps[1].variable, "C1");
    // verify the directive contains the expected dc line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_EQ(generated[0], ".DC R1 0 3.5 0.05 C1 0 3.5 0.5");
}

TEST(DCSimulationParametersChecks, reference_guide_example_dec_sweep) {
    // arrange
    const std::vector<std::string> directives = {".DC DEC VIN 1 100 2"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DEC");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "1");
    ASSERT_EQ(result->sweeps[0].stop, "100");
    ASSERT_EQ(result->sweeps[0].points, "2");
    // verify the directive contains the expected dc line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_EQ(generated[0], ".DC DEC VIN 1 100 2");
}

TEST(DCSimulationParametersChecks, reference_guide_example_dec_with_secondary) {
    // arrange
    const std::vector<std::string> directives = {".DC DEC R1 100 10000 3 DEC VGS 0.001 1.0 2"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DEC");
    ASSERT_EQ(result->sweeps[0].variable, "R1");
    ASSERT_EQ(result->sweeps[1].variable, "VGS");
    // verify the directive contains the expected dc line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_NE(generated[0].find("R1 100 10000 3"), std::string::npos);
    ASSERT_NE(generated[0].find("VGS 0.001 1.0 2"), std::string::npos);
}

TEST(DCSimulationParametersChecks, reference_guide_example_oct_sweep) {
    // arrange
    const std::vector<std::string> directives = {".DC OCT VIN 0.125 64 2"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "OCT");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].start, "0.125");
    ASSERT_EQ(result->sweeps[0].stop, "64");
    ASSERT_EQ(result->sweeps[0].points, "2");
    // verify the directive contains the expected dc line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_EQ(generated[0], ".DC OCT VIN 0.125 64 2");
}

TEST(DCSimulationParametersChecks, reference_guide_example_oct_with_secondary) {
    // arrange
    const std::vector<std::string> directives = {".DC OCT R1 0.015625 512 3 OCT C1 512 4096 1"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "OCT");
    ASSERT_EQ(result->sweeps[0].variable, "R1");
    ASSERT_EQ(result->sweeps[1].variable, "C1");
    // verify the directive contains the expected dc line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_NE(generated[0].find("R1 0.015625 512 3"), std::string::npos);
    ASSERT_NE(generated[0].find("C1 512 4096 1"), std::string::npos);
}

TEST(DCSimulationParametersChecks, reference_guide_example_list_sweep) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN LIST 1.0 2.0 5.0 6.0 10.0"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIST");
    ASSERT_EQ(result->sweeps[0].variable, "VIN");
    ASSERT_EQ(result->sweeps[0].list_values.size(), 5);
    ASSERT_EQ(result->sweeps[0].list_values[0], "1.0");
    ASSERT_EQ(result->sweeps[0].list_values[1], "2.0");
    ASSERT_EQ(result->sweeps[0].list_values[2], "5.0");
    ASSERT_EQ(result->sweeps[0].list_values[3], "6.0");
    ASSERT_EQ(result->sweeps[0].list_values[4], "10.0");
    // verify the directive contains the expected dc line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_EQ(generated[0], ".DC VIN LIST 1.0 2.0 5.0 6.0 10.0");
}

TEST(DCSimulationParametersChecks, reference_guide_example_data_sweep) {
    // arrange
    const std::vector<std::string> directives = {".DC DATA=resistorValues"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DATA");
    ASSERT_EQ(result->data_table_name, "resistorValues");
    // verify the directive contains the expected dc line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_EQ(generated[0], ".DC DATA=resistorValues");
}

// ========================================================================================
// validate
// ========================================================================================

TEST(DCSimulationParametersChecks, validate_lin_sweep_valid) {
    // arrange
    const DCSimulationParameters params("LIN", {DcSweep{"V1", "0", "5", "0.5", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_FALSE(error.has_value());
}

TEST(DCSimulationParametersChecks, validate_lin_sweep_missing_step) {
    // arrange
    const DCSimulationParameters params("LIN", {DcSweep{"V1", "0", "5", "", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_TRUE(error.has_value());
    ASSERT_NE(error->find("step"), std::string::npos);
}

TEST(DCSimulationParametersChecks, validate_lin_sweep_missing_variable) {
    // arrange
    const DCSimulationParameters params("LIN", {DcSweep{"", "0", "5", "0.5", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_TRUE(error.has_value());
}

TEST(DCSimulationParametersChecks, validate_dec_sweep_valid) {
    // arrange
    const DCSimulationParameters params("DEC", {DcSweep{"V1", "1", "100", "", "2"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_FALSE(error.has_value());
}

TEST(DCSimulationParametersChecks, validate_dec_sweep_fractional_points) {
    // arrange — a LIN step value carried over to a DEC sweep is not a valid point count
    const DCSimulationParameters params("DEC", {DcSweep{"V1", "0", "5", "", "0.5"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_TRUE(error.has_value());
    ASSERT_NE(error->find("positive integer"), std::string::npos);
}

TEST(DCSimulationParametersChecks, validate_dec_sweep_zero_start) {
    // arrange — a log sweep cannot start at zero
    const DCSimulationParameters params("DEC", {DcSweep{"V1", "0", "5", "", "2"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_TRUE(error.has_value());
    ASSERT_NE(error->find("greater than zero"), std::string::npos);
}

TEST(DCSimulationParametersChecks, validate_oct_sweep_zero_points) {
    // arrange
    const DCSimulationParameters params("OCT", {DcSweep{"V1", "0.125", "64", "", "0"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_TRUE(error.has_value());
    ASSERT_NE(error->find("positive integer"), std::string::npos);
}

TEST(DCSimulationParametersChecks, validate_dec_sweep_missing_points) {
    // arrange — a DEC sweep with an empty points value is incomplete
    const DCSimulationParameters params("DEC", {DcSweep{"V1", "1", "100", "", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_TRUE(error.has_value());
    ASSERT_NE(error->find("points"), std::string::npos);
}

TEST(DCSimulationParametersChecks, validate_list_sweep_valid) {
    // arrange
    const DCSimulationParameters params("LIST", {DcSweep{"V1", "", "", "", "", {"0.5", "1.0"}}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_FALSE(error.has_value());
}

TEST(DCSimulationParametersChecks, validate_list_sweep_missing_values) {
    // arrange
    const DCSimulationParameters params("LIST", {DcSweep{"V1", "", "", "", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_TRUE(error.has_value());
    ASSERT_NE(error->find("at least one value"), std::string::npos);
}

TEST(DCSimulationParametersChecks, validate_data_sweep_valid) {
    // arrange
    const DCSimulationParameters params("DATA", {DcSweep{"", "", "", "", ""}}, "myTable", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_FALSE(error.has_value());
}

TEST(DCSimulationParametersChecks, validate_data_sweep_missing_table) {
    // arrange
    const DCSimulationParameters params("DATA", {DcSweep{"", "", "", "", ""}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_TRUE(error.has_value());
}

TEST(DCSimulationParametersChecks, validate_dec_secondary_sweep_invalid) {
    // arrange — secondary sweep with a non-integer point count
    const DCSimulationParameters params("DEC", {DcSweep{"V1", "1", "100", "", "2"}, DcSweep{"R1", "1", "10", "", "0.5"}}, "", std::nullopt, {}, std::nullopt, std::nullopt);
    // act
    const auto error = params.validate();
    // assert
    ASSERT_TRUE(error.has_value());
    ASSERT_NE(error->find("positive integer"), std::string::npos);
}

TEST(DCSimulationParametersChecks, claims_pce_companion_directives) {
    // arrange
    const std::vector<std::string> directives = {
        ".DC VIN 0 5 0.1",
        ".PCE param=R1 type=normal means=3K std_deviations=1K",
        ".OPTIONS PCES OUTPUTS={V(1)}",
        ".PRINT PCE V(1)",
    };
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->pce.has_value());
    ASSERT_EQ(result->pce->parameters.size(), 1);
    ASSERT_EQ(result->pce->parameters[0], "R1");
    // round-trip through directives
    const auto regenerated = result->to_xyce_directives(NetlistTopology{});
    const auto reparsed = DCSimulationParameters::from_xyce_directives(regenerated);
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(*reparsed, *result);
}

TEST(DCSimulationParametersChecks, no_pce_directives_leave_pce_empty) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN 0 5 0.1"};
    // act
    const auto result = DCSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->pce.has_value());
}

TEST(DCSimulationParametersChecks, equality_operator_different_pce) {
    // arrange
    const DCSimulationParameters params1("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, PceParameters(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, std::nullopt));
    const DCSimulationParameters params2("LIN", {DcSweep{"VIN", "0", "5", "0.1", ""}}, "", std::nullopt, {}, std::nullopt, PceParameters(false, {"R2"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, std::nullopt));
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

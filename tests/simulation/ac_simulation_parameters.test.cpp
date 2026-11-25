#include <algorithm>
#include <gtest/gtest.h>

#include "netlist/netlist.h"
#include "simulation/ac_simulation_parameters.h"

// from_xyce_directives

TEST(AcSimulationParametersChecks, parses_lin_sweep) {
    // arrange / act
    const auto result = AcSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->points, "100");
    ASSERT_EQ(result->start, "1");
    ASSERT_EQ(result->end, "1MEG");
}

TEST(AcSimulationParametersChecks, parses_dec_sweep) {
    // arrange / act
    const auto result = AcSimulationParameters::from_xyce_directives({".AC DEC 10 1k 100MEG"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DEC");
    ASSERT_EQ(result->points, "10");
    ASSERT_EQ(result->start, "1k");
    ASSERT_EQ(result->end, "100MEG");
}

TEST(AcSimulationParametersChecks, parses_oct_sweep) {
    // arrange / act
    const auto result = AcSimulationParameters::from_xyce_directives({".AC OCT 5 1 1MEG"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "OCT");
    ASSERT_EQ(result->points, "5");
    ASSERT_EQ(result->start, "1");
    ASSERT_EQ(result->end, "1MEG");
}

TEST(AcSimulationParametersChecks, parses_data_sweep) {
    // arrange / act
    const auto result = AcSimulationParameters::from_xyce_directives({".AC DATA=myTable"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DATA");
    ASSERT_EQ(result->data_table_name, "myTable");
}

TEST(AcSimulationParametersChecks, parses_implicit_lin_sweep) {
    // arrange / act (no LIN keyword)
    const auto result = AcSimulationParameters::from_xyce_directives({".AC 100 1 1MEG"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->points, "100");
    ASSERT_EQ(result->start, "1");
    ASSERT_EQ(result->end, "1MEG");
}

TEST(AcSimulationParametersChecks, no_ac_directive_returns_none) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1u 1m"};
    // act
    const auto result = AcSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

// to_xyce_directives

TEST(AcSimulationParametersChecks, generates_lin_directive) {
    // arrange
    const AcSimulationParameters params("LIN", "100", "1", "1MEG", "", std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".AC LIN 100 1 1MEG");
}

TEST(AcSimulationParametersChecks, generates_dec_directive) {
    // arrange
    const AcSimulationParameters params("DEC", "10", "1k", "100MEG", "", std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".AC DEC 10 1k 100MEG");
}

TEST(AcSimulationParametersChecks, generates_oct_directive) {
    // arrange
    const AcSimulationParameters params("OCT", "5", "1", "1MEG", "", std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".AC OCT 5 1 1MEG");
}

TEST(AcSimulationParametersChecks, generates_data_directive) {
    // arrange
    const AcSimulationParameters params("DATA", "", "", "", "myTable", std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".AC DATA=myTable");
}

TEST(AcSimulationParametersChecks, generates_with_print_parameters) {
    // arrange
    const AcSimulationParameters params("LIN", "100", "1", "1MEG", "", PrintParameters("AC", "", "", {"V(*)"}, {}), std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".AC LIN 100 1 1MEG");
    ASSERT_EQ(directives[1], ".PRINT AC V(*)");
}

TEST(AcSimulationParametersChecks, generates_with_sensitivity) {
    // arrange
    const AcSimulationParameters params("LIN", "100", "1", "1MEG", "", std::nullopt, SensParameter("", "objfunc", {"V(OUT)"}, {"R1:R"}, false, true, std::nullopt));
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 3);
    ASSERT_EQ(directives[0], ".AC LIN 100 1 1MEG");
    ASSERT_EQ(directives[1], ".SENS objfunc={V(OUT)} param=R1:R");
    ASSERT_EQ(directives[2], ".OPTIONS SENSITIVITY direct=0 adjoint=1");
}

// equality operator

TEST(AcSimulationParametersChecks, equality_operator_equal_params) {
    // arrange
    const AcSimulationParameters params1("LIN", "100", "1", "1MEG", "", std::nullopt, std::nullopt);
    const AcSimulationParameters params2("LIN", "100", "1", "1MEG", "", std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_TRUE(result);
}

TEST(AcSimulationParametersChecks, equality_operator_different_sweep_mode) {
    // arrange
    const AcSimulationParameters params1("LIN", "100", "1", "1MEG", "", std::nullopt, std::nullopt);
    const AcSimulationParameters params2("DEC", "10", "1k", "100MEG", "", std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(AcSimulationParametersChecks, equality_operator_different_points) {
    // arrange
    const AcSimulationParameters params1("LIN", "100", "1", "1MEG", "", std::nullopt, std::nullopt);
    const AcSimulationParameters params2("LIN", "10", "1", "1MEG", "", std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(AcSimulationParametersChecks, equality_operator_different_start) {
    // arrange
    const AcSimulationParameters params1("LIN", "100", "1", "1MEG", "", std::nullopt, std::nullopt);
    const AcSimulationParameters params2("LIN", "100", "10", "1MEG", "", std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(AcSimulationParametersChecks, equality_operator_different_end) {
    // arrange
    const AcSimulationParameters params1("LIN", "100", "1", "1MEG", "", std::nullopt, std::nullopt);
    const AcSimulationParameters params2("LIN", "100", "1", "100MEG", "", std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(AcSimulationParametersChecks, equality_operator_different_print_parameters) {
    // arrange
    const AcSimulationParameters params1("LIN", "100", "1", "1MEG", "", PrintParameters("AC", "", "", {"V(*)"}, {}), std::nullopt);
    const AcSimulationParameters params2("LIN", "100", "1", "1MEG", "", PrintParameters("AC", "", "", {"I(*)"}, {}), std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(AcSimulationParametersChecks, equality_operator_different_sensitivity) {
    // arrange
    const AcSimulationParameters params1("LIN", "100", "1", "1MEG", "", std::nullopt, SensParameter("", "objfunc", {"V(OUT)"}, {"R1:R"}, false, true, std::nullopt));
    const AcSimulationParameters params2("LIN", "100", "1", "1MEG", "", std::nullopt, SensParameter("", "objfunc", {"V(IN)"}, {"R1:R"}, false, true, std::nullopt));
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

// Tests for from_xyce_directives edge cases

TEST(AcSimulationParametersChecks, empty_directives_returns_none) {
    // arrange
    const std::vector<std::string> directives = {};
    // act
    const auto result = AcSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(AcSimulationParametersChecks, non_ac_directives_return_none) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1ns 1ms"};
    // act
    const auto result = AcSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(AcSimulationParametersChecks, parses_print_ac_directive) {
    // arrange / act
    const auto result = AcSimulationParameters::from_xyce_directives({".AC LIN 10 1 1MEG", ".PRINT AC V(OUT) I(V1)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_parameters.has_value(), true);
    ASSERT_EQ(result->print_parameters->print_type, "AC");
    ASSERT_EQ(result->print_parameters->output_variables.size(), 2);
    ASSERT_EQ(result->print_parameters->output_variables[0], "V(OUT)");
    ASSERT_EQ(result->print_parameters->output_variables[1], "I(V1)");
}

TEST(AcSimulationParametersChecks, ignores_non_ac_print_directive) {
    // arrange / act
    const auto result = AcSimulationParameters::from_xyce_directives({".AC LIN 10 1 1MEG", ".PRINT TRAN V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_parameters.has_value(), false);
}

// Tests for reference guide examples

TEST(AcSimulationParametersChecks, reference_guide_example_lin_sweep) {
    // arrange
    const std::vector<std::string> directives = {".AC LIN 101 100Hz 200Hz"};
    // act
    const auto result = AcSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "LIN");
    ASSERT_EQ(result->points, "101");
    ASSERT_EQ(result->start, "100Hz");
    ASSERT_EQ(result->end, "200Hz");
    // verify the directive contains the expected ac line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_GE(generated.size(), 1);
    ASSERT_NE(std::find(generated.begin(), generated.end(), ".AC LIN 101 100Hz 200Hz"), generated.end());
}

TEST(AcSimulationParametersChecks, reference_guide_example_oct_sweep) {
    // arrange
    const std::vector<std::string> directives = {".AC OCT 10 1kHz 16kHz"};
    // act
    const auto result = AcSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "OCT");
    ASSERT_EQ(result->points, "10");
    ASSERT_EQ(result->start, "1kHz");
    ASSERT_EQ(result->end, "16kHz");
    // verify the directive contains the expected ac line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_GE(generated.size(), 1);
    ASSERT_NE(std::find(generated.begin(), generated.end(), ".AC OCT 10 1kHz 16kHz"), generated.end());
}

TEST(AcSimulationParametersChecks, reference_guide_example_dec_sweep) {
    // arrange
    const std::vector<std::string> directives = {".AC DEC 20 1MEG 100MEG"};
    // act
    const auto result = AcSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DEC");
    ASSERT_EQ(result->points, "20");
    ASSERT_EQ(result->start, "1MEG");
    ASSERT_EQ(result->end, "100MEG");
    // verify the directive contains the expected ac line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_GE(generated.size(), 1);
    ASSERT_NE(std::find(generated.begin(), generated.end(), ".AC DEC 20 1MEG 100MEG"), generated.end());
}

TEST(AcSimulationParametersChecks, reference_guide_example_data_sweep) {
    // arrange
    const std::vector<std::string> directives = {".AC DATA=myTable"};
    // act
    const auto result = AcSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sweep_mode, "DATA");
    ASSERT_EQ(result->data_table_name, "myTable");
    // verify the directive contains the expected ac line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_GE(generated.size(), 1);
    ASSERT_NE(std::find(generated.begin(), generated.end(), ".AC DATA=myTable"), generated.end());
}

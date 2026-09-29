#include <gtest/gtest.h>

#include "netlist/netlist.h"
#include "simulation/sens_parameter.h"

// ========================================================================================
// from_xyce_directives
// ========================================================================================

TEST(SensParameterChecks, parse_and_serialize_sens_directive) {
    // arrange
    const std::vector<std::string> directives = {
        ".SENS objfunc={V(2)} param=R1:R,C1:C",
        ".OPTIONS SENSITIVITY direct=0 adjoint=1",
        ".PREPROCESS REPLACEGROUND TRUE",
    };
    // act
    const auto result = SensParameter::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_context, "");
    ASSERT_EQ(result->objective_mode, "objfunc");
    ASSERT_EQ(result->objective_values.size(), 1);
    ASSERT_EQ(result->objective_values[0], "V(2)");
    ASSERT_EQ(result->parameter_list.size(), 2);
    ASSERT_EQ(result->parameter_list[0], "R1:R");
    ASSERT_EQ(result->parameter_list[1], "C1:C");
    ASSERT_EQ(result->direct, false);
    ASSERT_EQ(result->adjoint, true);
}

TEST(SensParameterChecks, parse_sens_with_objvars) {
    // arrange
    const std::vector<std::string> directives = {
        ".SENS objvars=V(2) param=R1:R",
        ".OPTIONS SENSITIVITY direct=1 adjoint=0",
    };
    // act
    const auto result = SensParameter::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->objective_mode, "objvars");
    ASSERT_EQ(result->objective_values.size(), 1);
    ASSERT_EQ(result->objective_values[0], "V(2)");
    ASSERT_EQ(result->parameter_list.size(), 1);
    ASSERT_EQ(result->parameter_list[0], "R1:R");
    ASSERT_EQ(result->direct, true);
    ASSERT_EQ(result->adjoint, false);
}

TEST(SensParameterChecks, parse_sens_without_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".SENS objfunc={V(2)} param=R1:R",
    };
    // act
    const auto result = SensParameter::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->direct, false);
    ASSERT_EQ(result->adjoint, false);
}

TEST(SensParameterChecks, parse_sens_with_multiple_objective_values) {
    // arrange
    const std::vector<std::string> directives = {
        ".SENS objfunc={V(2)} param=R1:R,C1:C,R2:R",
        ".OPTIONS SENSITIVITY direct=0 adjoint=1",
    };
    // act
    const auto result = SensParameter::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->objective_values.size(), 1);
    ASSERT_EQ(result->parameter_list.size(), 3);
    ASSERT_EQ(result->parameter_list[0], "R1:R");
    ASSERT_EQ(result->parameter_list[1], "C1:C");
    ASSERT_EQ(result->parameter_list[2], "R2:R");
}

TEST(SensParameterChecks, parse_sens_with_print_parameters) {
    // arrange
    const std::vector<std::string> directives = {
        ".SENS objfunc={V(2)} param=R1:R",
        ".PRINT SENS V(OUT)",
        ".OPTIONS SENSITIVITY direct=0 adjoint=1",
    };
    // act
    const auto result = SensParameter::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->print_parameters.has_value());
    ASSERT_EQ(result->print_parameters->print_type, "SENS");
    ASSERT_EQ(result->print_parameters->output_variables.size(), 1);
    ASSERT_EQ(result->print_parameters->output_variables[0], "V(OUT)");
}

TEST(SensParameterChecks, no_sens_directive_returns_none) {
    // arrange
    const std::vector<std::string> directives = {
        ".TRAN 1u 1m",
    };
    // act
    const auto result = SensParameter::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(SensParameterChecks, preserves_extra_sensitivity_option_keys) {
    // arrange
    const std::vector<std::string> directives = {
        ".SENS objfunc={V(2)} param=R1:R",
        ".OPTIONS SENSITIVITY direct=1 adjoint=0 OUTPUTUNSCALED=false ADJOINTBEGINTIME=0",
    };
    // act
    const auto result = SensParameter::from_xyce_directives(directives);
    // assert: every RG 2.1.25.16 key is captured verbatim beyond the direct/adjoint flags
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->option_tokens.size(), 4);
    ASSERT_EQ(result->option_tokens[0], "direct=1");
    ASSERT_EQ(result->option_tokens[1], "adjoint=0");
    ASSERT_EQ(result->option_tokens[2], "OUTPUTUNSCALED=false");
    ASSERT_EQ(result->option_tokens[3], "ADJOINTBEGINTIME=0");
}

// ========================================================================================
// to_xyce_directives
// ========================================================================================

TEST(SensParameterChecks, generate_sens_directive) {
    // arrange
    const SensParameter params("", "objfunc", {"V(2)"}, {"R1:R", "C1:C"}, false, true, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[1], ".OPTIONS SENSITIVITY direct=0 adjoint=1");
    ASSERT_EQ(directives[0], ".SENS objfunc={V(2)} param=R1:R,C1:C");
}

TEST(SensParameterChecks, generate_sens_with_options) {
    // arrange
    const SensParameter params("", "objfunc", {"V(2)"}, {"R1:R", "C1:C"}, false, true, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[1], ".OPTIONS SENSITIVITY direct=0 adjoint=1");
    ASSERT_EQ(directives[0], ".SENS objfunc={V(2)} param=R1:R,C1:C");
}

TEST(SensParameterChecks, generate_sens_with_print_parameters) {
    // arrange
    const SensParameter params("", "objfunc", {"V(2)"}, {"R1:R"}, false, true, PrintParameters("SENS", "", "", {"V(OUT)"}, {}));
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 3);
    ASSERT_EQ(directives[0], ".SENS objfunc={V(2)} param=R1:R");
    ASSERT_EQ(directives[1], ".OPTIONS SENSITIVITY direct=0 adjoint=1");
    ASSERT_EQ(directives[2], ".PRINT SENS V(OUT)");
}

TEST(SensParameterChecks, round_trip_sensitivity_options_line) {
    // arrange
    const std::vector<std::string> input = {
        ".SENS objfunc={V(2)} param=R1:R",
        ".OPTIONS SENSITIVITY direct=1 adjoint=0 OUTPUTUNSCALED=false",
    };
    // act
    const auto result = SensParameter::from_xyce_directives(input);
    ASSERT_TRUE(result.has_value());
    const auto directives = result->to_xyce_directives(NetlistTopology{});
    // assert: the options statement round-trips unchanged with every key intact
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[1], ".OPTIONS SENSITIVITY direct=1 adjoint=0 OUTPUTUNSCALED=false");
}

TEST(SensParameterChecks, passes_through_print_wildcards_with_topology) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\nR2 2 0 200\n.END\n");
    const SensParameter params("", "objfunc", {"V(2)"}, {"R1:R"}, false, true, PrintParameters("SENS", "", "", {"V(*)", "I(*)"}, {}));
    // act
    const auto directives = params.to_xyce_directives(topology);
    // assert
    ASSERT_EQ(directives.size(), 3);
    // V(*)/I(*) pass through to Xyce for native expansion
    ASSERT_NE(directives[2].find("V(*)"), std::string::npos);
    ASSERT_NE(directives[2].find("I(*)"), std::string::npos);
    // topology nodes/devices are NOT injected by the plugin
    ASSERT_EQ(directives[2].find("V(0)"), std::string::npos);
    ASSERT_EQ(directives[2].find("V(1)"), std::string::npos);
    ASSERT_EQ(directives[2].find("I(R1)"), std::string::npos);
}

// ========================================================================================
// equality operator
// ========================================================================================

TEST(SensParameterChecks, equality_operator_equal_params) {
    // arrange
    const SensParameter params1("", "objfunc", {"V(2)"}, {"R1:R", "C1:C"}, false, true, std::nullopt);
    const SensParameter params2("", "objfunc", {"V(2)"}, {"R1:R", "C1:C"}, false, true, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_TRUE(result);
}

TEST(SensParameterChecks, equality_operator_different_objective_mode) {
    // arrange
    const SensParameter params1("", "objfunc", {"V(2)"}, {"R1:R"}, false, true, std::nullopt);
    const SensParameter params2("", "objvars", {"V(2)"}, {"R1:R"}, false, true, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(SensParameterChecks, equality_operator_different_objective_values) {
    // arrange
    const SensParameter params1("", "objfunc", {"V(2)"}, {"R1:R"}, false, true, std::nullopt);
    const SensParameter params2("", "objfunc", {"{V(3)}"}, {"R1:R"}, false, true, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(SensParameterChecks, equality_operator_different_parameter_list) {
    // arrange
    const SensParameter params1("", "objfunc", {"V(2)"}, {"R1:R"}, false, true, std::nullopt);
    const SensParameter params2("", "objfunc", {"V(2)"}, {"C1:C"}, false, true, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(SensParameterChecks, equality_operator_different_direct) {
    // arrange
    const SensParameter params1("", "objfunc", {"V(2)"}, {"R1:R"}, false, true, std::nullopt);
    const SensParameter params2("", "objfunc", {"V(2)"}, {"R1:R"}, true, true, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(SensParameterChecks, equality_operator_different_adjoint) {
    // arrange
    const SensParameter params1("", "objfunc", {"V(2)"}, {"R1:R"}, false, true, std::nullopt);
    const SensParameter params2("", "objfunc", {"V(2)"}, {"R1:R"}, false, false, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(SensParameterChecks, equality_operator_different_print_parameters) {
    // arrange
    const SensParameter params1("", "objfunc", {"V(2)"}, {"R1:R"}, false, true, PrintParameters("TRAN", "", "", {"V(OUT)"}, {}));
    const SensParameter params2("", "objfunc", {"V(2)"}, {"R1:R"}, false, true, PrintParameters("TRAN", "", "", {"I(V1)"}, {}));
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

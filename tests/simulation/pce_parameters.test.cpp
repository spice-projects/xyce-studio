#include <gtest/gtest.h>

#include "netlist/netlist.h"
#include "simulation/pce_parameters.h"

// ========================================================================================
// from_xyce_directives
// ========================================================================================

TEST(PceParameterChecks, parse_and_serialize_pce_directive) {
    // arrange
    const std::vector<std::string> directives = {
        ".PCE param=R1 type=normal means=3K std_deviations=1K",
    };
    // act
    const auto result = PceParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->use_expression, false);
    ASSERT_EQ(result->parameters.size(), 1);
    ASSERT_EQ(result->parameters[0], "R1");
    ASSERT_EQ(result->distribution_types.size(), 1);
    ASSERT_EQ(result->distribution_types[0], "normal");
    ASSERT_EQ(result->means.size(), 1);
    ASSERT_EQ(result->means[0], "3K");
    ASSERT_EQ(result->std_deviations.size(), 1);
    ASSERT_EQ(result->std_deviations[0], "1K");
    ASSERT_TRUE(result->lower_bounds.empty());
    ASSERT_TRUE(result->upper_bounds.empty());
    ASSERT_TRUE(result->alphas.empty());
    ASSERT_TRUE(result->betas.empty());
}

TEST(PceParameterChecks, parse_pce_with_uniform_distributions) {
    // arrange
    const std::vector<std::string> directives = {
        ".PCE param=R1,R2 type=uniform,uniform lower_bounds=1K,2K upper_bounds=5K,6K",
    };
    // act
    const auto result = PceParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->parameters.size(), 2);
    ASSERT_EQ(result->parameters[0], "R1");
    ASSERT_EQ(result->parameters[1], "R2");
    ASSERT_EQ(result->distribution_types[0], "uniform");
    ASSERT_EQ(result->distribution_types[1], "uniform");
    ASSERT_EQ(result->lower_bounds.size(), 2);
    ASSERT_EQ(result->lower_bounds[0], "1K");
    ASSERT_EQ(result->lower_bounds[1], "2K");
    ASSERT_EQ(result->upper_bounds.size(), 2);
    ASSERT_EQ(result->upper_bounds[0], "5K");
    ASSERT_EQ(result->upper_bounds[1], "6K");
}

TEST(PceParameterChecks, parse_pce_with_use_expression) {
    // arrange
    const std::vector<std::string> directives = {
        ".PCE useExpr=true",
    };
    // act
    const auto result = PceParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->use_expression, true);
    ASSERT_TRUE(result->parameters.empty());
}

TEST(PceParameterChecks, parse_pce_with_gamma_distributions) {
    // arrange
    const std::vector<std::string> directives = {
        ".PCE param=R1 type=gamma alpha=2 beta=3",
    };
    // act
    const auto result = PceParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->alphas.size(), 1);
    ASSERT_EQ(result->alphas[0], "2");
    ASSERT_EQ(result->betas.size(), 1);
    ASSERT_EQ(result->betas[0], "3");
}

TEST(PceParameterChecks, parse_pce_with_pces_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".PCE param=R1 type=normal means=3K std_deviations=1K",
        ".OPTIONS PCES OUTPUTS={R1:R},{V(1)}",
    };
    // act
    const auto result = PceParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->pces_options.size(), 1);
    ASSERT_EQ(result->pces_options.at("OUTPUTS"), "{R1:R},{V(1)}");
}

TEST(PceParameterChecks, parse_pce_with_print_parameters) {
    // arrange
    const std::vector<std::string> directives = {
        ".PCE param=R1 type=normal means=3K std_deviations=1K",
        ".PRINT PCE FORMAT=CSV OUTPUT_SAMPLE_STATS=true V(1)",
    };
    // act
    const auto result = PceParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->print_parameters.has_value());
    ASSERT_EQ(result->print_parameters->print_type, "PCE");
    ASSERT_EQ(result->print_parameters->print_format, "CSV");
    ASSERT_EQ(result->print_parameters->extra_options.size(), 1);
    ASSERT_EQ(result->print_parameters->extra_options[0], "OUTPUT_SAMPLE_STATS=true");
    ASSERT_EQ(result->print_parameters->output_variables.size(), 1);
    ASSERT_EQ(result->print_parameters->output_variables[0], "V(1)");
}

TEST(PceParameterChecks, no_pce_directive_returns_none) {
    // arrange
    const std::vector<std::string> directives = {
        ".TRAN 1u 1m",
    };
    // act
    const auto result = PceParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

// ========================================================================================
// to_xyce_directives
// ========================================================================================

TEST(PceParameterChecks, generate_pce_directive) {
    // arrange
    const PceParameters params(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".PCE param=R1 type=normal means=3K std_deviations=1K");
}

TEST(PceParameterChecks, generate_pce_directive_only_present_lists) {
    // arrange
    const PceParameters params(false, {"R1", "R2"}, {"uniform", "uniform"}, {}, {}, {"1K", "2K"}, {"5K", "6K"}, {}, {}, {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".PCE param=R1,R2 type=uniform,uniform lower_bounds=1K,2K upper_bounds=5K,6K");
}

TEST(PceParameterChecks, generate_pce_directive_with_use_expression) {
    // arrange
    const PceParameters params(true, {}, {}, {}, {}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".PCE useExpr=true");
}

TEST(PceParameterChecks, generate_pce_with_pces_options) {
    // arrange
    const std::map<std::string, std::string> options = {{"OUTPUTS", "{R1:R},{V(1)}"}};
    const PceParameters params(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, options, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".PCE param=R1 type=normal means=3K std_deviations=1K");
    ASSERT_EQ(directives[1], ".OPTIONS PCES OUTPUTS={R1:R},{V(1)}");
}

TEST(PceParameterChecks, generate_pce_with_print_parameters) {
    // arrange
    const PceParameters params(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, PrintParameters("PCE", "CSV", "", {"V(1)"}, {"OUTPUT_SAMPLE_STATS=true"}));
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[1], ".PRINT PCE FORMAT=CSV OUTPUT_SAMPLE_STATS=true V(1)");
}

// ========================================================================================
// round-trip
// ========================================================================================

TEST(PceParameterChecks, round_trip_reference_guide_examples) {
    // arrange
    const std::vector<std::string> directives = {
        ".PCE param=R1,R2 type=uniform,uniform lower_bounds=1K,2K upper_bounds=5K,6K",
        ".OPTIONS PCES OUTPUTS={R1:R},{V(1)}",
        ".PRINT PCE OUTPUT_SAMPLE_STATS=true V(1)",
    };
    // act
    const auto parsed = PceParameters::from_xyce_directives(directives);
    const auto regenerated = parsed->to_xyce_directives(NetlistTopology{});
    const auto reparsed = PceParameters::from_xyce_directives(regenerated);
    // assert
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(*reparsed, *parsed);
}

TEST(PceParameterChecks, round_trip_use_expression) {
    // arrange
    const std::vector<std::string> directives = {
        ".PCE useExpr=true",
        ".OPTIONS PCES OUTPUTS={V(1)}",
    };
    // act
    const auto parsed = PceParameters::from_xyce_directives(directives);
    const auto regenerated = parsed->to_xyce_directives(NetlistTopology{});
    const auto reparsed = PceParameters::from_xyce_directives(regenerated);
    // assert
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(*reparsed, *parsed);
}

// ========================================================================================
// validate
// ========================================================================================

TEST(PceParameterChecks, validate_accepts_normal_distribution) {
    // arrange
    const PceParameters params(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const auto result = params.validate();
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(PceParameterChecks, validate_accepts_use_expression) {
    // arrange
    const PceParameters params(true, {}, {}, {}, {}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const auto result = params.validate();
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(PceParameterChecks, validate_requires_parameters_or_use_expression) {
    // arrange
    const PceParameters params(false, {}, {}, {}, {}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const auto result = params.validate();
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(PceParameterChecks, validate_rejects_mismatched_list_length) {
    // arrange
    const PceParameters params(false, {"R1", "R2"}, {"uniform", "uniform"}, {}, {}, {"1K"}, {"5K", "6K"}, {}, {}, {}, std::nullopt);
    // act
    const auto result = params.validate();
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(PceParameterChecks, validate_rejects_uniform_without_bounds) {
    // arrange
    const PceParameters params(false, {"R1"}, {"uniform"}, {}, {}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const auto result = params.validate();
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(PceParameterChecks, validate_rejects_normal_without_std_deviations) {
    // arrange
    const PceParameters params(false, {"R1"}, {"normal"}, {"3K"}, {}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const auto result = params.validate();
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(PceParameterChecks, validate_rejects_gamma_without_alpha) {
    // arrange
    const PceParameters params(false, {"R1"}, {"gamma"}, {}, {}, {}, {}, {}, {"3"}, {}, std::nullopt);
    // act
    const auto result = params.validate();
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(PceParameterChecks, validate_rejects_unknown_distribution_type) {
    // arrange
    const PceParameters params(false, {"R1"}, {"lognormal"}, {}, {}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const auto result = params.validate();
    // assert
    ASSERT_TRUE(result.has_value());
}

// ========================================================================================
// equality operator
// ========================================================================================

TEST(PceParameterChecks, equality_operator_equal_params) {
    // arrange
    const PceParameters params1(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, std::nullopt);
    const PceParameters params2(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_TRUE(result);
}

TEST(PceParameterChecks, equality_operator_different_use_expression) {
    // arrange
    const PceParameters params1(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, std::nullopt);
    const PceParameters params2(true, {}, {}, {}, {}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(PceParameterChecks, equality_operator_different_parameters) {
    // arrange
    const PceParameters params1(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, std::nullopt);
    const PceParameters params2(false, {"R2"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(PceParameterChecks, equality_operator_different_pces_options) {
    // arrange
    const PceParameters params1(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {{"OUTPUTS", "{V(1)}"}}, std::nullopt);
    const PceParameters params2(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {{"OUTPUTS", "{V(2)}"}}, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(PceParameterChecks, equality_operator_different_print_parameters) {
    // arrange
    const PceParameters params1(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, PrintParameters("PCE", "", "", {"V(1)"}, {}));
    const PceParameters params2(false, {"R1"}, {"normal"}, {"3K"}, {"1K"}, {}, {}, {}, {}, {}, PrintParameters("PCE", "", "", {"V(2)"}, {}));
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

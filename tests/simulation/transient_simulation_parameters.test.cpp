#include <gtest/gtest.h>

#include "netlist/netlist.h"
#include "simulation/transient_simulation_parameters.h"

// ========================================================================================
// TransientSchedulePoint
// ========================================================================================

TEST(TransientSchedulePointChecks, create_schedule_point) {
    // arrange
    const TransientSchedulePoint point("1u", "10u");
    // act/assert
    ASSERT_EQ(point.time_value, "1u");
    ASSERT_EQ(point.max_time_step_value, "10u");
}

TEST(TransientSchedulePointChecks, equality_operator_equal) {
    // arrange
    const TransientSchedulePoint point1("1u", "10u");
    const TransientSchedulePoint point2("1u", "10u");
    // act
    const bool result = point1 == point2;
    // assert
    ASSERT_TRUE(result);
}

TEST(TransientSchedulePointChecks, equality_operator_different_time) {
    // arrange
    const TransientSchedulePoint point1("1u", "10u");
    const TransientSchedulePoint point2("2u", "10u");
    // act
    const bool result = point1 == point2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSchedulePointChecks, equality_operator_different_step) {
    // arrange
    const TransientSchedulePoint point1("1u", "10u");
    const TransientSchedulePoint point2("1u", "20u");
    // act
    const bool result = point1 == point2;
    // assert
    ASSERT_FALSE(result);
}

// ========================================================================================
// from_xyce_directives
// ========================================================================================

TEST(TransientSimulationParametersChecks, parses_minimal_directive) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1u");
    ASSERT_EQ(result->final_time_value, "1m");
    ASSERT_EQ(result->start_time_value, "");
    ASSERT_EQ(result->step_ceiling_value, "");
}

TEST(TransientSimulationParametersChecks, parses_with_start_time) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1n 10u 100n"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1n");
    ASSERT_EQ(result->final_time_value, "10u");
    ASSERT_EQ(result->start_time_value, "100n");
    ASSERT_EQ(result->step_ceiling_value, "");
}

TEST(TransientSimulationParametersChecks, parses_with_step_ceiling) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m 0 10u"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1u");
    ASSERT_EQ(result->final_time_value, "1m");
    ASSERT_EQ(result->start_time_value, "0");
    ASSERT_EQ(result->step_ceiling_value, "10u");
}

TEST(TransientSimulationParametersChecks, parses_schedule_points) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m 0 10u {schedule(10u, 100u, 20u, 200u)}"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->schedule_points.size(), 2);
    ASSERT_EQ(result->schedule_points[0].time_value, "10u");
    ASSERT_EQ(result->schedule_points[0].max_time_step_value, "100u");
}

TEST(TransientSimulationParametersChecks, parses_fft_parameters) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".FFT V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->fft_parameters.size(), 1);
    ASSERT_EQ(result->fft_parameters[0].output_variable, "V(OUT)");
}

TEST(TransientSimulationParametersChecks, parses_four_parameters) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".FOUR 1k V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->four_parameters.size(), 1);
    ASSERT_EQ(result->four_parameters[0].fundamental_frequency, "1k");
    ASSERT_EQ(result->four_parameters[0].output_variables.size(), 1);
    ASSERT_EQ(result->four_parameters[0].output_variables[0], "V(OUT)");
}

TEST(TransientSimulationParametersChecks, parses_sensitivity_companion_directive) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1u 1m", ".SENS objfunc={V(OUT)} param=R1:R", ".OPTIONS SENSITIVITY direct=0 adjoint=1"};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
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

TEST(TransientSimulationParametersChecks, sensitivity_round_trip) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1u 1m", ".SENS objfunc={V(OUT)} param=R1:R", ".OPTIONS SENSITIVITY direct=0 adjoint=1"};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    const auto serialized = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(serialized.size(), 3);
    ASSERT_EQ(serialized[0], ".TRAN 1u 1m");
    ASSERT_EQ(serialized[1], ".SENS objfunc={V(OUT)} param=R1:R");
    ASSERT_EQ(serialized[2], ".OPTIONS SENSITIVITY direct=0 adjoint=1");
}

TEST(TransientSimulationParametersChecks, no_tran_directive_returns_none) {
    // arrange
    const std::vector<std::string> directives = {".DC VIN 0 5 0.1"};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

// ========================================================================================
// to_xyce_directives
// ========================================================================================

TEST(TransientSimulationParametersChecks, generates_minimal_directive) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m");
}

TEST(TransientSimulationParametersChecks, generates_with_start_time) {
    // arrange
    const TransientSimulationParameters params("1n", "10u", "100n", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".TRAN 1n 10u 100n");
}

TEST(TransientSimulationParametersChecks, generates_with_step_ceiling) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "0", "10u", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m 0 10u");
}

TEST(TransientSimulationParametersChecks, generates_with_schedule_points) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "0", "", "", std::vector<TransientSchedulePoint>{TransientSchedulePoint("10u", "100u")}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m 0 {schedule(10u, 100u)}");
}

TEST(TransientSimulationParametersChecks, generates_with_fft_parameters) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{FftParameters("V(OUT)", "", "", "", "", "", "", "", "", "")}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m");
    ASSERT_EQ(directives[1], ".FFT V(OUT)");
}

TEST(TransientSimulationParametersChecks, generates_with_four_parameters) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{FourParameters("1k", {"V(OUT)"})}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m");
    ASSERT_EQ(directives[1], ".FOUR 1k V(OUT)");
}

TEST(TransientSimulationParametersChecks, generates_with_print_parameters) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, PrintParameters("TRAN", "", "", {"V(*)"}, {}), std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m");
    ASSERT_EQ(directives[1], ".PRINT TRAN V(*)");
}

TEST(TransientSimulationParametersChecks, generates_with_measure_parameters) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{MeasureEntry("TRAN", "rise", "RISE", "V(OUT)")}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m");
    ASSERT_EQ(directives[1], ".MEASURE TRAN rise RISE V(OUT)");
}

TEST(TransientSimulationParametersChecks, generates_with_sensitivity) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, SensParameter("", "objfunc", {"V(OUT)"}, {"R1:R"}, false, true, std::nullopt), std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 3);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m");
    ASSERT_EQ(directives[1], ".SENS objfunc={V(OUT)} param=R1:R");
    ASSERT_EQ(directives[2], ".OPTIONS SENSITIVITY direct=0 adjoint=1");
}

// ========================================================================================
// equality operator
// ========================================================================================

TEST(TransientSimulationParametersChecks, equality_operator_equal_params) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_TRUE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_initial_step) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("2u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_final_time) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("1u", "10m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_start_time) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "0", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("1u", "1m", "100n", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_step_ceiling) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "0", "10u", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("1u", "1m", "0", "20u", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_schedule_points) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "0", "", "", std::vector<TransientSchedulePoint>{TransientSchedulePoint("10u", "100u")}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("1u", "1m", "0", "", "", std::vector<TransientSchedulePoint>{TransientSchedulePoint("20u", "200u")}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_fft_parameters) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{FftParameters("V(OUT)", "", "", "", "", "", "", "", "", "")}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{FftParameters("V(IN)", "", "", "", "", "", "", "", "", "")}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_four_parameters) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{FourParameters("1k", {"V(OUT)"})}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{FourParameters("2k", {"V(OUT)"})}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_print_parameters) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, PrintParameters("TRAN", "", "", {"V(*)"}, {}), std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, PrintParameters("TRAN", "", "", {"I(*)"}, {}), std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_measure_parameters) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{MeasureEntry("TRAN", "rise", "RISE", "V(OUT)")}, std::nullopt, std::nullopt);
    const TransientSimulationParameters params2("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{MeasureEntry("TRAN", "fall", "FALL", "V(OUT)")}, std::nullopt, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(TransientSimulationParametersChecks, equality_operator_different_sensitivity) {
    // arrange
    const TransientSimulationParameters params1("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, SensParameter("", "objfunc", {"V(OUT)"}, {"R1:R"}, false, true, std::nullopt), std::nullopt);
    const TransientSimulationParameters params2("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, SensParameter("", "objfunc", {"V(IN)"}, {"R1:R"}, false, true, std::nullopt), std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

// ========================================================================================
// Additional tests for op_keyword support
// ========================================================================================

TEST(TransientSimulationParametersChecks, generates_with_noop_keyword) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "", "", "NOOP", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m NOOP");
}

TEST(TransientSimulationParametersChecks, generates_with_uic_keyword) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "", "", "UIC", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m UIC");
}

TEST(TransientSimulationParametersChecks, generates_with_multiple_schedule_points) {
    // arrange
    const TransientSimulationParameters params("1n", "20u", "", "", "", std::vector<TransientSchedulePoint>{TransientSchedulePoint("1u", "10n"), TransientSchedulePoint("10u", "100n")}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".TRAN 1n 20u {schedule(1u, 10n, 10u, 100n)}");
}

TEST(TransientSimulationParametersChecks, generates_with_schedule_and_start_step_ceiling) {
    // arrange
    const TransientSimulationParameters params("1n", "10u", "0", "200n", "", std::vector<TransientSchedulePoint>{TransientSchedulePoint("5u", "50n")}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".TRAN 1n 10u 0 200n {schedule(5u, 50n)}");
}

TEST(TransientSimulationParametersChecks, generates_with_all_options_combined) {
    // arrange
    const TransientSimulationParameters params("1u", "1m", "0", "5u", "NOOP", std::vector<TransientSchedulePoint>{TransientSchedulePoint("500u", "1u")}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m 0 5u NOOP {schedule(500u, 1u)}");
}

// ========================================================================================
// Tests for from_xyce_directives edge cases
// ========================================================================================

TEST(TransientSimulationParametersChecks, empty_directives_returns_none) {
    // arrange
    const std::vector<std::string> directives = {};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(TransientSimulationParametersChecks, blank_directive_string_is_skipped) {
    // arrange
    const std::vector<std::string> directives = {""};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(TransientSimulationParametersChecks, non_tran_directives_are_ignored) {
    // arrange
    const std::vector<std::string> directives = {".OP", ".DC VIN 0 5 0.1"};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(TransientSimulationParametersChecks, parses_minimal_tran_directive) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1us 100ms"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1us");
    ASSERT_EQ(result->final_time_value, "100ms");
    ASSERT_EQ(result->start_time_value, "");
    ASSERT_EQ(result->step_ceiling_value, "");
    ASSERT_EQ(result->op_keyword, "");
}

TEST(TransientSimulationParametersChecks, parses_tran_with_start_time) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1ms 100ms 0ms"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1ms");
    ASSERT_EQ(result->final_time_value, "100ms");
    ASSERT_EQ(result->start_time_value, "0ms");
    ASSERT_EQ(result->step_ceiling_value, "");
}

TEST(TransientSimulationParametersChecks, parses_tran_with_start_time_and_step_ceiling) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1ms 100ms 0ms .1ms"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1ms");
    ASSERT_EQ(result->final_time_value, "100ms");
    ASSERT_EQ(result->start_time_value, "0ms");
    ASSERT_EQ(result->step_ceiling_value, ".1ms");
}

TEST(TransientSimulationParametersChecks, parses_tran_with_noop_keyword) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m NOOP"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->op_keyword, "NOOP");
    ASSERT_EQ(result->start_time_value, "");
}

TEST(TransientSimulationParametersChecks, parses_tran_with_uic_keyword) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m UIC"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->op_keyword, "UIC");
}

TEST(TransientSimulationParametersChecks, parses_tran_with_noop_after_positionals) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m 0 5u NOOP"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->start_time_value, "0");
    ASSERT_EQ(result->step_ceiling_value, "5u");
    ASSERT_EQ(result->op_keyword, "NOOP");
}

TEST(TransientSimulationParametersChecks, parses_tran_lowercase_noop_is_normalized) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m noop"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->op_keyword, "NOOP");
}

TEST(TransientSimulationParametersChecks, parses_tran_lowercase_uic_is_normalized) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m uic"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->op_keyword, "UIC");
}

TEST(TransientSimulationParametersChecks, parses_print_tran_directive) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".PRINT TRAN FORMAT=RAW FILE=tran.raw V(OUT) I(V1)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_parameters.has_value(), true);
    ASSERT_EQ(result->print_parameters->print_type, "TRAN");
    ASSERT_EQ(result->print_parameters->print_format, "RAW");
    ASSERT_EQ(result->print_parameters->print_file, "tran.raw");
    ASSERT_EQ(result->print_parameters->output_variables.size(), 2);
    ASSERT_EQ(result->print_parameters->output_variables[0], "V(OUT)");
    ASSERT_EQ(result->print_parameters->output_variables[1], "I(V1)");
}

TEST(TransientSimulationParametersChecks, ignores_non_transient_print_directive) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".PRINT DC V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_parameters.has_value(), false);
}

TEST(TransientSimulationParametersChecks, parses_print_tran_expression_with_spaces) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".PRINT TRAN FORMAT=RAW V(OUT) {V(OUT) * I(V1)}"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_parameters.has_value(), true);
    ASSERT_EQ(result->print_parameters->output_variables.size(), 2);
    ASSERT_EQ(result->print_parameters->output_variables[1], "{V(OUT) * I(V1)}");
}

TEST(TransientSimulationParametersChecks, parses_single_fft_directive) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".FFT V(OUT) WINDOW=HANN"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->fft_parameters.size(), 1);
    ASSERT_EQ(result->fft_parameters[0].output_variable, "V(OUT)");
    ASSERT_EQ(result->fft_parameters[0].window, "HANN");
}

TEST(TransientSimulationParametersChecks, parses_multiple_fft_directives) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".FFT V(1)", ".FFT V(2) WINDOW=RECT"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->fft_parameters.size(), 2);
    ASSERT_EQ(result->fft_parameters[0].output_variable, "V(1)");
    ASSERT_EQ(result->fft_parameters[1].output_variable, "V(2)");
    ASSERT_EQ(result->fft_parameters[1].window, "RECT");
}

TEST(TransientSimulationParametersChecks, parses_single_schedule_point) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 0 2.0e-3 {schedule( 0.5e-3, 0, 1.0e-3, 1.0e-6 )}"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->schedule_points.size(), 2);
    ASSERT_EQ(result->schedule_points[0].time_value, "0.5e-3");
    ASSERT_EQ(result->schedule_points[0].max_time_step_value, "0");
    ASSERT_EQ(result->schedule_points[1].time_value, "1.0e-3");
    ASSERT_EQ(result->schedule_points[1].max_time_step_value, "1.0e-6");
}

TEST(TransientSimulationParametersChecks, parses_schedule_with_three_pairs) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 0 2.0e-3 {schedule( 0.5e-3, 0, 1.0e-3, 1.0e-6, 2.0e-3, 0 )}"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->schedule_points.size(), 3);
    ASSERT_EQ(result->schedule_points[2].time_value, "2.0e-3");
    ASSERT_EQ(result->schedule_points[2].max_time_step_value, "0");
}

TEST(TransientSimulationParametersChecks, schedule_does_not_affect_positional_args) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1n 10u 0 200n {schedule( 5u, 50n )}"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1n");
    ASSERT_EQ(result->final_time_value, "10u");
    ASSERT_EQ(result->start_time_value, "0");
    ASSERT_EQ(result->step_ceiling_value, "200n");
    ASSERT_EQ(result->schedule_points.size(), 1);
}

TEST(TransientSimulationParametersChecks, no_schedule_clause_leaves_schedule_points_empty) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->schedule_points.size(), 0);
}

TEST(TransientSimulationParametersChecks, bare_tran_with_no_arguments_leaves_step_and_time_empty) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "");
    ASSERT_EQ(result->final_time_value, "");
}

TEST(TransientSimulationParametersChecks, tran_with_only_initial_step_leaves_final_time_empty) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1u");
    ASSERT_EQ(result->final_time_value, "");
}

// ========================================================================================
// Tests for wildcard round-trip
// ========================================================================================

TEST(TransientSimulationParametersChecks, generic_wildcards_round_trip) {
    // arrange
    const PrintParameters print_params("TRAN", "", "", {"V(*)", "I(*)", "P(*)"}, {});
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, print_params, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    const auto reparsed = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->print_parameters.has_value(), true);
    ASSERT_EQ(reparsed->print_parameters->print_type, "TRAN");
    ASSERT_EQ(reparsed->print_parameters->output_variables.size(), 3);
    ASSERT_EQ(reparsed->print_parameters->output_variables[0], "V(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[1], "I(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[2], "P(*)");
}

TEST(TransientSimulationParametersChecks, bjt_lead_wildcards_round_trip) {
    // arrange
    const PrintParameters print_params("TRAN", "", "", {"IB(*)", "IC(*)", "IE(*)", "IS(*)"}, {});
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, print_params, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    const auto reparsed = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->print_parameters.has_value(), true);
    ASSERT_EQ(reparsed->print_parameters->output_variables.size(), 4);
    ASSERT_EQ(reparsed->print_parameters->output_variables[0], "IB(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[1], "IC(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[2], "IE(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[3], "IS(*)");
}

TEST(TransientSimulationParametersChecks, fet_lead_wildcards_round_trip) {
    // arrange
    const PrintParameters print_params("TRAN", "", "", {"IB(*)", "ID(*)", "IG(*)", "IS(*)"}, {});
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, print_params, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    const auto reparsed = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->print_parameters.has_value(), true);
    ASSERT_EQ(reparsed->print_parameters->output_variables.size(), 4);
    ASSERT_EQ(reparsed->print_parameters->output_variables[0], "IB(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[1], "ID(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[2], "IG(*)");
    ASSERT_EQ(reparsed->print_parameters->output_variables[3], "IS(*)");
}

TEST(TransientSimulationParametersChecks, w_star_normalizes_to_p_star_on_parse) {
    // arrange
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".PRINT TRAN W(*)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_parameters.has_value(), true);
    ASSERT_EQ(result->print_parameters->output_variables.size(), 1);
    ASSERT_EQ(result->print_parameters->output_variables[0], "P(*)");
}

TEST(TransientSimulationParametersChecks, print_directive_uses_tran_not_dc_type) {
    // arrange
    const PrintParameters print_params("TRAN", "", "", {"V(*)"}, {});
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, print_params, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".TRAN 1u 1m");
    ASSERT_EQ(directives[1], ".PRINT TRAN V(*)");
}

// ========================================================================================
// Tests for round-trip with all options
// ========================================================================================

TEST(TransientSimulationParametersChecks, minimal_round_trip) {
    // arrange
    const TransientSimulationParameters original("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto parsed = TransientSimulationParameters::from_xyce_directives(original.to_xyce_directives(NetlistTopology{}));
    // assert
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->initial_step_value, "1u");
    ASSERT_EQ(parsed->final_time_value, "1m");
}

TEST(TransientSimulationParametersChecks, full_round_trip) {
    // arrange
    const TransientSimulationParameters original("1u", "1m", "0", "5u", "NOOP", std::vector<TransientSchedulePoint>{TransientSchedulePoint("500u", "1u")}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto parsed = TransientSimulationParameters::from_xyce_directives(original.to_xyce_directives(NetlistTopology{}));
    // assert
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->initial_step_value, "1u");
    ASSERT_EQ(parsed->final_time_value, "1m");
    ASSERT_EQ(parsed->start_time_value, "0");
    ASSERT_EQ(parsed->step_ceiling_value, "5u");
    ASSERT_EQ(parsed->op_keyword, "NOOP");
    ASSERT_EQ(parsed->schedule_points.size(), 1);
    ASSERT_EQ(parsed->schedule_points[0].time_value, "500u");
    ASSERT_EQ(parsed->schedule_points[0].max_time_step_value, "1u");
}

TEST(TransientSimulationParametersChecks, round_trip_with_print_parameters) {
    // arrange
    const PrintParameters print_params("TRAN", "RAW", "waves.raw", {"V(OUT)", "ID(M1)", "{V(OUT)*I(V1)}"}, {});
    const TransientSimulationParameters original("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, print_params, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto parsed = TransientSimulationParameters::from_xyce_directives(original.to_xyce_directives(NetlistTopology{}));
    // assert
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->print_parameters.has_value(), true);
    ASSERT_EQ(parsed->print_parameters->print_type, "TRAN");
    ASSERT_EQ(parsed->print_parameters->print_format, "RAW");
    ASSERT_EQ(parsed->print_parameters->print_file, "waves.raw");
    ASSERT_EQ(parsed->print_parameters->output_variables.size(), 3);
    ASSERT_EQ(parsed->print_parameters->output_variables[0], "V(OUT)");
    ASSERT_EQ(parsed->print_parameters->output_variables[1], "ID(M1)");
    ASSERT_EQ(parsed->print_parameters->output_variables[2], "{V(OUT)*I(V1)}");
}

TEST(TransientSimulationParametersChecks, round_trip_with_fft_parameters) {
    // arrange
    const FftParameters fft("V(OUT)", "1024", "HANN", "", "", "", "", "", "", "");
    const TransientSimulationParameters original("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{fft}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{}, std::nullopt, std::nullopt);
    // act
    const auto parsed = TransientSimulationParameters::from_xyce_directives(original.to_xyce_directives(NetlistTopology{}));
    // assert
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->fft_parameters.size(), 1);
    ASSERT_EQ(parsed->fft_parameters[0].output_variable, "V(OUT)");
    ASSERT_EQ(parsed->fft_parameters[0].window, "HANN");
    ASSERT_EQ(parsed->fft_parameters[0].np, "1024");
}

// ========================================================================================
// Tests for transient schedule point
// ========================================================================================

TEST(TransientSchedulePointChecks, stores_time_and_step) {
    // arrange / act
    const TransientSchedulePoint point("1u", "10n");
    // assert
    ASSERT_EQ(point.time_value, "1u");
    ASSERT_EQ(point.max_time_step_value, "10n");
}

TEST(TransientSchedulePointChecks, equality) {
    // arrange
    const TransientSchedulePoint a("1u", "10n");
    const TransientSchedulePoint b("1u", "10n");
    // assert
    ASSERT_EQ(a == b, true);
}

TEST(TransientSchedulePointChecks, inequality_on_time) {
    // arrange
    const TransientSchedulePoint a("1u", "10n");
    const TransientSchedulePoint b("2u", "10n");
    // assert
    ASSERT_EQ(a == b, false);
}

// ========================================================================================
// Tests for measure parameters
// ========================================================================================

TEST(TransientSimulationParametersChecks, parses_single_measure_directive) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".MEASURE TRAN avg_out AVG V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_parameters.size(), 1);
    ASSERT_EQ(result->measure_parameters[0].result_name, "avg_out");
    ASSERT_EQ(result->measure_parameters[0].measure_type, "AVG");
    ASSERT_EQ(result->measure_parameters[0].analysis_type, "TRAN");
    ASSERT_EQ(result->measure_parameters[0].variable, "V(OUT)");
}

TEST(TransientSimulationParametersChecks, parses_multiple_measure_directives) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".MEASURE TRAN avg_out AVG V(OUT)", ".MEASURE TRAN max_out MAX V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_parameters.size(), 2);
    ASSERT_EQ(result->measure_parameters[0].result_name, "avg_out");
    ASSERT_EQ(result->measure_parameters[1].result_name, "max_out");
}

TEST(TransientSimulationParametersChecks, ignores_non_tran_measure_directive) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".MEASURE AC avg_out AVG V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_parameters.size(), 0);
}

TEST(TransientSimulationParametersChecks, parses_measure_with_qualifiers) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".MEASURE TRAN avg_out AVG V(OUT) FROM=0 TO=1m"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_parameters.size(), 1);
    ASSERT_EQ(result->measure_parameters[0].from_val, "0");
    ASSERT_EQ(result->measure_parameters[0].to_val, "1m");
}

TEST(TransientSimulationParametersChecks, parses_meas_alias) {
    // arrange / act
    const auto result = TransientSimulationParameters::from_xyce_directives({".TRAN 1u 1m", ".MEAS TRAN avg_out AVG V(OUT)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_parameters.size(), 1);
    ASSERT_EQ(result->measure_parameters[0].result_name, "avg_out");
}

TEST(TransientSimulationParametersChecks, emits_single_measure_directive) {
    // arrange
    const MeasureEntry measure("TRAN", "avg_out", "AVG", "V(OUT)");
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{measure}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[1], ".MEASURE TRAN avg_out AVG V(OUT)");
}

TEST(TransientSimulationParametersChecks, emits_multiple_measure_directives) {
    // arrange
    const MeasureEntry measure1("TRAN", "avg_out", "AVG", "V(OUT)");
    const MeasureEntry measure2("TRAN", "max_out", "MAX", "V(OUT)");
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{measure1, measure2}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 3);
    ASSERT_EQ(directives[1], ".MEASURE TRAN avg_out AVG V(OUT)");
    ASSERT_EQ(directives[2], ".MEASURE TRAN max_out MAX V(OUT)");
}

TEST(TransientSimulationParametersChecks, emits_measure_with_qualifiers) {
    // arrange
    const MeasureEntry measure("TRAN", "avg_out", "AVG", "V(OUT)", "0", "1m");
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{measure}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[1], ".MEASURE TRAN avg_out AVG V(OUT) FROM=0 TO=1m");
}

TEST(TransientSimulationParametersChecks, measure_round_trip) {
    // arrange
    const MeasureEntry measure("TRAN", "avg_out", "AVG", "V(OUT)", "0", "1m");
    const TransientSimulationParameters params("1u", "1m", "", "", "", std::vector<TransientSchedulePoint>{}, std::nullopt, std::vector<FftParameters>{}, std::vector<FourParameters>{}, std::vector<MeasureEntry>{measure}, std::nullopt, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    const auto reparsed = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->measure_parameters.size(), 1);
    ASSERT_EQ(reparsed->measure_parameters[0].result_name, "avg_out");
    ASSERT_EQ(reparsed->measure_parameters[0].measure_type, "AVG");
    ASSERT_EQ(reparsed->measure_parameters[0].analysis_type, "TRAN");
    ASSERT_EQ(reparsed->measure_parameters[0].variable, "V(OUT)");
    ASSERT_EQ(reparsed->measure_parameters[0].from_val, "0");
    ASSERT_EQ(reparsed->measure_parameters[0].to_val, "1m");
}

// ========================================================================================
// Tests for reference guide examples
// ========================================================================================

TEST(TransientSimulationParametersChecks, reference_guide_example_basic_transient) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1us 100ms"};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1us");
    ASSERT_EQ(result->final_time_value, "100ms");
    ASSERT_EQ(result->start_time_value, "");
    ASSERT_EQ(result->step_ceiling_value, "");
    // verify the directive contains the expected tran line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_EQ(generated[0], ".TRAN 1us 100ms");
}

TEST(TransientSimulationParametersChecks, reference_guide_example_with_start_and_step_ceiling) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1ms 100ms 0ms .1ms"};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "1ms");
    ASSERT_EQ(result->final_time_value, "100ms");
    ASSERT_EQ(result->start_time_value, "0ms");
    ASSERT_EQ(result->step_ceiling_value, ".1ms");
    // verify the directive contains the expected tran line
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_EQ(generated[0], ".TRAN 1ms 100ms 0ms .1ms");
}

TEST(TransientSimulationParametersChecks, reference_guide_example_with_schedule) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 0 2.0e-3 {schedule( 0.5e-3, 0, 1.0e-3, 1.0e-6, 2.0e-3, 0 )}"};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->initial_step_value, "0");
    ASSERT_EQ(result->final_time_value, "2.0e-3");
    ASSERT_EQ(result->schedule_points.size(), 3);
    ASSERT_EQ(result->schedule_points[0].time_value, "0.5e-3");
    ASSERT_EQ(result->schedule_points[0].max_time_step_value, "0");
    ASSERT_EQ(result->schedule_points[1].time_value, "1.0e-3");
    ASSERT_EQ(result->schedule_points[1].max_time_step_value, "1.0e-6");
    ASSERT_EQ(result->schedule_points[2].time_value, "2.0e-3");
    ASSERT_EQ(result->schedule_points[2].max_time_step_value, "0");
    // verify the directive contains the expected schedule clause
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_NE(generated[0].find("schedule"), std::string::npos);
    ASSERT_NE(generated[0].find("0.5e-3, 0"), std::string::npos);
    ASSERT_NE(generated[0].find("1.0e-3, 1.0e-6"), std::string::npos);
    ASSERT_NE(generated[0].find("2.0e-3, 0"), std::string::npos);
}

TEST(TransientSimulationParametersChecks, claims_pce_companion_directives) {
    // arrange
    const std::vector<std::string> directives = {
        ".TRAN 1u 1m",
        ".PCE param=R1 type=uniform lower_bounds=1K upper_bounds=5K",
        ".OPTIONS PCES OUTPUTS={V(1)}",
        ".PRINT PCE OUTPUT_SAMPLE_STATS=true V(1)",
    };
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->pce.has_value());
    ASSERT_EQ(result->pce->parameters.size(), 1);
    ASSERT_EQ(result->pce->distribution_types[0], "uniform");
    ASSERT_TRUE(result->pce->print_parameters.has_value());
    // round-trip through directives
    const auto regenerated = result->to_xyce_directives(NetlistTopology{});
    const auto reparsed = TransientSimulationParameters::from_xyce_directives(regenerated);
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(*reparsed, *result);
}

TEST(TransientSimulationParametersChecks, no_pce_directives_leave_pce_empty) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1u 1m"};
    // act
    const auto result = TransientSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->pce.has_value());
}

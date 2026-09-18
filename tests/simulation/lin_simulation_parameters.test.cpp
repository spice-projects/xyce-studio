#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "simulation/lin_simulation_parameters.h"

namespace
{
    // locate the .LIN directive within a directive list
    auto find_lin_directive(const std::vector<std::string>& directives) -> std::string {
        const auto lin_line = std::find_if(directives.begin(), directives.end(), [](const std::string& d) { return d.rfind(".LIN", 0) == 0; });
        EXPECT_NE(lin_line, directives.end());
        return lin_line == directives.end() ? std::string{} : *lin_line;
    }
} // namespace

// ========================================================================================
// from_xyce_directives — LINTYPE keyword
// ========================================================================================

TEST(LinSimulationParametersChecks, parses_lintype_keyword) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN LINTYPE=Z"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->lintype, "Z");
}

TEST(LinSimulationParametersChecks, parses_lintype_keyword_lowercase) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN lintype=z"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->lintype, "Z");
}

TEST(LinSimulationParametersChecks, parses_type_keyword_backward_compatibility) {
    // arrange / act — TYPE= is accepted for backward compatibility
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN TYPE=Y"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->lintype, "Y");
}

// ========================================================================================
// from_xyce_directives — FILENAME synonym
// ========================================================================================

TEST(LinSimulationParametersChecks, parses_filename_synonym) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN FILENAME=foo.s2p"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->file, "foo.s2p");
}

TEST(LinSimulationParametersChecks, file_wins_over_filename_when_both_given) {
    // arrange / act — FILE= appears after FILENAME=
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN FILENAME=foo.s2p FILE=bar.s2p"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->file, "bar.s2p");
}

TEST(LinSimulationParametersChecks, file_wins_over_filename_when_both_given_reversed) {
    // arrange / act — FILE= appears before FILENAME=
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN FILE=bar.s2p FILENAME=foo.s2p"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->file, "bar.s2p");
}

// ========================================================================================
// to_xyce_directives — keyword emission
// ========================================================================================

TEST(LinSimulationParametersChecks, emits_lintype_keyword) {
    // arrange
    const LinSimulationParameters params(true, "TOUCHSTONE2", "Z", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const std::string lin_line = find_lin_directive(directives);
    EXPECT_NE(lin_line.find("LINTYPE=Z"), std::string::npos);
}

TEST(LinSimulationParametersChecks, emits_lintype_keyword_for_default) {
    // arrange — default lintype "S" is still emitted so the directive
    // round-trips the netlist text unchanged
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const std::string lin_line = find_lin_directive(directives);
    EXPECT_NE(lin_line.find("LINTYPE=S"), std::string::npos);
}

TEST(LinSimulationParametersChecks, emits_file_keyword) {
    // arrange
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "RI", "out.s2p", "", "", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const std::string lin_line = find_lin_directive(directives);
    EXPECT_NE(lin_line.find("FILE=out.s2p"), std::string::npos);
}

// ========================================================================================
// round-trip tests
// ========================================================================================

TEST(LinSimulationParametersChecks, lintype_round_trips_through_directives) {
    // arrange
    const LinSimulationParameters input(true, "TOUCHSTONE2", "Z", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = input.to_xyce_directives(NetlistTopology{});
    const auto output = LinSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->lintype, "Z");
}

TEST(LinSimulationParametersChecks, filename_round_trips_through_directives) {
    // arrange — .LIN FILENAME=foo on input, emitted as FILE=, parsed back as file
    const auto input = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN FILENAME=foo.s2p"});
    ASSERT_TRUE(input.has_value());
    // act
    const auto directives = input->to_xyce_directives(NetlistTopology{});
    const auto output = LinSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->file, "foo.s2p");
}

TEST(LinSimulationParametersChecks, reference_guide_lintype_round_trip) {
    // arrange — RG 2.1.17 style directive: .LIN LINTYPE=Z DATAFORMAT=MA FILENAME=foo
    const auto input = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN LINTYPE=Z DATAFORMAT=MA FILENAME=foo"});
    ASSERT_TRUE(input.has_value());
    // act
    const auto directives = input->to_xyce_directives(NetlistTopology{});
    const auto output = LinSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->lintype, "Z");
    EXPECT_EQ(output->dataformat, "MA");
    EXPECT_EQ(output->file, "foo");
}

// ========================================================================================
// from_xyce_directives — SPARCALC keyword
// ========================================================================================

TEST(LinSimulationParametersChecks, sparcalc_defaults_to_true) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->sparcalc);
}

TEST(LinSimulationParametersChecks, parses_sparcalc_disabled) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN SPARCALC=0"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->sparcalc);
}

TEST(LinSimulationParametersChecks, parses_sparcalc_true) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN SPARCALC=1"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->sparcalc);
}

// ========================================================================================
// from_xyce_directives — FORMAT / DATAFORMAT / WIDTH / PRECISION keywords
// ========================================================================================

TEST(LinSimulationParametersChecks, parses_format_keyword) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN FORMAT=TOUCHSTONE"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->format, "TOUCHSTONE");
}

TEST(LinSimulationParametersChecks, parses_output_layout_keywords) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN DATAFORMAT=MA WIDTH=20 PRECISION=6"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->dataformat, "MA");
    EXPECT_EQ(result->width, "20");
    EXPECT_EQ(result->precision, "6");
}

// ========================================================================================
// from_xyce_directives — embedded AC sweep variants
// ========================================================================================

TEST(LinSimulationParametersChecks, parses_dec_sweep) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC DEC 10 1 1MEG", ".LIN"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sweep_mode, "DEC");
    EXPECT_EQ(result->points, "10");
    EXPECT_EQ(result->start, "1");
    EXPECT_EQ(result->end, "1MEG");
}

TEST(LinSimulationParametersChecks, parses_oct_sweep) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC OCT 5 1 1MEG", ".LIN"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sweep_mode, "OCT");
    EXPECT_EQ(result->points, "5");
}

TEST(LinSimulationParametersChecks, parses_data_sweep) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC DATA=SWEEPTBL", ".LIN"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sweep_mode, "DATA");
    EXPECT_EQ(result->data_table_name, "SWEEPTBL");
}

TEST(LinSimulationParametersChecks, parses_explicit_lin_sweep) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sweep_mode, "LIN");
    EXPECT_EQ(result->points, "100");
    EXPECT_EQ(result->start, "1");
    EXPECT_EQ(result->end, "1MEG");
}

TEST(LinSimulationParametersChecks, parses_implicit_lin_sweep) {
    // arrange / act — .AC without a sweep keyword implies LIN
    const auto result = LinSimulationParameters::from_xyce_directives({".AC 100 1 1MEG", ".LIN"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sweep_mode, "LIN");
    EXPECT_EQ(result->points, "100");
    EXPECT_EQ(result->start, "1");
    EXPECT_EQ(result->end, "1MEG");
}

TEST(LinSimulationParametersChecks, no_lin_directive_returns_none) {
    // arrange — a bare .AC netlist is handled by the AC parser, not LIN
    const std::vector<std::string> directives = {".AC LIN 100 1 1MEG"};
    // act
    const auto result = LinSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

// ========================================================================================
// from_xyce_directives — print directive retention
// ========================================================================================

TEST(LinSimulationParametersChecks, retains_ac_print_parameters) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN", ".PRINT AC V(1)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->print_parameters.has_value());
    EXPECT_EQ(result->print_parameters->print_type, "AC");
}

TEST(LinSimulationParametersChecks, ignores_non_ac_print_directives) {
    // arrange / act
    const auto result = LinSimulationParameters::from_xyce_directives({".AC LIN 100 1 1MEG", ".LIN", ".PRINT TRAN V(1)"});
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->print_parameters.has_value());
}

// ========================================================================================
// to_xyce_directives — keyword emission branches
// ========================================================================================

TEST(LinSimulationParametersChecks, emits_sparcalc_zero_when_disabled) {
    // arrange
    const LinSimulationParameters params(false, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const std::string lin_line = find_lin_directive(directives);
    EXPECT_NE(lin_line.find("SPARCALC=0"), std::string::npos);
}

TEST(LinSimulationParametersChecks, emits_sparcalc_one_for_default) {
    // arrange — the enabled state is emitted explicitly so the directive
    // round-trips the netlist text unchanged
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const std::string lin_line = find_lin_directive(directives);
    EXPECT_NE(lin_line.find("SPARCALC=1"), std::string::npos);
}

TEST(LinSimulationParametersChecks, emits_format_when_not_default) {
    // arrange
    const LinSimulationParameters params(true, "TOUCHSTONE", "S", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const std::string lin_line = find_lin_directive(directives);
    EXPECT_NE(lin_line.find("FORMAT=TOUCHSTONE"), std::string::npos);
    EXPECT_EQ(lin_line.find("FORMAT=TOUCHSTONE2"), std::string::npos);
}

TEST(LinSimulationParametersChecks, emits_format_keyword_for_default) {
    // arrange — the default format is emitted explicitly so the directive
    // round-trips the netlist text unchanged
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const std::string lin_line = find_lin_directive(directives);
    EXPECT_NE(lin_line.find("FORMAT=TOUCHSTONE2"), std::string::npos);
}

TEST(LinSimulationParametersChecks, emits_output_layout_keywords) {
    // arrange
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "MA", "out.s2p", "20", "6", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const std::string lin_line = find_lin_directive(directives);
    EXPECT_NE(lin_line.find("DATAFORMAT=MA"), std::string::npos);
    EXPECT_NE(lin_line.find("WIDTH=20"), std::string::npos);
    EXPECT_NE(lin_line.find("PRECISION=6"), std::string::npos);
}

TEST(LinSimulationParametersChecks, emits_dec_sweep_directive) {
    // arrange
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "RI", "", "", "", "DEC", "10", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_FALSE(directives.empty());
    EXPECT_EQ(directives[0], ".AC DEC 10 1 1MEG");
}

TEST(LinSimulationParametersChecks, emits_oct_sweep_directive) {
    // arrange
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "RI", "", "", "", "OCT", "5", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_FALSE(directives.empty());
    EXPECT_EQ(directives[0], ".AC OCT 5 1 1MEG");
}

TEST(LinSimulationParametersChecks, emits_data_sweep_directive) {
    // arrange
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "RI", "", "", "", "DATA", "", "", "", "SWEEPTBL", std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_FALSE(directives.empty());
    EXPECT_EQ(directives[0], ".AC DATA=SWEEPTBL");
}

TEST(LinSimulationParametersChecks, emits_ac_print_directive) {
    // arrange
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", PrintParameters("AC", "", "", {"V(1)"}, {}));
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const auto found = std::find_if(directives.begin(), directives.end(), [](const std::string& d) { return d.find(".PRINT AC V(1)") != std::string::npos; });
    ASSERT_NE(found, directives.end());
}

TEST(LinSimulationParametersChecks, skips_non_ac_print_on_emission) {
    // arrange
    const LinSimulationParameters params(true, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", PrintParameters("DC", "", "", {"V(1)"}, {}));
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    const auto found = std::find_if(directives.begin(), directives.end(), [](const std::string& d) { return d.rfind(".PRINT", 0) == 0; });
    EXPECT_EQ(found, directives.end());
}

// ========================================================================================
// full round trips
// ========================================================================================

TEST(LinSimulationParametersChecks, sparcalc_round_trips_through_directives) {
    // arrange
    const LinSimulationParameters input(false, "TOUCHSTONE2", "S", "RI", "", "", "", "LIN", "100", "1", "1MEG", "", std::nullopt);
    // act
    const auto directives = input.to_xyce_directives(NetlistTopology{});
    const auto output = LinSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(output.has_value());
    EXPECT_FALSE(output->sparcalc);
}

TEST(LinSimulationParametersChecks, dec_sweep_round_trips_through_directives) {
    // arrange
    const auto input = LinSimulationParameters::from_xyce_directives({".AC DEC 10 1 1MEG", ".LIN"});
    ASSERT_TRUE(input.has_value());
    // act
    const auto directives = input->to_xyce_directives(NetlistTopology{});
    const auto output = LinSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->sweep_mode, "DEC");
    EXPECT_EQ(output->points, "10");
    EXPECT_EQ(output->start, "1");
    EXPECT_EQ(output->end, "1MEG");
}

TEST(LinSimulationParametersChecks, data_sweep_round_trips_through_directives) {
    // arrange
    const auto input = LinSimulationParameters::from_xyce_directives({".AC DATA=SWEEPTBL", ".LIN"});
    ASSERT_TRUE(input.has_value());
    // act
    const auto directives = input->to_xyce_directives(NetlistTopology{});
    const auto output = LinSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->sweep_mode, "DATA");
    EXPECT_EQ(output->data_table_name, "SWEEPTBL");
}

TEST(LinSimulationParametersChecks, full_keyword_set_round_trips_through_directives) {
    // arrange
    const auto input = LinSimulationParameters::from_xyce_directives({".AC OCT 5 1 1MEG", ".LIN SPARCALC=0 FORMAT=TOUCHSTONE LINTYPE=Y DATAFORMAT=MA FILE=out.s2p WIDTH=20 PRECISION=6", ".PRINT AC V(1)"});
    ASSERT_TRUE(input.has_value());
    // act
    const auto directives = input->to_xyce_directives(NetlistTopology{});
    const auto output = LinSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(*output, *input);
}

TEST(LinSimulationParametersChecks, default_keyword_set_round_trips_verbatim) {
    // arrange — the lin-simple-01 netlist directive: every keyword matches the
    // Xyce defaults, and the emitted directive must keep the original text
    const auto input = LinSimulationParameters::from_xyce_directives({".AC DEC 200 100meg 1.5g", ".LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=lin-simple-01.s2p WIDTH=16 PRECISION=8"});
    ASSERT_TRUE(input.has_value());
    // act
    const auto directives = input->to_xyce_directives(NetlistTopology{});
    const auto output = LinSimulationParameters::from_xyce_directives(directives);
    // assert — the .LIN line is byte-for-byte the original
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(*output, *input);
    const std::string lin_line = find_lin_directive(directives);
    EXPECT_EQ(lin_line, ".LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=lin-simple-01.s2p WIDTH=16 PRECISION=8");
}

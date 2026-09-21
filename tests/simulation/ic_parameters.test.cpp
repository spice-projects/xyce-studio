#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "simulation/ic_parameters.h"
#include "simulation/simulation_config.h"

// ========================================================================================
// IcEntry
// ========================================================================================

TEST(IcEntryChecks, create_ic_entry) {
    // arrange
    const IcEntry entry("V1", "5");
    // act/assert
    ASSERT_EQ(entry.node, "V1");
    ASSERT_EQ(entry.voltage, "5");
}

TEST(IcEntryChecks, equality_operator_equal) {
    // arrange
    const IcEntry entry1("V1", "5");
    const IcEntry entry2("V1", "5");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_TRUE(result);
}

TEST(IcEntryChecks, equality_operator_different_node) {
    // arrange
    const IcEntry entry1("V1", "5");
    const IcEntry entry2("V2", "5");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_FALSE(result);
}

TEST(IcEntryChecks, equality_operator_different_voltage) {
    // arrange
    const IcEntry entry1("V1", "5");
    const IcEntry entry2("V1", "3.3");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_FALSE(result);
}

// ========================================================================================
// ICParameters
// ========================================================================================

TEST(ICParametersChecks, default_constructor_returns_empty) {
    // arrange / act
    const ICParameters params;
    // assert
    ASSERT_TRUE(params.empty());
    ASSERT_EQ(params.entries().size(), 0);
}

TEST(ICParametersChecks, constructed_with_entries_is_not_empty) {
    // arrange
    const std::vector<IcEntry> entries = {IcEntry("V1", "5"), IcEntry("V2", "3.3")};
    // act
    const ICParameters params(std::move(entries));
    // assert
    ASSERT_FALSE(params.empty());
    ASSERT_EQ(params.entries().size(), 2);
}

// ========================================================================================
// from_xyce_directives
// ========================================================================================

TEST(ICParametersChecks, empty_directives_returns_empty) {
    // arrange / act
    const auto params = ICParameters::from_xyce_directives({});
    // assert
    ASSERT_TRUE(params.empty());
}

TEST(ICParametersChecks, no_ic_directives_returns_empty) {
    // arrange / act
    const auto params = ICParameters::from_xyce_directives({".OP", ".TRAN 1u 1m"});
    // assert
    ASSERT_TRUE(params.empty());
}

TEST(ICParametersChecks, parses_ic_v_form) {
    // arrange / act
    const auto params = ICParameters::from_xyce_directives({".IC V(out)=1.0 V(in)=0"});
    // assert
    ASSERT_FALSE(params.empty());
    ASSERT_EQ(params.entries().size(), 2);
    ASSERT_EQ(params.entries()[0].node, "out");
    ASSERT_EQ(params.entries()[0].voltage, "1.0");
    ASSERT_EQ(params.entries()[1].node, "in");
    ASSERT_EQ(params.entries()[1].voltage, "0");
}

TEST(ICParametersChecks, parses_ic_node_val_form) {
    // arrange / act
    const auto params = ICParameters::from_xyce_directives({".IC out 1.0"});
    // assert
    ASSERT_FALSE(params.empty());
    ASSERT_EQ(params.entries().size(), 1);
    ASSERT_EQ(params.entries()[0].node, "out");
    ASSERT_EQ(params.entries()[0].voltage, "1.0");
}

TEST(ICParametersChecks, parses_dcvolt_v_form) {
    // arrange / act
    const auto params = ICParameters::from_xyce_directives({".DCVOLT V(out)=2.5"});
    // assert
    ASSERT_FALSE(params.empty());
    ASSERT_EQ(params.entries().size(), 1);
    ASSERT_EQ(params.entries()[0].node, "out");
    ASSERT_EQ(params.entries()[0].voltage, "2.5");
}

TEST(ICParametersChecks, parses_reference_guide_example) {
    // arrange — .IC V(2)=3.1 and .IC 2 3.1 per the Xyce reference guide
    const auto params_v_form = ICParameters::from_xyce_directives({".IC V(2)=3.1"});
    const auto params_pair_form = ICParameters::from_xyce_directives({".IC 2 3.1"});
    // assert
    ASSERT_FALSE(params_v_form.empty());
    ASSERT_EQ(params_v_form.entries()[0].node, "2");
    ASSERT_EQ(params_v_form.entries()[0].voltage, "3.1");
    ASSERT_FALSE(params_pair_form.empty());
    ASSERT_EQ(params_pair_form.entries()[0].node, "2");
    ASSERT_EQ(params_pair_form.entries()[0].voltage, "3.1");
}

TEST(ICParametersChecks, parses_mixed_forms_in_one_line) {
    // arrange / act
    const auto params = ICParameters::from_xyce_directives({".IC V(out)=1.2 3 0.5"});
    // assert
    ASSERT_FALSE(params.empty());
    ASSERT_EQ(params.entries().size(), 2);
    ASSERT_EQ(params.entries()[0].node, "out");
    ASSERT_EQ(params.entries()[0].voltage, "1.2");
    ASSERT_EQ(params.entries()[1].node, "3");
    ASSERT_EQ(params.entries()[1].voltage, "0.5");
}

TEST(ICParametersChecks, parses_multiple_directives) {
    // arrange / act
    const auto params = ICParameters::from_xyce_directives({".DCVOLT V(X)=1.0", ".IC Y 2.0", ".IC V(Z)=3.0"});
    // assert
    ASSERT_FALSE(params.empty());
    ASSERT_EQ(params.entries().size(), 3);
    ASSERT_EQ(params.entries()[0].node, "X");
    ASSERT_EQ(params.entries()[0].voltage, "1.0");
    ASSERT_EQ(params.entries()[1].node, "Y");
    ASSERT_EQ(params.entries()[1].voltage, "2.0");
    ASSERT_EQ(params.entries()[2].node, "Z");
    ASSERT_EQ(params.entries()[2].voltage, "3.0");
}

// ========================================================================================
// to_xyce_directives
// ========================================================================================

TEST(ICParametersChecks, empty_params_produces_no_directives) {
    // arrange
    const ICParameters params;
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_TRUE(directives.empty());
}

TEST(ICParametersChecks, generates_ic_directives_in_v_form) {
    // arrange
    const ICParameters params({IcEntry("V1", "5"), IcEntry("V2", "3.3")});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".IC V1=5");
    ASSERT_EQ(directives[1], ".IC V2=3.3");
}

TEST(ICParametersChecks, plain_node_is_wrapped_in_v_form) {
    // arrange
    const ICParameters params({IcEntry("out", "1.2")});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert — "out" becomes "V(out)"
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".IC V(out)=1.2");
}

TEST(ICParametersChecks, voltage_node_keeps_its_form) {
    // arrange — node "V1" already starts with V, kept as-is
    const ICParameters params({IcEntry("V1", "5")});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives[0], ".IC V1=5");
}

// ========================================================================================
// round-trip
// ========================================================================================

TEST(ICParametersChecks, v_form_round_trips) {
    // arrange
    const std::vector<std::string> input = {".IC V(out)=1.0 V(in)=0"};
    // act
    const auto parsed = ICParameters::from_xyce_directives(input);
    const auto directives = parsed.to_xyce_directives(NetlistTopology{});
    const auto reparsed = ICParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(reparsed.entries().size(), 2);
    ASSERT_EQ(reparsed.entries()[0].node, "out");
    ASSERT_EQ(reparsed.entries()[0].voltage, "1.0");
    ASSERT_EQ(reparsed.entries()[1].node, "in");
    ASSERT_EQ(reparsed.entries()[1].voltage, "0");
}

TEST(ICParametersChecks, node_val_form_round_trips_as_v_form) {
    // arrange
    const std::vector<std::string> input = {".IC out 1.0"};
    // act
    const auto parsed = ICParameters::from_xyce_directives(input);
    const auto directives = parsed.to_xyce_directives(NetlistTopology{});
    const auto reparsed = ICParameters::from_xyce_directives(directives);
    // assert — the node/val pair form is emitted as V(node)=val, then reparses to the same entry
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".IC V(out)=1.0");
    ASSERT_EQ(reparsed.entries().size(), 1);
    ASSERT_EQ(reparsed.entries()[0].node, "out");
    ASSERT_EQ(reparsed.entries()[0].voltage, "1.0");
}

// ========================================================================================
// equality operator
// ========================================================================================

TEST(ICParametersChecks, equality_operator_equal) {
    // arrange
    const ICParameters params1({IcEntry("V1", "5")});
    const ICParameters params2({IcEntry("V1", "5")});
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_TRUE(result);
}

TEST(ICParametersChecks, equality_operator_different_entries) {
    // arrange
    const ICParameters params1({IcEntry("V1", "5")});
    const ICParameters params2({IcEntry("V2", "3.3")});
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

// ========================================================================================
// integration with SimulationConfig
// ========================================================================================

TEST(ICParametersChecks, simulation_config_parses_ic_directives) {
    // arrange
    const std::vector<std::string> directives = {".IC V(out)=1.0 V(in)=0", ".OP"};
    // act
    const auto config = SimulationConfig::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(config.ic_parameters.empty());
    ASSERT_EQ(config.ic_parameters.entries().size(), 2);
    ASSERT_EQ(config.ic_parameters.entries()[0].node, "out");
    ASSERT_EQ(config.ic_parameters.entries()[0].voltage, "1.0");
}

TEST(ICParametersChecks, simulation_config_without_ic_returns_empty) {
    // arrange
    const std::vector<std::string> directives = {".OP"};
    // act
    const auto config = SimulationConfig::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(config.ic_parameters.empty());
}

TEST(ICParametersChecks, simulation_config_emits_ic_directives) {
    // arrange
    const std::vector<std::string> input = {".IC V(out)=1.0", ".OP"};
    const auto config = SimulationConfig::from_xyce_directives(input);
    // act
    const auto directives = config.to_xyce_directives(NetlistTopology{});
    // assert
    bool found_ic = false;
    for (const auto& directive : directives) {
        if (directive.find(".IC V(out)=1.0") == 0)
            found_ic = true;
    }
    ASSERT_TRUE(found_ic);
}

TEST(ICParametersChecks, dcvolt_is_parsed_as_ic) {
    // arrange — .DCVOLT is an alias for .IC
    const auto params = ICParameters::from_xyce_directives({".DCVOLT V(X)=1.0"});
    // assert
    ASSERT_FALSE(params.empty());
    ASSERT_EQ(params.entries()[0].node, "X");
    ASSERT_EQ(params.entries()[0].voltage, "1.0");
}

TEST(ICParametersChecks, ic_and_dcvolt_are_collected_together) {
    // arrange / act
    const auto params = ICParameters::from_xyce_directives({".IC V(out)=1.0", ".DCVOLT V(in)=0", ".IC X 2.0"});
    // assert
    ASSERT_EQ(params.entries().size(), 3);
}

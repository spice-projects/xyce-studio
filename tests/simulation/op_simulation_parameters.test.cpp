#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "netlist/netlist.h"
#include "simulation/op_simulation_parameters.h"

// ========================================================================================
// NodesetEntry
// ========================================================================================

TEST(NodesetEntryChecks, create_nodeset_entry) {
    // arrange
    const NodesetEntry entry("V1", "5");
    // act/assert
    ASSERT_EQ(entry.node, "V1");
    ASSERT_EQ(entry.voltage, "5");
}

TEST(NodesetEntryChecks, equality_operator_equal) {
    // arrange
    const NodesetEntry entry1("V1", "5");
    const NodesetEntry entry2("V1", "5");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_TRUE(result);
}

TEST(NodesetEntryChecks, equality_operator_different_node) {
    // arrange
    const NodesetEntry entry1("V1", "5");
    const NodesetEntry entry2("V2", "5");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_FALSE(result);
}

TEST(NodesetEntryChecks, equality_operator_different_voltage) {
    // arrange
    const NodesetEntry entry1("V1", "5");
    const NodesetEntry entry2("V1", "3.3");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_FALSE(result);
}

// ========================================================================================
// from_xyce_directives
// ========================================================================================

TEST(OpSimulationParametersChecks, parses_op_directive) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP"});
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(OpSimulationParametersChecks, parses_print_dc_directive) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".PRINT DC V(*) I(*)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->print_parameters.has_value());
    ASSERT_EQ(result->print_parameters->print_type, "DC");
    ASSERT_EQ(result->print_parameters->output_variables.size(), 2);
    ASSERT_EQ(result->print_parameters->output_variables[0], "V(*)");
    ASSERT_EQ(result->print_parameters->output_variables[1], "I(*)");
}

TEST(OpSimulationParametersChecks, parses_nodeset_directives) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".NODESET V1=5 V2=3.3"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodeset_entries.size(), 2);
    ASSERT_EQ(result->nodeset_entries[0].node, "V1");
    ASSERT_EQ(result->nodeset_entries[0].voltage, "5");
    ASSERT_EQ(result->nodeset_entries[1].node, "V2");
    ASSERT_EQ(result->nodeset_entries[1].voltage, "3.3");
}

TEST(OpSimulationParametersChecks, no_op_directive_returns_none) {
    // arrange
    const std::vector<std::string> directives = {".TRAN 1u 1m"};
    // act
    const auto result = OpSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(OpSimulationParametersChecks, generates_op_directive_default) {
    // arrange
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".OP");
}

TEST(OpSimulationParametersChecks, generates_print_dc_directive) {
    // arrange
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {}, PrintParameters("DC", "", "", {"V(1)", "I(V1)"}, {}));
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".OP");
    ASSERT_EQ(directives[1], ".PRINT DC V(1) I(V1)");
}

TEST(OpSimulationParametersChecks, generates_save_directive) {
    // arrange
    const OpSimulationParameters params(false, false, false, {}, "", "", true, "IC", "test.ic", {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".OP");
    ASSERT_EQ(directives[1], ".SAVE TYPE=IC FILE=test.ic");
}

TEST(OpSimulationParametersChecks, generates_nodeset_directive) {
    // arrange
    const std::vector<NodesetEntry> nodeset_entries = {NodesetEntry("out", "1.2")};
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", nodeset_entries, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".OP");
    ASSERT_EQ(directives[1], ".NODESET V(out)=1.2");
}

TEST(OpSimulationParametersChecks, generates_generic_wildcards_via_print_parameters) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"V(*)", "I(*)", "P(*)"}, {});
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {}, print_params);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    bool has_print_dc = false;
    for (const auto& directive : directives) {
        if (directive.find(".PRINT DC") == 0) {
            has_print_dc = true;
            ASSERT_NE(directive.find("V(*)"), std::string::npos);
            ASSERT_NE(directive.find("I(*)"), std::string::npos);
            ASSERT_NE(directive.find("P(*)"), std::string::npos);
        }
    }
    ASSERT_TRUE(has_print_dc);
}

TEST(OpSimulationParametersChecks, generates_bjt_lead_wildcards_via_print_parameters) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"IB(*)", "IC(*)", "IE(*)", "IS(*)"}, {});
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {}, print_params);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    bool has_print_dc = false;
    for (const auto& directive : directives) {
        if (directive.find(".PRINT DC") == 0) {
            has_print_dc = true;
            ASSERT_NE(directive.find("IB(*)"), std::string::npos);
            ASSERT_NE(directive.find("IC(*)"), std::string::npos);
            ASSERT_NE(directive.find("IE(*)"), std::string::npos);
            ASSERT_NE(directive.find("IS(*)"), std::string::npos);
        }
    }
    ASSERT_TRUE(has_print_dc);
}

TEST(OpSimulationParametersChecks, generates_fet_lead_wildcards_via_print_parameters) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"IB(*)", "ID(*)", "IG(*)", "IS(*)"}, {});
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {}, print_params);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    bool has_print_dc = false;
    for (const auto& directive : directives) {
        if (directive.find(".PRINT DC") == 0) {
            has_print_dc = true;
            ASSERT_NE(directive.find("IB(*)"), std::string::npos);
            ASSERT_NE(directive.find("ID(*)"), std::string::npos);
            ASSERT_NE(directive.find("IG(*)"), std::string::npos);
            ASSERT_NE(directive.find("IS(*)"), std::string::npos);
        }
    }
    ASSERT_TRUE(has_print_dc);
}

TEST(OpSimulationParametersChecks, w_star_normalizes_to_p_star_on_parse) {
    // arrange
    const std::vector<std::string> directives = {".OP", ".PRINT DC W(*)"};
    // act
    const auto result = OpSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->print_parameters.has_value());
    ASSERT_NE(std::find(result->print_parameters->output_variables.begin(), result->print_parameters->output_variables.end(), "P(*)"), result->print_parameters->output_variables.end());
    ASSERT_EQ(std::find(result->print_parameters->output_variables.begin(), result->print_parameters->output_variables.end(), "W(*)"), result->print_parameters->output_variables.end());
}

TEST(OpSimulationParametersChecks, print_parameters_round_trip) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"V(*)", "I(*)", "P(*)", "IB(*)", "IC(*)", "IE(*)", "IS(*)", "ID(*)", "IG(*)"}, {});
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {}, print_params);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    std::vector<std::string> reparsed_directives = {".OP"};
    reparsed_directives.insert(reparsed_directives.end(), directives.begin(), directives.end());
    const auto reparsed = OpSimulationParameters::from_xyce_directives(reparsed_directives);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_TRUE(reparsed->print_parameters.has_value());
    ASSERT_EQ(reparsed->print_parameters->print_type, "DC");
    ASSERT_EQ(reparsed->print_parameters->output_variables.size(), 9);
}

TEST(OpSimulationParametersChecks, print_parameters_emits_print_dc_directive) {
    // arrange
    const PrintParameters print_params("DC", "", "", {"V(*)"}, {});
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {}, print_params);
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

// ========================================================================================
// to_xyce_directives
// ========================================================================================

TEST(OpSimulationParametersChecks, generates_op_directive) {
    // arrange
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".OP");
}

TEST(OpSimulationParametersChecks, generates_print_dc_directive_with_wildcards) {
    // arrange
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {}, PrintParameters("DC", "", "", {"V(*)", "I(*)"}, {}));
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".OP");
    ASSERT_EQ(directives[1], ".PRINT DC V(*) I(*)");
}

TEST(OpSimulationParametersChecks, generates_nodeset_directives) {
    // arrange
    const OpSimulationParameters params(false, false, false, {}, "", "", false, "", "", {NodesetEntry("V1", "5"), NodesetEntry("V2", "3.3")}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 3);
    ASSERT_EQ(directives[0], ".OP");
    ASSERT_EQ(directives[1], ".NODESET V1=5");
    ASSERT_EQ(directives[2], ".NODESET V2=3.3");
}

// ========================================================================================
// equality operator
// ========================================================================================

TEST(OpSimulationParametersChecks, equality_operator_equal_params) {
    // arrange
    const OpSimulationParameters params1(false, false, false, {}, "", "", false, "", "", {}, std::nullopt);
    const OpSimulationParameters params2(false, false, false, {}, "", "", false, "", "", {}, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_TRUE(result);
}

TEST(OpSimulationParametersChecks, equality_operator_different_print_dc_enabled) {
    // arrange
    const OpSimulationParameters params1(true, false, false, {}, "", "", false, "", "", {}, std::nullopt);
    const OpSimulationParameters params2(false, false, false, {}, "", "", false, "", "", {}, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(OpSimulationParametersChecks, equality_operator_different_nodeset_entries) {
    // arrange
    const OpSimulationParameters params1(false, false, false, {}, "", "", false, "", "", {NodesetEntry("V1", "5")}, std::nullopt);
    const OpSimulationParameters params2(false, false, false, {}, "", "", false, "", "", {NodesetEntry("V2", "3.3")}, std::nullopt);
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

TEST(OpSimulationParametersChecks, equality_operator_different_print_parameters) {
    // arrange
    const OpSimulationParameters params1(false, false, false, {}, "", "", false, "", "", {}, PrintParameters("DC", "", "", {"V(*)"}, {}));
    const OpSimulationParameters params2(false, false, false, {}, "", "", false, "", "", {}, PrintParameters("DC", "", "", {"I(*)"}, {}));
    // act
    const bool result = params1 == params2;
    // assert
    ASSERT_FALSE(result);
}

// ========================================================================================
// Tests for from_xyce_directives edge cases
// ========================================================================================

TEST(OpSimulationParametersChecks, empty_directives_returns_none) {
    // arrange
    const std::vector<std::string> directives = {};
    // act
    const auto result = OpSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(OpSimulationParametersChecks, parses_print_dc_with_variables) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".PRINT DC V(1) I(R1)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_dc_enabled, true);
    ASSERT_EQ(result->print_dc_specific_variables.size(), 2);
    ASSERT_EQ(result->print_dc_specific_variables[0], "V(1)");
    ASSERT_EQ(result->print_dc_specific_variables[1], "I(R1)");
}

TEST(OpSimulationParametersChecks, parses_save) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".SAVE"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->save_enabled, true);
}

TEST(OpSimulationParametersChecks, parses_save_with_type_and_file) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".SAVE TYPE=IC FILE=mycircuit.ic"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->save_enabled, true);
    ASSERT_EQ(result->save_type, "IC");
    ASSERT_EQ(result->save_file, "mycircuit.ic");
}

TEST(OpSimulationParametersChecks, parses_save_with_level) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".SAVE LEVEL=none"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->save_enabled, true);
    ASSERT_EQ(result->save_level, "none");
}

TEST(OpSimulationParametersChecks, parses_save_defaults_to_nodeset) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".SAVE"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->save_type, "NODESET");
    ASSERT_EQ(result->save_file, "");
    ASSERT_EQ(result->save_level, "");
}

TEST(OpSimulationParametersChecks, parses_save_ignores_unsupported_time) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".SAVE TYPE=IC FILE=mycircuit.ic TIME=5.0"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->save_enabled, true);
    ASSERT_EQ(result->save_type, "IC");
    ASSERT_EQ(result->save_file, "mycircuit.ic");
    ASSERT_EQ(result->save_level, "");
}

TEST(OpSimulationParametersChecks, save_full_round_trip) {
    // arrange / act
    const auto parsed = OpSimulationParameters::from_xyce_directives({".OP", ".SAVE TYPE=IC FILE=mycircuit.ic LEVEL=none"});
    ASSERT_TRUE(parsed.has_value());
    const auto directives = parsed->to_xyce_directives(NetlistTopology{});
    // assert serialization preserves all save options
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".OP");
    ASSERT_EQ(directives[1], ".SAVE TYPE=IC FILE=mycircuit.ic LEVEL=none");
    // assert the emitted form reparses back to the same entry
    const auto reparsed = OpSimulationParameters::from_xyce_directives(directives);
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->save_enabled, true);
    ASSERT_EQ(reparsed->save_type, "IC");
    ASSERT_EQ(reparsed->save_file, "mycircuit.ic");
    ASSERT_EQ(reparsed->save_level, "none");
}

TEST(OpSimulationParametersChecks, generates_save_directive_with_level) {
    // arrange
    const OpSimulationParameters params(false, false, false, {}, "", "", true, "NODESET", "", {}, std::nullopt, "all");
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".OP");
    ASSERT_EQ(directives[1], ".SAVE TYPE=NODESET LEVEL=all");
}

TEST(OpSimulationParametersChecks, parses_nodeset) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".NODESET V(out)=1.2 V(in)=0.5"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodeset_entries.size(), 2);
    ASSERT_EQ(result->nodeset_entries[0].node, "out");
    ASSERT_EQ(result->nodeset_entries[0].voltage, "1.2");
    ASSERT_EQ(result->nodeset_entries[1].node, "in");
    ASSERT_EQ(result->nodeset_entries[1].voltage, "0.5");
}

TEST(OpSimulationParametersChecks, nodeset_ignores_pair_without_equals) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".NODESET INVALID"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodeset_entries.size(), 0);
}

TEST(OpSimulationParametersChecks, parses_nodeset_positional_form) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".NODESET 2 3.1"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodeset_entries.size(), 1);
    ASSERT_EQ(result->nodeset_entries[0].node, "2");
    ASSERT_EQ(result->nodeset_entries[0].voltage, "3.1");
}

TEST(OpSimulationParametersChecks, parses_nodeset_mixed_forms) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".NODESET V(2)=3.1 3 1.5"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodeset_entries.size(), 2);
    ASSERT_EQ(result->nodeset_entries[0].node, "2");
    ASSERT_EQ(result->nodeset_entries[0].voltage, "3.1");
    ASSERT_EQ(result->nodeset_entries[1].node, "3");
    ASSERT_EQ(result->nodeset_entries[1].voltage, "1.5");
}

TEST(OpSimulationParametersChecks, nodeset_positional_form_round_trips_as_v_form) {
    // arrange / act
    const auto parsed = OpSimulationParameters::from_xyce_directives({".OP", ".NODESET 2 3.1"});
    ASSERT_TRUE(parsed.has_value());
    const auto directives = parsed->to_xyce_directives(NetlistTopology{});
    // assert serialization uses the canonical V()= form
    ASSERT_EQ(directives.size(), 2);
    ASSERT_EQ(directives[0], ".OP");
    ASSERT_EQ(directives[1], ".NODESET V(2)=3.1");
    // assert the emitted form reparses back to the same entry
    const auto reparsed = OpSimulationParameters::from_xyce_directives(directives);
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_EQ(reparsed->nodeset_entries.size(), 1);
    ASSERT_EQ(reparsed->nodeset_entries[0].node, "2");
    ASSERT_EQ(reparsed->nodeset_entries[0].voltage, "3.1");
}

TEST(OpSimulationParametersChecks, nodeset_ignores_invalid_node_format) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".NODESET out=1.2"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodeset_entries.size(), 0);
}

TEST(OpSimulationParametersChecks, ignores_unknown_directive) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".TRAN 1ns 1ms"});
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(OpSimulationParametersChecks, ignores_print_non_dc) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".PRINT AC V(1)"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_dc_enabled, false);
}

TEST(OpSimulationParametersChecks, ignores_print_without_type) {
    // arrange / act
    const auto result = OpSimulationParameters::from_xyce_directives({".OP", ".PRINT"});
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->print_dc_enabled, false);
}

// ========================================================================================
// Tests for reference guide examples
// ========================================================================================

TEST(OpSimulationParametersChecks, reference_guide_example_basic) {
    // arrange
    const std::vector<std::string> directives = {".OP"};
    // act
    const auto result = OpSimulationParameters::from_xyce_directives(directives);
    // assert
    ASSERT_TRUE(result.has_value());
    const auto generated = result->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(generated.size(), 1);
    ASSERT_EQ(generated[0], ".OP");
}

TEST(OpSimulationParametersChecks, reference_guide_example_round_trip) {
    // arrange
    const std::vector<std::string> directives = {".OP"};
    // act
    const auto result = OpSimulationParameters::from_xyce_directives(directives);
    const auto regenerated = result->to_xyce_directives(NetlistTopology{});
    const auto reparsed = OpSimulationParameters::from_xyce_directives(regenerated);
    // assert
    ASSERT_TRUE(reparsed.has_value());
    const auto regenerated2 = reparsed->to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(regenerated2.size(), 1);
    ASSERT_EQ(regenerated2[0], ".OP");
}

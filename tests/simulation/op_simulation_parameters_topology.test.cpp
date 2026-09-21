#include <string>

#include <gtest/gtest.h>

#include "netlist/netlist.h"
#include "simulation/op_simulation_parameters.h"

TEST(OpSimulationParametersTopologyChecks, passes_through_node_wildcard_from_topology) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\nR2 2 1 200\nQ1 3 2 1 npn\n.END\n");
    const OpSimulationParameters params(true, true, false, {"V(*)"}, "", "", false, "", "", {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(topology);
    // assert
    ASSERT_GE(directives.size(), 1);
    // find the .PRINT DC line from the generated directives
    std::string print_line;
    for (const auto& d : directives) {
        if (d.find(".PRINT DC") == 0) {
            print_line = d;
            break;
        }
    }
    ASSERT_FALSE(print_line.empty());
    // the V(*) wildcard passes through to Xyce for native expansion
    ASSERT_NE(print_line.find("V(*)"), std::string::npos);
    // topology nodes are NOT injected by the plugin
    ASSERT_EQ(print_line.find("V(1)"), std::string::npos);
    ASSERT_EQ(print_line.find("V(2)"), std::string::npos);
    ASSERT_EQ(print_line.find("V(3)"), std::string::npos);
}

TEST(OpSimulationParametersTopologyChecks, passes_through_current_wildcard_from_topology) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\nC1 2 0 1u\n.END\n");
    const OpSimulationParameters params(true, false, true, {"I(*)"}, "", "", false, "", "", {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(topology);
    // assert
    // find the .PRINT DC line from the generated directives
    std::string print_line;
    for (const auto& d : directives) {
        if (d.find(".PRINT DC") == 0) {
            print_line = d;
            break;
        }
    }
    ASSERT_FALSE(print_line.empty());
    // the I(*) wildcard passes through to Xyce for native expansion
    ASSERT_NE(print_line.find("I(*)"), std::string::npos);
    // device currents are NOT injected by the plugin
    ASSERT_EQ(print_line.find("I(R1)"), std::string::npos);
    ASSERT_EQ(print_line.find("I(C1)"), std::string::npos);
}

TEST(OpSimulationParametersTopologyChecks, deduplicates_vars) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.END\n");
    const OpSimulationParameters params(true, true, true, {"V(1)"}, "", "", false, "", "", {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(topology);
    // assert
    // find the .PRINT DC line from the generated directives
    std::string print_line;
    for (const auto& d : directives) {
        if (d.find(".PRINT DC") == 0) {
            print_line = d;
            break;
        }
    }
    ASSERT_FALSE(print_line.empty());
    // V(1) should appear only once even though it's in both user vars and topology expansion
    size_t pos = 0;
    int count = 0;
    while ((pos = print_line.find("V(1)", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    ASSERT_EQ(count, 1);
}

TEST(OpSimulationParametersTopologyChecks, passes_through_no_topology) {
    // arrange
    const OpSimulationParameters params(true, false, false, {"V(1)"}, "", "", false, "", "", {}, std::nullopt);
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    // find the .PRINT DC line from the generated directives
    std::string print_line;
    for (const auto& d : directives) {
        if (d.find(".PRINT DC") == 0) {
            print_line = d;
            break;
        }
    }
    ASSERT_FALSE(print_line.empty());
    // no topology means no V(0) expansion, only user-supplied V(1) is present
    ASSERT_EQ(print_line.find("V(0)"), std::string::npos);
}

TEST(OpSimulationParametersTopologyChecks, passes_through_wildcards_when_print_parameters_set) {
    // arrange
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.END\n");
    const PrintParameters print_params("DC", "RAW", "", {"V(*)"}, {});
    const OpSimulationParameters params(false, true, true, {}, "", "", false, "", "", {}, print_params);
    // act
    const auto directives = params.to_xyce_directives(topology);
    // assert
    // find the .PRINT DC line from the generated directives
    std::string print_line;
    for (const auto& d : directives) {
        if (d.find(".PRINT DC") == 0) {
            print_line = d;
            break;
        }
    }
    ASSERT_FALSE(print_line.empty());
    // V(*) passes through to Xyce for native expansion
    ASSERT_NE(print_line.find("V(*)"), std::string::npos);
    // topology nodes are NOT injected by the plugin
    ASSERT_EQ(print_line.find("V(1)"), std::string::npos);
}

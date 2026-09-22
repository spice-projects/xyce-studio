#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../netlist/netlist.h"
#include "ic_parameters.h"
#include "print_parameters.h"

// entry for a nodeset directive
struct NodesetEntry
{
    // construct a nodeset entry from node and voltage
    NodesetEntry(std::string node, std::string voltage);

    // node name
    std::string node;
    // voltage value
    std::string voltage;

    // equality operator
    [[nodiscard]] bool operator==(const NodesetEntry& other) const;
};

// OP simulation parameters class — parses and serializes Xyce .OP directives
class OpSimulationParameters
{
public:
    // construct an OP simulation parameters instance from individual fields
    OpSimulationParameters(bool print_dc_enabled, bool print_dc_all_nodes, bool print_dc_all_currents, std::vector<std::string> print_dc_specific_variables, std::string print_dc_format, std::string print_dc_file, bool save_enabled, std::string save_type, std::string save_file, std::vector<NodesetEntry> nodeset_entries, std::optional<PrintParameters> print_parameters, std::string save_level = "");

    // parse all directives into an OpSimulationParameters instance;
    // returns nullopt when no .OP directive is found
    [[nodiscard]] static std::optional<OpSimulationParameters> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // equality operator
    [[nodiscard]] bool operator==(const OpSimulationParameters& other) const;

    // print DC enabled flag
    bool print_dc_enabled;
    // print all DC nodes flag
    bool print_dc_all_nodes;
    // print all DC currents flag
    bool print_dc_all_currents;
    // specific DC variables to print
    std::vector<std::string> print_dc_specific_variables;
    // DC print format
    std::string print_dc_format;
    // DC print file
    std::string print_dc_file;
    // save enabled flag
    bool save_enabled;
    // save type (e.g. "NODESET")
    std::string save_type;
    // save file
    std::string save_file;
    // save level (e.g. "all", "none"); empty when not specified
    std::string save_level;
    // nodeset entries
    std::vector<NodesetEntry> nodeset_entries;
    // optional print parameters
    std::optional<PrintParameters> print_parameters;
};

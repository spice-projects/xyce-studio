#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../core/util.h"
#include "op_simulation_parameters.h"
#include "print_parameters.h"

NodesetEntry::NodesetEntry(std::string node, std::string voltage) :
    node(std::move(node)), voltage(std::move(voltage)) {}

bool NodesetEntry::operator==(const NodesetEntry& other) const {
    // compare all fields for equality
    return node == other.node && voltage == other.voltage;
}

OpSimulationParameters::OpSimulationParameters(bool print_dc_enabled, bool print_dc_all_nodes, bool print_dc_all_currents, std::vector<std::string> print_dc_specific_variables, std::string print_dc_format, std::string print_dc_file, bool save_enabled, std::string save_type, std::string save_file, std::vector<NodesetEntry> nodeset_entries, std::optional<PrintParameters> print_parameters, std::string save_level) :
    print_dc_enabled(print_dc_enabled), print_dc_all_nodes(print_dc_all_nodes), print_dc_all_currents(print_dc_all_currents), print_dc_specific_variables(std::move(print_dc_specific_variables)), print_dc_format(std::move(print_dc_format)), print_dc_file(std::move(print_dc_file)), save_enabled(save_enabled), save_type(std::move(save_type)), save_file(std::move(save_file)), save_level(std::move(save_level)), nodeset_entries(std::move(nodeset_entries)), print_parameters(std::move(print_parameters)) {}

std::optional<OpSimulationParameters> OpSimulationParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init flag
    bool print_dc_enabled = false;
    // init list
    std::vector<std::string> print_dc_vars;
    // init parsed print parameters object
    std::optional<PrintParameters> print_parameters_parsed;
    // init flag
    bool save_enabled = false;
    // save options default per reference guide (TYPE=NODESET); empty FILE/LEVEL mean not specified
    std::string save_type = "NODESET";
    std::string save_file;
    std::string save_level;
    // init list
    std::vector<NodesetEntry> nodeset_entries;

    // flag indicating whether a valid directive was found
    bool found = false;

    // parse directives
    for (const auto& directive : directives) {
        // tokenize the directive
        const auto tokens = tokenize(directive);

        // skip empty directives
        if (tokens.empty()) {
            continue;
        }

        const std::string cmd = to_upper(tokens[0]);

        // check .OP directive
        if (cmd == ".OP") {
            // set flag
            found = true;
            continue;
        }

        // handle print dc
        if (cmd == ".PRINT" && tokens.size() > 1 && to_upper(tokens[1]) == "DC") {
            // set enabled
            print_dc_enabled = true;
            // parse via PrintParameters for structured wildcard/format access
            const auto print_parameters_parsed_opt = PrintParameters::from_xyce_statement(directive);
            if (print_parameters_parsed_opt) {
                print_parameters_parsed = *print_parameters_parsed_opt;
            }
            // also collect raw vars for backward-compatible list
            for (size_t i = 2; i < tokens.size(); ++i) {
                print_dc_vars.push_back(std::string(tokens[i]));
            }
            continue;
        }

        // handle save
        if (cmd == ".SAVE") {
            // set enabled
            save_enabled = true;
            // parse the optional TYPE, FILE and LEVEL arguments
            for (size_t i = 1; i < tokens.size(); ++i) {
                const auto& token = tokens[i];
                const auto eq_pos = token.find('=');
                if (eq_pos == std::string::npos) {
                    continue;
                }
                const auto key = to_upper(token.substr(0, eq_pos));
                const auto value = token.substr(eq_pos + 1);
                // handle TYPE
                if (key == "TYPE") {
                    save_type = value;
                }
                // handle FILE
                else if (key == "FILE") {
                    save_file = value;
                }
                // handle LEVEL (HSPICE compatibility, default all)
                else if (key == "LEVEL") {
                    save_level = value;
                }
                // TIME is an HSPICE compatibility parameter and unsupported in Xyce; ignore it
            }
            continue;
        }

        // handle nodeset
        if (cmd == ".NODESET") {
            // process pairs
            for (size_t i = 1; i < tokens.size(); ++i) {
                const auto& pair = tokens[i];
                // check if pair valid
                if (pair.find('=') != std::string::npos) {
                    // split node and voltage
                    const auto eq_pos = pair.find('=');
                    const auto node_part = pair.substr(0, eq_pos);
                    const auto voltage = pair.substr(eq_pos + 1);
                    // validate and extract the node
                    std::string node;
                    if (node_part.substr(0, 2) == "V(" && node_part.back() == ')') {
                        // V(node)=voltage form
                        node = node_part.substr(2, node_part.length() - 3);
                    }
                    else if (node_part.size() >= 1 && (node_part[0] == 'V' || node_part[0] == 'v')) {
                        // bare voltage node name form (e.g. V1=5)
                        node = node_part;
                    }
                    else {
                        // invalid node format
                        continue;
                    }
                    // append entry
                    nodeset_entries.emplace_back(std::string(node), std::string(voltage));
                }
                else if (i + 1 < tokens.size()) {
                    // node value pair form (e.g. .NODESET 2 3.1)
                    nodeset_entries.emplace_back(std::string(pair), std::string(tokens[i + 1]));
                    ++i;
                }
            }
            continue;
        }
    }

    // return instance if a valid directive was found
    if (!found) {
        return std::nullopt;
    }

    return OpSimulationParameters(print_dc_enabled, false, false, print_dc_vars, "", "", save_enabled, save_type, save_file, nodeset_entries, print_parameters_parsed, save_level);
}

std::vector<std::string> OpSimulationParameters::to_xyce_directives(const NetlistTopology& topology) const {
    // init output directive list
    std::vector<std::string> directives;

    // topology reserved for future wildcard expansion; pass-through for now
    (void)topology;

    // start with the .OP directive
    directives.push_back(".OP");

    // use print_parameters when set (newer wildcard approach with topology expansion)
    if (print_parameters) {
        // emit the statement directly using the structured print parameters with topology
        directives.push_back(print_parameters->to_xyce_statement());
    }
    // check enabled
    else if (print_dc_enabled) {
        // start tokens
        std::string tokens = ".PRINT DC";
        // check format
        if (!print_dc_format.empty()) {
            // append format
            tokens += " FORMAT=" + print_dc_format;
        }
        // check file
        if (!print_dc_file.empty()) {
            // append file (quoted when the filename contains whitespace)
            tokens += " FILE=" + format_print_file_value(print_dc_file);
        }
        // start with custom vars
        std::vector<std::string> vars = print_dc_specific_variables;
        // de-duplicate preserving order
        std::vector<std::string> unique_vars;
        std::set<std::string> seen;
        for (const auto& var : vars) {
            if (seen.insert(var).second) {
                unique_vars.push_back(var);
            }
        }
        // add unique vars
        for (const auto& var : unique_vars) {
            tokens += " " + var;
        }
        directives.push_back(tokens);
    }

    // check save enabled
    if (save_enabled) {
        // build tokens
        std::string tokens = ".SAVE";
        // append type
        tokens += " TYPE=" + save_type;
        // check file
        if (!save_file.empty()) {
            // append file
            tokens += " FILE=" + save_file;
        }
        // check level
        if (!save_level.empty()) {
            // append level
            tokens += " LEVEL=" + save_level;
        }
        directives.push_back(tokens);
    }

    // check nodeset entries
    for (const auto& entry : nodeset_entries) {
        // format the node, wrapping in V(...) unless it is already a voltage node name
        std::string node = entry.node;
        if (node.empty() || (node[0] != 'V' && node[0] != 'v')) {
            node = "V(" + entry.node + ")";
        }
        // append one directive per entry
        directives.push_back(".NODESET " + node + "=" + entry.voltage);
    }

    // return directives
    return directives;
}

bool OpSimulationParameters::operator==(const OpSimulationParameters& other) const {
    // compare all fields for equality
    return print_dc_enabled == other.print_dc_enabled && print_dc_all_nodes == other.print_dc_all_nodes && print_dc_all_currents == other.print_dc_all_currents && print_dc_specific_variables == other.print_dc_specific_variables && print_dc_format == other.print_dc_format && print_dc_file == other.print_dc_file && save_enabled == other.save_enabled && save_type == other.save_type && save_file == other.save_file && save_level == other.save_level && nodeset_entries == other.nodeset_entries && print_parameters == other.print_parameters;
}

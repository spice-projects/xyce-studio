#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../core/util.h"
#include "netlist.h"

namespace
{

    // Y-prefix device types that have multi-letter type codes
    const std::vector<std::string_view> Y_PREFIXES = {"YMEMRISTOR", "YPDE", "YACC", "YLIN"};

    // standard node counts for single-letter device types
    // K (coupling) and Y (handled separately) have count 0
    const std::map<char, int, std::less<>> NODE_COUNTS = {
        {'B', 2}, {'C', 2}, {'D', 2}, {'E', 4}, {'F', 2}, {'G', 4}, {'H', 2}, {'I', 2}, {'J', 3}, {'K', 0}, {'L', 2}, {'M', 4}, {'O', 4}, {'P', 2}, {'Q', 3}, {'R', 2}, {'S', 4}, {'T', 4}, {'U', 2}, {'V', 2}, {'W', 2}, {'Z', 3},
    };

    // node counts for Y-prefix device types
    const std::map<std::string, int, std::less<>> Y_NODE_COUNTS = {
        {"YMEMRISTOR", 2},
        {"YLIN", 2},
        {"YACC", 3},
        {"YPDE", 0},
    };

    // directives that are recognized as simulation-control and stored separately
    const std::vector<std::string_view> SIMULATION_DIRECTIVES = {
        ".OP", ".PRINT", ".SAVE", ".NODESET", ".DC", ".TRAN", ".FFT", ".FOUR", ".AC", ".LIN", ".HB", ".NOISE", ".MEASURE", ".MEAS", ".SENS", ".PCE", ".IC", ".DCVOLT", ".STEP", ".DATA", ".ENDDATA",
    };

    // .OPTIONS packages that are managed and stored as directives
    const std::vector<std::string_view> MANAGED_OPTIONS_PACKAGES = {
        "HBINT", "NONLIN-HB", "LINSOL-HB", "DEVICE", "TIMEINT", "NONLIN", "LINSOL", "FFT", "MEASURE", "PCES",
    };

    // join lines that start with '+' into the preceding logical line
    std::vector<std::string> join_continuation_lines(const std::vector<std::string_view>& raw_lines) {
        // result container for joined logical lines
        std::vector<std::string> joined;
        // process each raw line from the input
        for (const auto& line : raw_lines) {
            // strip leading whitespace from the raw line
            auto lstrip_line = line;
            lstrip_line.remove_prefix(std::min(lstrip_line.find_first_not_of(" \t"), lstrip_line.size()));
            // check if this is a continuation line (starts with '+')
            if (!lstrip_line.empty() && lstrip_line[0] == '+') {
                // only append if there is a preceding line to extend
                if (!joined.empty()) {
                    // extract the content after '+' and strip whitespace
                    auto rest = lstrip_line.substr(1);
                    auto rest_stripped = rest;
                    auto pos = rest_stripped.find_first_not_of(" \t");
                    // remove leading whitespace from continuation content
                    if (pos != std::string_view::npos)
                        rest_stripped = rest_stripped.substr(pos);
                    auto rstrip_end = rest_stripped.find_last_not_of(" \t");
                    // remove trailing whitespace from continuation content
                    if (rstrip_end != std::string_view::npos)
                        rest_stripped = rest_stripped.substr(0, rstrip_end + 1);
                    // append stripped continuation content with a single space separator
                    joined.back() += " " + std::string(rest_stripped);
                }
            }
            else {
                // start a new logical line
                joined.emplace_back(line);
            }
        }
        return joined;
    }

    // remove inline comments starting with ';' from a line
    std::string strip_inline_comment(const std::string& line) {
        // find the first semicolon which starts an inline comment
        auto idx = line.find(';');
        // return the portion before the comment if one was found
        if (idx != std::string::npos)
            return line.substr(0, idx);
        // return the original line unchanged when no comment exists
        return line;
    }

    // determine the device type letter (or full Y-prefix) from an uppercased name
    std::string get_type_letter(const std::string& upper_name) {
        // check Y-prefixes first (e.g. YMEMRISTOR, YACC) before falling back to single letter
        for (const auto& prefix : Y_PREFIXES) {
            // compare the prefix against the start of the device name
            if (upper_name.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), upper_name.begin()))
                return std::string(prefix);
        }
        // fall back to the first character of the device name
        return upper_name.substr(0, 1);
    }

    // extract subcircuit instance (X) node names, stopping before PARAMS:
    std::vector<std::string> extract_x_nodes(const std::vector<std::string_view>& fields) {
        // find the index of the PARAMS: keyword (if present)
        int params_idx = -1;
        // scan through all fields looking for PARAMS:
        for (size_t i = 0; i < fields.size(); ++i) {
            auto upper = to_upper(fields[i]);
            // check for exact or prefix match on PARAMS:
            if (upper == "PARAMS:" || upper.find("PARAMS:") == 0) {
                params_idx = static_cast<int>(i);
                break;
            }
        }
        // determine how many fields are node names (everything before PARAMS:)
        size_t relevant_end = (params_idx >= 0) ? static_cast<size_t>(params_idx) : fields.size();
        // need at least one node after the instance name
        if (relevant_end <= 1)
            return {};
        // collect all fields before PARAMS: as node names
        std::vector<std::string> nodes;
        nodes.reserve(relevant_end - 1);
        // skip field 0 (the instance name like XU1), take the rest as nodes
        for (size_t i = 0; i < relevant_end - 1; ++i)
            nodes.emplace_back(fields[i]);
        // exit
        return nodes;
    }

    // extract node names from device tokens based on the device type letter
    std::vector<std::string> extract_nodes(const std::string& type_letter, const std::vector<std::string_view>& tokens) {
        // skip the first token (device name), keep the rest as potential node fields
        std::vector<std::string_view> fields(tokens.begin() + 1, tokens.end());
        // handle subcircuit instances (X devices) separately
        if (type_letter == "X")
            return extract_x_nodes(fields);
        // check Y-prefix types (YMEMRISTOR, YLIN, YACC, YPDE) for their specific node counts
        auto y_it = Y_NODE_COUNTS.find(type_letter);
        if (y_it != Y_NODE_COUNTS.end()) {
            int count = y_it->second;
            // return empty for types with zero node count (e.g. YPDE)
            if (count <= 0)
                return {};
            // extract exactly 'count' node names from the beginning of fields
            std::vector<std::string> nodes;
            nodes.reserve(static_cast<size_t>(count));
            for (size_t i = 0; i < static_cast<size_t>(count) && i < fields.size(); ++i)
                nodes.emplace_back(fields[i]);
            // exit
            return nodes;
        }
        // fall back to single-letter type lookup for standard devices
        if (!type_letter.empty()) {
            auto it = NODE_COUNTS.find(type_letter[0]);
            if (it != NODE_COUNTS.end()) {
                int count = it->second;
                // return empty for types with zero node count
                if (count <= 0)
                    return {};
                // extract exactly 'count' node names from the beginning of fields
                std::vector<std::string> nodes;
                nodes.reserve(static_cast<size_t>(count));
                for (size_t i = 0; i < static_cast<size_t>(count) && i < fields.size(); ++i)
                    nodes.emplace_back(fields[i]);
                // exit
                return nodes;
            }
        }
        // unknown device type yields no node information
        return {};
    }

} // anonymous namespace

std::pair<std::string, NetlistTopology> parse_netlist(std::string_view text) {
    // split the input text into individual lines
    auto raw_lines = split_by(text, '\n');
    // join continuation lines (those starting with '+') into logical lines
    auto logical_lines = join_continuation_lines(raw_lines);
    // remove inline comments (text after ';') from every logical line
    for (auto& line : logical_lines)
        line = strip_inline_comment(line);
    // the title extracted from the first line or .TITLE directive
    std::string title;
    // devices found outside any .SUBCKT block
    std::vector<Device> top_level_devices;
    // set of all unique top-level node names
    std::set<std::string> top_level_nodes;
    // map from subcircuit name to its parsed definition
    std::map<std::string, SubcircuitDefinition> subcircuit_definitions;
    // nodes declared as global (via .GLOBAL or $G_ prefix)
    std::set<std::string> global_nodes;
    // simulation directives extracted from the netlist
    std::vector<std::string> directives;
    // directives that should pass through without re-insertion
    std::vector<std::string> passthrough_directives;
    // pointer to the currently open subcircuit definition (nullptr when at top level)
    SubcircuitDefinition* current_subckt = nullptr;
    // sanitized netlist lines being built during parsing
    std::vector<std::string> netlist;
    // flag indicating whether we are processing the very first line
    bool first_line = true;
    // process each logical line of the netlist
    for (const auto& line : logical_lines) {
        // strip leading whitespace from the line
        auto stripped = line;
        auto non_space = stripped.find_first_not_of(" \t");
        // remove leading spaces and tabs
        if (non_space != std::string::npos && non_space > 0)
            stripped = stripped.substr(non_space);
        // strip trailing whitespace from the line
        auto rstrip_end = stripped.find_last_not_of(" \t");
        if (rstrip_end != std::string::npos)
            stripped = stripped.substr(0, rstrip_end + 1);
        // preserve empty lines and skip further processing for them
        if (stripped.empty()) {
            // append line
            netlist.push_back(stripped);
            // next
            continue;
        }
        // handle the first line specially (it becomes the title)
        if (first_line) {
            // reset flag
            first_line = false;
            // tokenize the first line to check for .TITLE directive
            auto toks = tokenize(stripped);
            // check if the first line is a .TITLE directive
            if (!toks.empty() && to_upper(toks[0]) == ".TITLE") {
                // reconstruct the title text from tokens after .TITLE
                std::string title_text;
                for (size_t i = 1; i < toks.size(); ++i) {
                    if (i > 1)
                        title_text += " ";
                    // append the token to the title text
                    title_text += toks[i];
                }
                title = title_text;
                // store the sanitized .TITLE line
                netlist.push_back(title_text.empty() ? ".TITLE" : ".TITLE " + title_text);
            }
            else {
                // first line without .TITLE is the title itself
                title = stripped;
                netlist.push_back(stripped);
            }
            continue;
        }
        // preserve comment lines (starting with '*') as-is
        if (stripped[0] == '*') {
            // append comment line
            netlist.push_back(stripped);
            // next
            continue;
        }
        // tokenize the line for further processing
        auto tokens = tokenize(stripped);
        // preserve lines with no tokens (e.g. pure whitespace after comment removal)
        if (tokens.empty()) {
            // append line
            netlist.push_back(stripped);
            // next
            continue;
        }
        // uppercase the first token for directive matching
        auto first_upper = to_upper(tokens[0]);
        // stop parsing when .END is encountered (short-circuit)
        if (first_upper == ".END") {
            // append .END to the sanitized netlist
            netlist.push_back(".END");
            // exit the parsing loop
            break;
        }
        // handle all lines starting with '.' (directives)
        if (stripped[0] == '.') {
            // handle .TITLE directive (may appear on non-first lines too)
            if (first_upper == ".TITLE") {
                // reconstruct the title text from remaining tokens
                std::string title_text;
                for (size_t i = 1; i < tokens.size(); ++i) {
                    if (i > 1) {
                        title_text += " ";
                    }
                    title_text += std::string(tokens[i]);
                }
                title = title_text;
                // store the sanitized .TITLE line
                netlist.push_back(title_text.empty() ? ".TITLE" : ".TITLE " + title_text);
                // next
                continue;
            }
            // check if the directive is a recognized simulation directive
            if (std::find_if(SIMULATION_DIRECTIVES.begin(), SIMULATION_DIRECTIVES.end(), [&first_upper](std::string_view d) { return first_upper == d; }) != SIMULATION_DIRECTIVES.end()) {
                // store as a managed directive
                directives.push_back(stripped);
                // next
                continue;
            }
            // check for managed .OPTIONS package directives
            if (first_upper == ".OPTIONS" && tokens.size() > 1) {
                // extract the package name (second token) and check against managed list
                auto pkg = to_upper(tokens[1]);
                if (std::find(MANAGED_OPTIONS_PACKAGES.begin(), MANAGED_OPTIONS_PACKAGES.end(), pkg) != MANAGED_OPTIONS_PACKAGES.end()) {
                    // store as a managed directive
                    directives.push_back(stripped);
                    // next
                    continue;
                }
            }
            // check for .PREPROCESS REPLACEGROUND directive
            if (first_upper == ".PREPROCESS" && tokens.size() > 2 && to_upper(tokens[1]) == "REPLACEGROUND") {
                // store as a managed directive
                directives.push_back(stripped);
                // next
                continue;
            }
            // handle .SUBCKT (subcircuit definition start)
            if (first_upper == ".SUBCKT") {
                // reconstruct the full .SUBCKT line
                std::string subckt_line = ".SUBCKT";
                for (size_t i = 1; i < tokens.size(); ++i)
                    subckt_line += " " + std::string(tokens[i]);
                // append the .SUBCKT line to the sanitized netlist
                netlist.push_back(subckt_line);
                // ensure there is at least a subcircuit name
                if (tokens.size() >= 2) {
                    // extract the subcircuit name (uppercased)
                    auto subckt_name = to_upper(tokens[1]);
                    // collect port names (everything before PARAMS:)
                    std::vector<std::string> ports;
                    for (size_t i = 2; i < tokens.size(); ++i) {
                        auto t_upper = to_upper(tokens[i]);
                        // stop collecting at PARAMS: keyword
                        if (t_upper == "PARAMS:" || t_upper.find("PARAMS:") == 0) {
                            break;
                        }
                        ports.push_back(t_upper);
                    }
                    // create and store the subcircuit definition
                    SubcircuitDefinition def;
                    def.m_name = subckt_name;
                    def.m_ports = std::move(ports);
                    subcircuit_definitions[subckt_name] = std::move(def);
                    // update the current subcircuit pointer for device collection
                    current_subckt = &subcircuit_definitions[subckt_name];
                }
                continue;
            }
            // handle .ENDS (subcircuit definition end)
            if (first_upper == ".ENDS") {
                // append .ENDS to the sanitized netlist
                netlist.push_back(".ENDS");
                // reset the current subcircuit pointer back to top level
                current_subckt = nullptr;
                // next
                continue;
            }
            // handle .GLOBAL (global node declaration)
            if (first_upper == ".GLOBAL") {
                // reconstruct the full .GLOBAL line
                std::string global_line = ".GLOBAL";
                for (size_t i = 1; i < tokens.size(); ++i)
                    global_line += " " + std::string(tokens[i]);
                // append the .GLOBAL line to the sanitized netlist
                netlist.push_back(global_line);
                // add each listed node to the global set
                for (size_t i = 1; i < tokens.size(); ++i)
                    global_nodes.insert(to_upper(tokens[i]));
                // next
                continue;
            }
            // any other dot-command is treated as a passthrough directive
            std::string other_directive = first_upper;
            for (size_t i = 1; i < tokens.size(); ++i)
                other_directive += " " + std::string(tokens[i]);
            // store the passthrough directive for later re-insertion
            netlist.push_back(other_directive);
            // next
            continue;
        }
        // preserve non-directive lines in the sanitized netlist
        netlist.push_back(stripped);
        // extract device information from the tokenized line
        auto upper_name = to_upper(tokens[0]);
        auto type_letter = get_type_letter(upper_name);
        auto raw_nodes = extract_nodes(type_letter, tokens);
        // uppercase all extracted node names
        std::vector<std::string> node_names;
        node_names.reserve(raw_nodes.size());
        for (const auto& n : raw_nodes)
            node_names.push_back(to_upper(n));
        // build the device structure
        Device device;
        device.m_name = upper_name;
        device.m_type_letter = type_letter;
        device.m_nodes = node_names;
        // assign the device to the current scope (subcircuit or top-level)
        if (current_subckt != nullptr) {
            // add device to the current subcircuit's device list
            current_subckt->m_devices.push_back(std::move(device));
        }
        else {
            // add nodes to the top-level node set
            for (const auto& node : node_names)
                top_level_nodes.insert(node);
            // add the device to the top-level devices list
            top_level_devices.push_back(std::move(device));
        }
        // detect $G_ prefixed nodes that implicitly become global
        for (const auto& node : node_names) {
            // $G prefix indicates an implicit global connection
            if (node.size() >= 2 && node[0] == '$' && node[1] == 'G')
                global_nodes.insert(node);
        }
    }
    // assemble the sanitized netlist into a single string with newlines
    auto netlist_str = std::string();
    for (size_t i = 0; i < netlist.size(); ++i) {
        if (i > 0) {
            netlist_str += '\n';
        }
        netlist_str += netlist[i];
    }
    netlist_str += '\n';
    // construct the topology result
    NetlistTopology topology;
    topology.m_title = std::move(title);
    topology.m_devices = std::move(top_level_devices);
    topology.m_nodes = std::move(top_level_nodes);
    topology.m_subcircuit_definitions = std::move(subcircuit_definitions);
    topology.m_global_nodes = std::move(global_nodes);
    topology.m_directives = std::move(directives);
    topology.m_passthrough_directives = std::move(passthrough_directives);
    // return the sanitized netlist string and the parsed topology
    return {std::move(netlist_str), std::move(topology)};
}

std::string build_final_netlist(std::string_view netlist, const std::vector<std::string>& directives, const std::vector<std::string>& passthrough) {
    // combine directives and passthrough into a single insertion block
    std::vector<std::string> combined;
    combined.reserve(directives.size() + passthrough.size());
    combined.insert(combined.end(), directives.begin(), directives.end());
    combined.insert(combined.end(), passthrough.begin(), passthrough.end());
    // return the netlist unchanged when there are no directives to insert
    if (combined.empty())
        return std::string(netlist);
    // build the directive block as a single string with newline separators
    std::string directive_block;
    for (size_t i = 0; i < combined.size(); ++i) {
        if (i > 0) {
            directive_block += '\n';
        }
        directive_block += combined[i];
    }
    // convert the netlist view to a mutable string for insertion
    auto netlist_str = std::string(netlist);
    // locate the .END marker in the netlist
    auto end_pos = netlist_str.find(".END\n");
    // insert the directive block before .END when the marker is found
    if (end_pos != std::string::npos) {
        netlist_str.replace(end_pos, 5, "\n" + directive_block + "\n\n.END\n");
    }
    return netlist_str;
}

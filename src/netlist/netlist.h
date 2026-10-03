#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// represents a single device in the netlist
struct Device
{
    // device name (uppercased, e.g. "R1", "Q2", "XU1")
    std::string m_name;
    // single-letter type or full Y-prefix (e.g. "R", "Q", "X", "YMEMRISTOR")
    std::string m_type_letter;
    // list of connected node names (uppercased)
    std::vector<std::string> m_nodes;
};

// definition of a .SUBCKT block
struct SubcircuitDefinition
{
    // subcircuit name (uppercased)
    std::string m_name;
    // port node names (uppercased)
    std::vector<std::string> m_ports;
    // devices inside the subcircuit
    std::vector<Device> m_devices;
};

// parsed topology of the netlist
// provides node sets, device lists, subcircuit definitions, and extracted directives
struct NetlistTopology
{
    // first line of the netlist (or .TITLE value)
    std::string m_title;
    // top-level (non-subcircuit) devices
    std::vector<Device> m_devices;
    // set of all top-level node names (uppercased)
    std::set<std::string> m_nodes;
    // map from subcircuit name to definition
    std::map<std::string, SubcircuitDefinition> m_subcircuit_definitions;
    // global nodes from .GLOBAL directives and $G_ prefixed nodes
    std::set<std::string> m_global_nodes;
    // simulation directives found by the parser (.OP, .PRINT, .TRAN, etc.)
    std::vector<std::string> m_directives;
    // directives that are stored but not re-inserted by build_final_netlist
    std::vector<std::string> m_passthrough_directives;
    // .PLOT directive lines declaring the default charts of a run; like every managed directive they are re-inserted before .END by build_final_netlist and never reach the simulator
    std::vector<std::string> m_plot_directives;
};

// parse a raw netlist text and return a sanitized netlist string + topology
[[nodiscard]] std::pair<std::string, NetlistTopology> parse_netlist(std::string_view text);

// insert directives before .END in a sanitized netlist string
[[nodiscard]] std::string build_final_netlist(std::string_view netlist, const std::vector<std::string>& directives, const std::vector<std::string>& passthrough);

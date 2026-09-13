#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../netlist/netlist.h"

// print parameters class — parses and serializes Xyce .PRINT directives
class PrintParameters
{
public:
    // construct a print parameters instance from individual fields
    PrintParameters(std::string print_type, std::string print_format, std::string print_file, std::vector<std::string> output_variables, std::vector<std::string> extra_options);

    // parse a .PRINT directive string into a PrintParameters instance;
    // returns nullopt when the string is not a valid .PRINT statement
    [[nodiscard]] static std::optional<PrintParameters> from_xyce_statement(const std::string& print_statement);

    // serialize this instance back to a .PRINT directive string
    [[nodiscard]] std::string to_xyce_statement() const;

    // serialize with topology-aware wildcard expansion: V(*), I(*), P(*) are
    // expanded to concrete node/device entries; null topology passes through
    [[nodiscard]] std::string to_xyce_statement(const NetlistTopology* topology) const;

    // equality comparison
    [[nodiscard]] bool operator==(const PrintParameters& other) const;

    // print type (e.g. "TRAN", "AC", "DC")
    std::string print_type;
    // output format (e.g. "RAW", "CSV"); empty when not specified
    std::string print_format;
    // output file name; empty when not specified
    std::string print_file;
    // ordered list of output variable tokens
    std::vector<std::string> output_variables;
    // extra option tokens (e.g. WIDTH=20, PRECISION=12)
    std::vector<std::string> extra_options;
};

// tokenize a .PRINT statement respecting brace-enclosed expressions and quotes
[[nodiscard]] std::vector<std::string> tokenize_print_statement(const std::string& print_statement);

// remove the FILE= option from a .PRINT statement that produces RAW output
// (FORMAT=RAW or no FORMAT option); any other statement is returned unchanged.
// used to force Xyce to write the RAW file to its default netlist-derived
// location so the simulation never rewrites a file the application holds open
[[nodiscard]] std::string strip_print_file_option(const std::string& print_statement);

#pragma once

#include <filesystem>
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

    // parse a .PRINT directive string, returns nullopt when the string is not a valid .PRINT statement
    [[nodiscard]] static std::optional<PrintParameters> from_xyce_statement(const std::string& print_statement);

    // serialize this instance back to a .PRINT directive string
    [[nodiscard]] std::string to_xyce_statement() const;

    // serialize with topology-aware wildcard expansion, a null topology passes the statement through unchanged
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

// a FILE= destination recorded from a .PRINT directive while preparing the run netlist, paired with the default file the simulation produces for the directive
struct PrintOutputCopy
{
    // default file produced for the .PRINT directive
    std::filesystem::path produced_file;
    // recorded FILE= destination of the .PRINT directive
    std::filesystem::path destination_file;
};

// tokenize a .PRINT statement respecting brace-enclosed expressions and quotes
[[nodiscard]] std::vector<std::string> tokenize_print_statement(const std::string& print_statement);

// remove the FILE= option from a .PRINT statement so Xyce writes the default file for the print type and format and never rewrites a file the application holds open
[[nodiscard]] std::string strip_print_file_option(const std::string& print_statement);

// record the FILE= option of every .PRINT directive with the default file the simulation produces for its print type and format, the recorded destinations are copied after the run
[[nodiscard]] std::vector<PrintOutputCopy> collect_print_file_copies(const std::vector<std::string>& directives, const std::filesystem::path& netlist_file_path, const std::filesystem::path& working_directory);

// strip matching outer quote characters from an option value, used to normalize a FILE= value so a quoted filename shows up unquoted in the dialog and resolves to the actual file
[[nodiscard]] std::string strip_outer_quotes(std::string value);

// remove the output variable tokens the given print type cannot produce per the Xyce reference guide (power and device lead currents), any other print type returns the variables unchanged
[[nodiscard]] std::vector<std::string> sanitize_print_output_variables(const std::string& print_type, const std::vector<std::string>& output_variables);

// format a FILE= option value, values containing whitespace are quoted so the statement survives tokenization
[[nodiscard]] std::string format_print_file_value(const std::string& file);

// return the Xyce .prn output file suffix for a .PRINT type per the reference guide (AC → .FD.prn, AC_IC → .TD.prn, HB frequency data → .HB.FD.prn, HB time data → .HB.TD.prn, HB initial conditions → .hb_ic.prn, HB startup → .startup.prn, ES → .ES.prn, SENS → .SENS.prn, TRANADJOINT → .TRADJ.prn, PCE → .PCE.prn, everything else → .prn)
[[nodiscard]] std::string prn_output_suffix(const std::string& print_type);

// return the Xyce .csv output file suffix for a .PRINT FORMAT=CSV print type per the reference guide tables 2-19 to 2-29, everything else appends .csv straight to the netlist name
[[nodiscard]] std::string csv_output_suffix(const std::string& print_type);

// return the Xyce .dat output file suffix for a .PRINT FORMAT=TECPLOT print type per the reference guide tables 2-19 to 2-29, everything else appends .dat straight to the netlist name
[[nodiscard]] std::string tecplot_output_suffix(const std::string& print_type);

// compute the default file Xyce writes for a .PRINT statement without its FILE= option (RAW → <netlist>.raw, PROBE → <netlist>.csd with .TD.csd for AC_IC, CSV and TECPLOT append their own extension, every other format including the unspecified default appends the prn suffix for the print type)
[[nodiscard]] std::filesystem::path default_print_output_file(const PrintParameters& print_parameters, const std::filesystem::path& netlist_file_path);

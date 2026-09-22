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

// remove the FILE= option from a .PRINT statement that produces RAW or PROBE output
// (FORMAT=RAW, FORMAT=PROBE or no FORMAT option); any other statement is returned unchanged.
// used to force Xyce to write the output file to its default netlist-derived
// location so the simulation never rewrites a file the application holds open
[[nodiscard]] std::string strip_print_file_option(const std::string& print_statement);

// strip matching outer quote characters from an option value; used to normalize
// a FILE= value so a quoted filename shows up unquoted in the dialog and
// resolves to the actual file when composing the output copy destination
[[nodiscard]] std::string strip_outer_quotes(std::string value);

// remove the output variable tokens the given print type cannot produce per
// the Xyce reference guide: power (P(*)/W(*)) and device lead currents
// (IB/IC/IE/IS/ID/IG) are unsupported for AC, NOISE and HB prints, which only
// carry voltages and branch currents (V, E, H, L devices, voltage-form B);
// any other print type returns the variables unchanged
[[nodiscard]] std::vector<std::string> sanitize_print_output_variables(const std::string& print_type, const std::vector<std::string>& output_variables);

// format a FILE= option value: values containing whitespace are quoted so the
// statement survives tokenization; already-quoted values pass through as-is
[[nodiscard]] std::string format_print_file_value(const std::string& file);

// return the Xyce .prn output file suffix for a .PRINT type per the reference
// guide: AC → .FD.prn, AC_IC → .TD.prn, HB frequency data → .HB.FD.prn, HB
// time data → .HB.TD.prn, HB initial conditions → .hb_ic.prn, HB startup →
// .startup.prn, ES → .ES.prn, SENS → .SENS.prn, TRANADJOINT → .TRADJ.prn,
// PCE → .PCE.prn, everything else (DC, TRAN, NOISE, HOMOTOPY, etc.) → .prn
[[nodiscard]] std::string prn_output_suffix(const std::string& print_type);

// return the Xyce .csv output file suffix for a .PRINT FORMAT=CSV print type
// per the reference guide (tables 2-19 to 2-29): AC → .FD.csv, AC_IC →
// .TD.csv, HB frequency data → .HB.FD.csv, HB time data → .HB.TD.csv, HB
// initial conditions → .hb_ic.csv, HB startup → .startup.csv, NOISE →
// .NOISE.csv, HOMOTOPY → .HOMOTOPY.csv, SENS → .SENS.csv, TRANADJOINT →
// .TRADJ.csv, ES → .ES.csv, PCE → .PCE.csv, everything else (DC, TRAN, ...) →
// .csv appended straight to the netlist name
[[nodiscard]] std::string csv_output_suffix(const std::string& print_type);

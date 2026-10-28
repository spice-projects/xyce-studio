#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/util.h"
#include "xyce_output_file.h"
#include "xyce_print_table.h"
#include "xyce_prn_file.h"

namespace
{
    // format enumeration for PRN files
    enum class PrnFormat
    {
        STD,
        NOINDEX,
        GNUPLOT,
        SPLOT
    };

    // return a format string from the PrnFormat enum: STD is the fallback for
    // the value every file without an explicit marker resolves to
    std::string format_string(PrnFormat format) {
        // check for the NOINDEX format
        if (format == PrnFormat::NOINDEX)
            return "NOINDEX";
        // check for the GNUPLOT format
        if (format == PrnFormat::GNUPLOT)
            return "GNUPLOT";
        // check for the SPLOT format
        if (format == PrnFormat::SPLOT)
            return "SPLOT";
        // STD format
        return "STD";
    }

    // detect the plot type from the output file name: Xyce appends analysis-specific suffixes to the netlist name (reference guide, print analysis tables 2-19 to 2-29) and those suffixes are the only analysis-type marker a prn file carries; TRAN and DC share the plain .prn suffix, so for files without a well-known suffix the analysis markers in the netlist-derived name are used as a fallback
    PlotType detect_plot_type(const std::filesystem::path& filename) {
        // lowercase file name for the suffix comparisons
        const std::string name = to_lower(filename.filename().string());
        // frequency-domain output: AC, HB_FD and the HB frequency data
        if (name.find(".fd.prn") != std::string::npos || name.find(".fd.sens.prn") != std::string::npos)
            return PlotType::AC;
        // time-domain output: AC_IC, HB_TD, HB startup and initial conditions, homotopy, transient adjoint sensitivity and time-domain sensitivity
        if (name.find(".td.prn") != std::string::npos || name.find(".startup.prn") != std::string::npos || name.find(".hb_ic.prn") != std::string::npos || name.find(".homotopy.prn") != std::string::npos || name.find(".tradj.prn") != std::string::npos || name.find(".sens.prn") != std::string::npos)
            return PlotType::TRANSIENT;
        // noise analysis output
        if (name.find(".noise.prn") != std::string::npos)
            return PlotType::NOISE;
        // TRAN and DC share the plain .prn suffix and cannot be told apart from the file name alone: fall back to the analysis markers in the netlist-derived name
        if (name.find("tran") != std::string::npos)
            return PlotType::TRANSIENT;
        if (name.find("ac") != std::string::npos)
            return PlotType::AC;
        if (name.find("dc") != std::string::npos)
            return PlotType::DC;
        if (name.find("noise") != std::string::npos)
            return PlotType::NOISE;
        // unrecognized output
        return PlotType::UNKNOWN;
    }
} // namespace

std::optional<std::shared_ptr<XyceOutputFile>> xyce_prn_file_parser(const std::filesystem::path& filename) {
    // check if file exists
    if (!std::filesystem::exists(filename))
        return {};
    // track timing
    auto start_time = std::chrono::steady_clock::now();
    // log information
    spdlog::info("Parsing PRN file: {}", filename.string());
    // open the file
    std::ifstream file(filename);
    if (!file.is_open())
        return {};
    // reused line and token buffers: lines are streamed one by one and no whole-file copy is kept in memory
    std::string line;
    std::vector<std::string_view> tokens;
    // find the header line (first non-blank line)
    bool any_line = false;
    bool header_found = false;
    // process lines to find the header
    while (std::getline(file, line)) {
        // drop the carriage return left by Windows line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // at least a line is present in the file
        any_line = true;
        // skip blank lines
        if (line.empty())
            continue;
        // the first non-blank line is considered the header
        header_found = true;
        // exit the loop after finding the header
        break;
    }
    // check we found the header line
    if (!header_found) {
        // log a warning if the header line is missing
        if (any_line)
            spdlog::warn("PRN file has no header: {}", filename.string());
        else
            spdlog::warn("PRN file is empty: {}", filename.string());
        // exit
        return {};
    }
    // tokenize header line
    print_table_tokenize_whitespace(line, tokens);
    // check if the header line is empty
    if (tokens.empty()) {
        // log information
        spdlog::warn("PRN header line is empty: {}", filename.string());
        // exit
        return {};
    }
    // store column names from header, excluding FORMAT= tokens, and capture the explicit format and the index column presence
    std::vector<std::string> column_names;
    column_names.reserve(tokens.size());
    std::optional<PrnFormat> explicit_format;
    bool has_index = false;
    // process each token in the header line
    for (std::string_view token : tokens) {
        // lowercase token for comparison
        auto token_lowercase = to_lower(token);
        // check for a format token, it is not a column name
        if (token_lowercase.rfind("format=", 0) == 0) {
            // extract the format string after "FORMAT="
            std::string format_str = to_lower(token_lowercase.substr(7));
            // find format
            if (format_str == "noindex")
                explicit_format = PrnFormat::NOINDEX;
            else if (format_str == "gnuplot")
                explicit_format = PrnFormat::GNUPLOT;
            else if (format_str == "splot")
                explicit_format = PrnFormat::SPLOT;
            else if (format_str == "std")
                explicit_format = PrnFormat::STD;
            // next token
            continue;
        }
        // check for the index column (case-insensitive: the Xyce output writes the header with mixed case)
        if (token_lowercase == "index")
            has_index = true;
        // add the token to the list of column names
        column_names.emplace_back(token);
    }
    // check if any columns were found
    if (column_names.empty()) {
        // log information
        spdlog::warn("PRN header has no columns: {}", filename.string());
        // exit
        return {};
    }
    // log header information
    spdlog::debug(">> {}", line);
    spdlog::debug(">> ...");
    // with an index column the abscissa is the second column, otherwise it is the first column
    const size_t abscissa_column = has_index ? 1 : 0;
    // check if the abscissa column exists
    if (abscissa_column >= column_names.size()) {
        // log information
        spdlog::warn("PRN header has no abscissa column: {}", filename.string());
        // exit
        return {};
    }
    // abscissa information & data
    const std::string& abscissa_name = column_names[abscissa_column];
    const std::string abscissa_unit = print_table_unit_for_type(print_table_detect_variable_type(abscissa_name));
    std::vector<double> abscissa_values;
    // column descriptors: the complex pairing is decided from the header before any data row is read
    std::vector<PrintTableColumn> columns = print_table_build_columns(column_names, abscissa_column);
    // per-step bookkeeping: slices are shared by all expressions and value ranges feed the step information
    std::vector<std::pair<size_t, size_t>> step_slices;
    std::vector<std::pair<double, double>> value_ranges;
    std::vector<std::string> step_keys;
    size_t step_row_offset = 0;
    size_t step_row_count = 0;
    double step_min = std::numeric_limits<double>::max();
    double step_max = -std::numeric_limits<double>::max();
    bool boundary_seen = false;
    // finalize the current step
    auto finalize_step = [&]() {
        // append step slice
        step_slices.emplace_back(step_row_offset, step_row_offset + step_row_count);
        // append value range for the step
        value_ranges.emplace_back(step_min, step_max);
        // append step key
        if (step_keys.empty())
            step_keys.emplace_back("step");
        else
            step_keys.emplace_back("step " + std::to_string(step_keys.size() + 1));
        // update the step row offset and reset the step bookkeeping
        step_row_offset += step_row_count;
        step_row_count = 0;
        // min and max for the next step
        step_min = std::numeric_limits<double>::max();
        step_max = -std::numeric_limits<double>::max();
    };
    // reused per-row values buffer
    std::vector<double> row_values;
    // process data lines one by one, appending values directly into the column vectors
    while (std::getline(file, line)) {
        // drop the carriage return left by Windows line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // check for footer (single dot on line for STD format)
        if (line == ".")
            break;
        // check for blank line, which marks a step boundary in the GNUPLOT and SPLOT formats
        if (line.empty()) {
            // check if there are any rows in the current step before finalizing
            if (step_row_count > 0) {
                boundary_seen = true;
                finalize_step();
            }
            continue;
        }
        // tokenize data line
        print_table_tokenize_whitespace(line, tokens);
        // the line must be fully numeric and match the header column count
        if (tokens.size() != column_names.size())
            continue;
        // resize the row values buffer to match the number of tokens
        row_values.resize(tokens.size());
        // flag to validate all values are numeric
        bool all_numeric = true;
        // process tokens
        for (size_t t = 0; t < tokens.size(); ++t) {
            // parse each token as a number
            if (!print_table_parse_number(tokens[t], row_values[t])) {
                // invalid numeric value, skip the entire row
                all_numeric = false;
                // exit loop
                break;
            }
        }
        // skip the row if any value is non-numeric
        if (!all_numeric)
            continue;
        // abscissa value
        const double abscissa_value = row_values[abscissa_column];
        // append it to vector
        abscissa_values.push_back(abscissa_value);
        // update the step min and max
        if (abscissa_value < step_min)
            step_min = abscissa_value;
        if (abscissa_value > step_max)
            step_max = abscissa_value;
        // process columns
        for (PrintTableColumn& column : columns) {
            // check column is complex
            if (column.is_complex_column) {
                // real and imaginary values
                column.complex_values.emplace_back(row_values[column.source_column], row_values[column.source_imag_column]);
                // next
                continue;
            }
            // real column value
            column.values.push_back(row_values[column.source_column]);
        }
        ++step_row_count;
    }
    // finalize the last step
    if (step_row_count > 0)
        finalize_step();
    // handle case where no data was found
    if (step_slices.empty()) {
        // log information
        spdlog::warn("PRN file contains no data: {}", filename.string());
        // exit
        return {};
    }
    // detect abscissa scale from the values
    AbscissaScale abscissa_scale = print_table_detect_abscissa_scale(abscissa_values);
    // determine the final format: the explicit token wins, otherwise the format is inferred from the file characteristics
    const PrnFormat format = explicit_format.value_or(boundary_seen ? PrnFormat::GNUPLOT : (has_index ? PrnFormat::STD : PrnFormat::NOINDEX));
    // file-level metadata: format and index column presence
    std::unordered_map<std::string, std::string> metadata;
    metadata["format"] = format_string(format);
    metadata["has_index"] = has_index ? "true" : "false";
    // determine plot type from the output file name
    PlotType plot_type = detect_plot_type(filename);
    // count the data variables for the completion log before the assembly moves them out
    const size_t variables_count = columns.size();
    // assemble the parsed table into the output file
    auto xyce_file = print_table_assemble_output(filename, filename.stem().string(), abscissa_name, abscissa_unit, abscissa_values, columns, step_slices, std::move(value_ranges), std::move(step_keys), abscissa_scale, plot_type, std::move(metadata));
    // log completion
    spdlog::info("Successfully parsed PRN file: {}, format: {}, variables: {}, steps: {}, elapsed time: {}ms", filename.string(), format_string(format), variables_count, xyce_file->step_information().length(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count());
    // use file
    return xyce_file;
}

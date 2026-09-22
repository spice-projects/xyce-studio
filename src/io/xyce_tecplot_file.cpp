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

#include "../core/util.h"
#include "xyce_output_file.h"
#include "xyce_print_table.h"
#include "xyce_tecplot_file.h"

namespace
{
    // detect the plot type from the output file name: Xyce appends analysis-specific suffixes to the netlist name (reference guide, print analysis tables 2-19 to 2-29) and those suffixes are the only analysis-type marker a tecplot file carries; TRAN and DC share the plain .dat suffix, so for files without a well-known suffix the analysis markers in the netlist-derived name are used as a fallback
    PlotType detect_plot_type(const std::filesystem::path& filename) {
        // lowercase file name for the suffix comparisons
        const std::string name = to_lower(filename.filename().string());
        // frequency-domain output: AC, HB frequency data and AC sensitivity
        if (name.find(".fd.dat") != std::string::npos || name.find(".fd.sens.dat") != std::string::npos)
            return PlotType::AC;
        // time-domain output: AC_IC, HB time data, HB startup and initial conditions, homotopy, transient adjoint sensitivity and time-domain sensitivity
        if (name.find(".td.dat") != std::string::npos || name.find(".startup.dat") != std::string::npos || name.find(".hb_ic.dat") != std::string::npos || name.find(".homotopy.dat") != std::string::npos || name.find(".tradj.dat") != std::string::npos || name.find(".sens.dat") != std::string::npos)
            return PlotType::TRANSIENT;
        // noise analysis output
        if (name.find(".noise.dat") != std::string::npos)
            return PlotType::NOISE;
        // TRAN and DC share the plain .dat suffix and cannot be told apart from the file name alone: fall back to the analysis markers in the netlist-derived name
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

    // extract the text between the first and last double quote on the line and trim the padding spaces: Xyce writes tecplot strings as " value " with spaces inside the quotes
    std::string quoted_span(const std::string& line) {
        // locate the outer quotes
        const auto first = line.find('"');
        const auto last = line.rfind('"');
        // the line must hold a closed quoted span
        if (first == std::string::npos || last == first)
            return "";
        // take the span between the quotes
        const std::string_view span(line.data() + first + 1, last - first - 1);
        // strip the padding whitespace
        return trim(span);
    }

} // namespace

std::optional<std::shared_ptr<XyceOutputFile>> xyce_tecplot_file_parser(const std::filesystem::path& filename) {
    // check if file exists
    if (!std::filesystem::exists(filename))
        return {};
    // track timing
    auto start_time = std::chrono::steady_clock::now();
    // log information
    spdlog::info("Parsing TECPLOT file: {}", filename.string());
    // open the file
    std::ifstream file(filename);
    if (!file.is_open())
        return {};
    // reused line and token buffers: lines are streamed one by one and no whole-file copy is kept in memory
    std::string line;
    std::vector<std::string_view> tokens;
    // find the TITLE line (first non-blank line carrying the TITLE marker)
    bool any_line = false;
    bool title_found = false;
    // process lines to find the title
    while (std::getline(file, line)) {
        // drop the carriage return left by Windows line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // at least a line is present in the file
        any_line = true;
        // skip blank lines
        if (line.empty())
            continue;
        // the TITLE marker must be present on the line
        if (line.find("TITLE") == std::string::npos)
            continue;
        // the first line carrying the TITLE marker is the title line
        title_found = true;
        // exit the loop after finding the title
        break;
    }
    // check we found the title line
    if (!title_found) {
        // log a warning if the title line is missing
        if (any_line)
            spdlog::warn("TECPLOT file has no title: {}", filename.string());
        else
            spdlog::warn("TECPLOT file is empty: {}", filename.string());
        // exit
        return {};
    }
    // capture the circuit title text from the TITLE line
    std::string title = quoted_span(line);
    // column names: every variable of the header block is declared on a quoted line of its own, with the standard layout carrying the first column on the VARIABLES line and the intrusive PCE companion files leaving that marker bare
    std::vector<std::string> column_names;
    // process the following lines: every quoted line declares one more variable name, and the block ends at the first line that does not start with a quote (DATASETAUXDATA or ZONE)
    while (std::getline(file, line)) {
        // drop the carriage return left by Windows line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // skip blank lines
        if (line.empty())
            continue;
        // locate the VARIABLES marker: the standard Xyce layout declares the first column on the marker line, while the intrusive PCE companion files leave the marker bare
        const auto marker_pos = line.find("VARIABLES");
        // a line carrying the VARIABLES marker declares the first variable on the same line
        if (marker_pos != std::string::npos) {
            // extract the first variable when the marker carries a quoted span
            const std::string first_column = quoted_span(line.substr(marker_pos));
            // append the first column name when the marker carries a quoted span
            if (!first_column.empty())
                column_names.push_back(first_column);
            // next line
            continue;
        }
        // find the first non-blank character of the line
        const auto first_non_blank = line.find_first_not_of(" \t");
        // the variable block ends at the first line that does not start with a quote
        if (first_non_blank == std::string::npos || line[first_non_blank] != '"')
            break;
        // extract the variable name from the quoted span
        column_names.push_back(quoted_span(line));
    }
    // check if any columns were found
    if (column_names.empty()) {
        // log information
        spdlog::warn("TECPLOT file has no variables: {}", filename.string());
        // exit
        return {};
    }
    // log header information
    spdlog::debug(">> {}", title);
    spdlog::debug(">> ...");
    // Xyce writes no index column in tecplot format, so the abscissa is always the first column
    const size_t abscissa_column = 0;
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
    // consume one data line: the line must be fully numeric and match the header column count
    auto process_line = [&](const std::string& data_line) {
        // tokenize data line
        print_table_tokenize_whitespace(data_line, tokens);
        // the line must be fully numeric and match the header column count
        if (tokens.size() != column_names.size())
            return;
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
            return;
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
    };
    // process the remaining lines one by one: ZONE lines close the current step and start the next one, DATASETAUXDATA and AUXDATA lines are skipped and numeric lines carry data
    while (std::getline(file, line)) {
        // drop the carriage return left by Windows line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // blank lines carry no information in tecplot format: ignore them
        if (line.empty())
            continue;
        // a ZONE marker starts a new data block: the previous step is complete
        if (line.find("ZONE") != std::string::npos) {
            // check if there are any rows in the current step before finalizing
            if (step_row_count > 0)
                finalize_step();
            // next line
            continue;
        }
        // the footer marks the end of the simulation output
        if (line.find("End of Xyce") != std::string::npos)
            break;
        // consume the line as a candidate data row
        process_line(line);
    }
    // finalize the last step
    if (step_row_count > 0)
        finalize_step();
    // handle case where no data was found
    if (step_slices.empty()) {
        // log information
        spdlog::warn("TECPLOT file contains no data: {}", filename.string());
        // exit
        return {};
    }
    // detect abscissa scale from the values
    AbscissaScale abscissa_scale = print_table_detect_abscissa_scale(abscissa_values);
    // build file-level metadata: TECPLOT is the fixed format of the file
    std::unordered_map<std::string, std::string> metadata;
    metadata["format"] = "TECPLOT";
    // determine plot type from the output file name
    PlotType plot_type = detect_plot_type(filename);
    // count the data variables for the completion log before the assembly moves them out
    const size_t variables_count = columns.size();
    // assemble the parsed table into the output file
    auto xyce_file = print_table_assemble_output(filename, std::move(title), abscissa_name, abscissa_unit, abscissa_values, columns, step_slices, std::move(value_ranges), std::move(step_keys), abscissa_scale, plot_type, std::move(metadata));
    // log completion
    spdlog::info("Successfully parsed TECPLOT file: {}, variables: {}, steps: {}, elapsed time: {}ms", filename.string(), variables_count, xyce_file->step_information().length(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count());
    // use file
    return xyce_file;
}

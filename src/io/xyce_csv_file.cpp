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
#include "xyce_csv_file.h"
#include "xyce_output_file.h"
#include "xyce_print_table.h"

namespace
{
    // candidate field separators probed to autodetect the file delimiter: Xyce
    // writes ',' for FORMAT=CSV but the DELIMITER= option can replace it with
    // TAB, COLON or SEMICOLON, and the format still produces .csv files
    constexpr char DELIMITER_CANDIDATES[] = {',', ';', ':', '\t'};

    // detect the plot type from the output file name: Xyce appends analysis-specific suffixes to the netlist name (reference guide, print analysis tables 2-19 to 2-29) and those suffixes are the only analysis-type marker a csv file carries; TRAN and DC share the plain .csv suffix, so for files without a well-known suffix the analysis markers in the netlist-derived name are used as a fallback
    PlotType detect_plot_type(const std::filesystem::path& filename) {
        // lowercase file name for the suffix comparisons
        const std::string name = to_lower(filename.filename().string());
        // frequency-domain output: AC, HB frequency data and AC sensitivity
        if (name.find(".fd.csv") != std::string::npos || name.find(".fd.sens.csv") != std::string::npos)
            return PlotType::AC;
        // time-domain output: AC_IC, HB time data, HB startup and initial conditions, homotopy, transient adjoint sensitivity and time-domain sensitivity
        if (name.find(".td.csv") != std::string::npos || name.find(".startup.csv") != std::string::npos || name.find(".hb_ic.csv") != std::string::npos || name.find(".homotopy.csv") != std::string::npos || name.find(".tradj.csv") != std::string::npos || name.find(".sens.csv") != std::string::npos)
            return PlotType::TRANSIENT;
        // noise analysis output
        if (name.find(".noise.csv") != std::string::npos)
            return PlotType::NOISE;
        // TRAN and DC share the plain .csv suffix and cannot be told apart from the file name alone: fall back to the analysis markers in the netlist-derived name
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

std::optional<std::shared_ptr<XyceOutputFile>> xyce_csv_file_parser(const std::filesystem::path& filename) {
    // check if file exists
    if (!std::filesystem::exists(filename))
        return {};
    // track timing
    auto start_time = std::chrono::steady_clock::now();
    // log information
    spdlog::info("Parsing CSV file: {}", filename.string());
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
            spdlog::warn("CSV file has no header: {}", filename.string());
        else
            spdlog::warn("CSV file is empty: {}", filename.string());
        // exit
        return {};
    }
    // keep a copy of the header line: the column names are extracted after the delimiter is detected
    const std::string header_line = line;
    // find the first non-blank data line: the delimiter is detected from the header/first-row agreement
    bool first_data_found = false;
    std::string first_data_line;
    while (std::getline(file, line)) {
        // drop the carriage return left by Windows line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // skip blank lines
        if (line.empty())
            continue;
        // the first non-blank line after the header is the first data row
        first_data_line = line;
        // mark found
        first_data_found = true;
        // exit the loop
        break;
    }
    // autodetect the delimiter: a candidate matches when it splits the header and the first data row into the same number of fields; candidates with at least two fields win over degenerate single-field splits, and the comma is the fallback default
    std::optional<char> detected;
    std::optional<char> weak_match;
    // scan the candidate delimiters
    for (char candidate : DELIMITER_CANDIDATES) {
        // count the header fields for the candidate
        print_table_tokenize_delimited(header_line, candidate, tokens);
        const size_t header_fields = tokens.size();
        // count the first row fields for the candidate
        print_table_tokenize_delimited(first_data_line, candidate, tokens);
        const size_t row_fields = tokens.size();
        // the candidate must agree between the header and the first data row
        if (header_fields != row_fields)
            continue;
        // a multi-field agreement is a strong match: stop here
        if (header_fields >= 2) {
            detected = candidate;
            break;
        }
        // remember the first degenerate match as a weak fallback
        if (!weak_match.has_value())
            weak_match = candidate;
    }
    // the detected delimiter: the strong match, else the weak match, else the comma default
    const char delimiter = first_data_found ? detected.value_or(weak_match.value_or(',')) : ',';
    // split the header into column names with the selected delimiter
    print_table_tokenize_delimited(header_line, delimiter, tokens);
    // store column names from the header line
    std::vector<std::string> column_names;
    column_names.reserve(tokens.size());
    // copy the token views into owning strings
    for (std::string_view token : tokens)
        column_names.emplace_back(token);
    // log header information
    spdlog::debug(">> {}", header_line);
    spdlog::debug(">> ...");
    // Xyce writes no index column in CSV format, so the abscissa is always the first column
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
    // the abscissa value of the last accepted row: a decrease means a .STEP run restarted the sweep
    double last_abscissa = std::numeric_limits<double>::lowest();
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
    // consume one data line: the file carries no step separators, so an abscissa restart closes the current step
    auto process_line = [&](const std::string& data_line) {
        // tokenize data line
        print_table_tokenize_delimited(data_line, delimiter, tokens);
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
        // an abscissa decrease means the sweep restarted in a new .STEP iteration
        if (step_row_count > 0 && abscissa_value < last_abscissa)
            finalize_step();
        // remember the value for the next restart comparison
        last_abscissa = abscissa_value;
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
    // process the buffered first data line, when the file carries any data at all
    if (first_data_found)
        process_line(first_data_line);
    // process the remaining data lines one by one, appending values directly into the column vectors
    while (std::getline(file, line)) {
        // drop the carriage return left by Windows line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // blank lines are not step markers in CSV format: ignore them
        if (line.empty())
            continue;
        // consume the row
        process_line(line);
    }
    // finalize the last step
    if (step_row_count > 0)
        finalize_step();
    // handle case where no data was found
    if (step_slices.empty()) {
        // log information
        spdlog::warn("CSV file contains no data: {}", filename.string());
        // exit
        return {};
    }
    // detect abscissa scale from the values
    AbscissaScale abscissa_scale = print_table_detect_abscissa_scale(abscissa_values);
    // build file-level metadata: CSV is the fixed format and the delimiter is the autodetected one
    std::unordered_map<std::string, std::string> metadata;
    metadata["format"] = "CSV";
    metadata["delimiter"] = std::string(1, delimiter);
    // determine plot type from the output file name
    PlotType plot_type = detect_plot_type(filename);
    // count the data variables for the completion log before the assembly moves them out
    const size_t variables_count = columns.size();
    // assemble the parsed table into the output file
    auto xyce_file = print_table_assemble_output(filename, filename.stem().string(), abscissa_name, abscissa_unit, abscissa_values, columns, step_slices, std::move(value_ranges), std::move(step_keys), abscissa_scale, plot_type, std::move(metadata));
    // log completion
    spdlog::info("Successfully parsed CSV file: {}, variables: {}, steps: {}, elapsed time: {}ms", filename.string(), variables_count, xyce_file->step_information().length(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count());
    // use file
    return xyce_file;
}

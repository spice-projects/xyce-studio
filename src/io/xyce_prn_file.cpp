#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
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

#include "../core/step_information.h"
#include "../core/util.h"
#include "../expression/expression.h"
#include "../expression/expression_manager.h"
#include "xyce_output_file.h"
#include "xyce_prn_file.h"
#include "xyce_raw_file.h"

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

    // detect variable type from column name
    VariableType detect_variable_type(std::string_view name) {
        // check for time variable
        if (name == "time" || name == "TIME") {
            return VariableType::TIME;
        }
        // check for frequency variable
        if (name == "frequency" || name == "FREQ" || name == "FREQUENCY") {
            return VariableType::FREQUENCY;
        }
        // check for voltage variable
        if (name.rfind("v(", 0) == 0 || name.rfind("V(", 0) == 0) {
            return VariableType::VOLTAGE;
        }
        // check for current variable
        if (name.rfind("i(", 0) == 0 || name.rfind("I(", 0) == 0) {
            return VariableType::CURRENT;
        }
        // check for power variable
        if (name.rfind("p(", 0) == 0 || name.rfind("P(", 0) == 0 || name.rfind("w(", 0) == 0 || name.rfind("W(", 0) == 0) {
            return VariableType::POWER;
        }
        // check for phase variable
        if (name.rfind("ph(", 0) == 0 || name.rfind("PH(", 0) == 0 || name.rfind("phase", 0) == 0 || name.rfind("PHASE", 0) == 0) {
            return VariableType::PHASE;
        }
        // check for parameter variable
        if (name == "sweep" || name == "parameter" || name == "PARAMETER") {
            return VariableType::PARAMETER;
        }
        // check for expression variable (enclosed in braces)
        if (!name.empty() && name.front() == '{' && name.back() == '}') {
            return VariableType::EXPRESSION;
        }
        // default to unknown
        return VariableType::UNKNOWN;
    }

    // split a line into whitespace-separated tokens, reusing the token buffer
    void tokenize_line(const std::string& line, std::vector<std::string_view>& tokens) {
        // reset the buffer
        tokens.clear();
        // scan the line
        size_t pos = 0;
        while (pos < line.size()) {
            // skip whitespace
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
                ++pos;
            // check for end of line
            if (pos >= line.size())
                break;
            // find the token end
            size_t start = pos;
            while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos])))
                ++pos;
            // store the token as a view into the line
            tokens.emplace_back(line.data() + start, pos - start);
        }
    }

    // parse a numeric token, returning false for non-numeric tokens
    bool parse_number(std::string_view token, double& out) {
        // reject empty tokens
        if (token.empty())
            return false;
        // parse the number: the token holds no whitespace, so strtod cannot run past it into the next token
        char* end = nullptr;
        errno = 0;
        // attempt to parse the number
        double value = std::strtod(token.data(), &end);
        // the token must be fully consumed without range errors
        if (end != token.data() + token.size() || errno == ERANGE)
            return false;
        // update the output value
        out = value;
        // indicate successful parsing
        return true;
    }

    // detect a complex column part from a column name: Re(X) returns the base
    // name X with is_real true, Im(X) returns the base name X with is_real
    // false, and any other name returns nullopt
    std::optional<std::pair<std::string_view, bool>> complex_column_part(std::string_view name) {
        // need at least the wrapping parentheses
        if (name.size() < 4 || name.back() != ')')
            return {};
        // check for a real part column
        if (name.rfind("Re(", 0) == 0 || name.rfind("re(", 0) == 0)
            return std::make_pair(name.substr(3, name.size() - 4), true);
        // check for an imaginary part column
        if (name.rfind("Im(", 0) == 0 || name.rfind("im(", 0) == 0)
            return std::make_pair(name.substr(3, name.size() - 4), false);
        // not a complex part column
        return {};
    }

    // detect abscissa scale from a sequence of values
    AbscissaScale detect_abscissa_scale(const std::vector<double>& values) {
        // need at least three points to detect non-linear scale
        if (values.size() < 3) {
            return AbscissaScale::LINEAR;
        }
        // check if values are positive and strictly increasing
        for (size_t i = 1; i < values.size(); ++i) {
            if (values[i - 1] <= 0.0 || values[i] <= values[i - 1]) {
                return AbscissaScale::LINEAR;
            }
        }
        // compute log10 ratio between consecutive values
        double reference_ratio = std::log10(values[1] / values[0]);
        for (size_t i = 2; i < values.size(); ++i) {
            double ratio = std::log10(values[i] / values[i - 1]);
            // if ratio is not uniform, return LINEAR
            if (std::abs(ratio - reference_ratio) > 1e-7) {
                return AbscissaScale::LINEAR;
            }
        }
        // check for decade spacing
        long decade_steps = std::lround(1.0 / reference_ratio);
        // check for octave spacing
        long octave_steps = std::lround(std::log10(2.0) / reference_ratio);
        // compute residuals
        double decade_residual = std::numeric_limits<double>::max();
        if (decade_steps >= 2) {
            decade_residual = std::abs(reference_ratio - 1.0 / static_cast<double>(decade_steps));
        }
        double octave_residual = std::numeric_limits<double>::max();
        if (octave_steps >= 2) {
            octave_residual = std::abs(reference_ratio - std::log10(2.0) / static_cast<double>(octave_steps));
        }
        // return the winning candidate
        return decade_residual <= octave_residual ? AbscissaScale::DECADE : AbscissaScale::OCTAVE;
    }

    // return a format string from the PrnFormat enum
    std::string format_string(PrnFormat format) {
        switch (format) {
        case PrnFormat::STD:
            return "STD";
        case PrnFormat::NOINDEX:
            return "NOINDEX";
        case PrnFormat::GNUPLOT:
            return "GNUPLOT";
        case PrnFormat::SPLOT:
            return "SPLOT";
        default:
            return "STD";
        }
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

    // determine the unit string for a variable type
    std::string unit_for_type(VariableType vt) {
        switch (vt) {
        case VariableType::TIME:
            return "s";
        case VariableType::FREQUENCY:
            return "Hz";
        case VariableType::VOLTAGE:
            return "V";
        case VariableType::CURRENT:
            return "A";
        case VariableType::POWER:
            return "W";
        case VariableType::PHASE:
            return "\u00b0";
        default:
            return "";
        }
    }

    // in-progress column data: rows append directly into the vectors that
    // back the final Expression objects, no temporary copies are kept
    struct PrnColumn
    {
        // variable name
        std::string name;
        // measurement unit
        std::string unit;
        // column index holding the real part or the value
        size_t source_column = 0;
        // column index holding the imaginary part (complex columns only)
        size_t source_imag_column = 0;
        // whether the column holds complex values
        bool is_complex_column = false;
        // real values
        std::vector<double> values;
        // complex values
        std::vector<std::complex<double>> complex_values;
    };

    // build the column descriptors from the header column names: a Re(X)/Im(X) pair becomes a single complex variable X regardless of the order in which the parts appear, the remaining columns become plain double variables, and the abscissa column is excluded from the descriptors
    std::vector<PrnColumn> build_columns(const std::vector<std::string>& column_names, size_t abscissa_column) {
        // track columns consumed by a complex pair
        std::vector<bool> column_consumed(column_names.size(), false);
        // the abscissa column is handled separately
        column_consumed[abscissa_column] = true;
        // return value
        std::vector<PrnColumn> columns;
        // loop columns to build the column descriptors
        for (size_t col_idx = 0; col_idx < column_names.size(); ++col_idx) {
            // skip the abscissa column and columns consumed by a complex pair
            if (column_consumed[col_idx])
                continue;
            // get the column name for the current index
            const std::string& column_name = column_names[col_idx];
            // check whether the column holds one part of a complex variable
            auto part = complex_column_part(column_name);
            if (part.has_value()) {
                // counter part column index
                size_t counterpart_column = col_idx;
                // search the counterpart column: the other part of the same complex variable, wherever it appears
                for (size_t other_idx = 0; other_idx < column_names.size(); ++other_idx) {
                    // skip the abscissa column, the current column and consumed columns
                    if (other_idx == abscissa_column || other_idx == col_idx || column_consumed[other_idx])
                        continue;
                    // get the other column part
                    auto other_part = complex_column_part(column_names[other_idx]);
                    // the counterpart holds the opposite part of the same complex variable
                    if (!other_part.has_value() || other_part->second == part->second || other_part->first != part->first)
                        continue;
                    // counterpart found
                    counterpart_column = other_idx;
                    // exit loop
                    break;
                }
                // check if a complex pair was found
                if (counterpart_column != col_idx) {
                    // create the complex column: the real part column feeds the real component and the imaginary part column feeds the imaginary component, regardless of the order in which they appear
                    PrnColumn column;
                    column.name = part->first;
                    column.unit = unit_for_type(detect_variable_type(part->first));
                    column.source_column = part->second ? col_idx : counterpart_column;
                    column.source_imag_column = part->second ? counterpart_column : col_idx;
                    column.is_complex_column = true;
                    // append the complex column to the columns vector
                    columns.push_back(std::move(column));
                    // consume both columns
                    column_consumed[col_idx] = true;
                    column_consumed[counterpart_column] = true;
                    // next column
                    continue;
                }
            }
            // create a plain double column
            PrnColumn column;
            column.name = column_name;
            column.unit = unit_for_type(detect_variable_type(column_name));
            column.source_column = col_idx;
            // append the plain double column to the columns vector
            columns.push_back(std::move(column));
            // consume column
            column_consumed[col_idx] = true;
        }
        // return the column descriptors
        return columns;
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
    tokenize_line(line, tokens);
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
    const std::string abscissa_unit = unit_for_type(detect_variable_type(abscissa_name));
    std::vector<double> abscissa_values;
    // column descriptors: the complex pairing is decided from the header before any data row is read
    std::vector<PrnColumn> columns = build_columns(column_names, abscissa_column);
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
        tokenize_line(line, tokens);
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
            if (!parse_number(tokens[t], row_values[t])) {
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
        for (PrnColumn& column : columns) {
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
    AbscissaScale abscissa_scale = detect_abscissa_scale(abscissa_values);
    // determine the final format: the explicit token wins, otherwise the format is inferred from the file characteristics
    const PrnFormat format = explicit_format.value_or(boundary_seen ? PrnFormat::GNUPLOT : (has_index ? PrnFormat::STD : PrnFormat::NOINDEX));
    // a frequency-domain file carries Re(X)/Im(X) column pairs: when at least one pair exists the file holds complex data and every data expression is a complex number, the abscissa stays real
    bool is_complex = false;
    // process columns
    for (const PrnColumn& column : columns) {
        // check if column is complex
        if (column.is_complex_column) {
            // if a single complex column is found, the file is considered complex
            is_complex = true;
            // exit loop
            break;
        }
    }
    // file expressions
    std::vector<AnyExpression> expressions;
    // reserve space for expressions
    expressions.reserve(columns.size() + 1);
    // append the abscissa as the first expression: it is always a real number
    expressions.emplace_back(Expression<double>(abscissa_name, std::move(abscissa_values), step_slices, abscissa_unit));
    // process columns
    for (PrnColumn& column : columns) {
        // check if column is complex or already paired
        if (column.is_complex_column) {
            // create complex expression
            expressions.emplace_back(Expression<std::complex<double>>(column.name, std::move(column.complex_values), step_slices, column.unit));
            // next column
            continue;
        }
        // check if data is complex: plain columns of a complex file hold purely real values carried in the real component
        if (is_complex) {
            // build the complex values from the real column values
            std::vector<std::complex<double>> complex_values;
            // reserve space
            complex_values.reserve(column.values.size());
            // promote each value
            for (double value : column.values)
                complex_values.emplace_back(value, 0.0);
            // create complex expression
            expressions.emplace_back(Expression<std::complex<double>>(column.name, std::move(complex_values), step_slices, column.unit));
            // next column
            continue;
        }
        // create expression for real column
        expressions.emplace_back(Expression<double>(column.name, std::move(column.values), step_slices, column.unit));
    }
    // create expression manager (constructor takes lvalue references)
    ExpressionManager expression_manager(expressions, step_slices);
    // build step information
    std::vector<std::vector<double>> step_values;
    step_values.reserve(step_keys.size());
    // append step values
    for (size_t step = 0; step < step_keys.size(); ++step)
        step_values.push_back({static_cast<double>(step + 1)});
    // create step information instance
    StepInformation step_info(std::move(step_keys), std::move(step_values), std::move(value_ranges));
    // determine plot type from the output file name
    PlotType plot_type = detect_plot_type(filename);
    // capture values for logging before move operations
    size_t variables_count = expressions.size() - 1;
    size_t steps_count = step_info.length();
    // build file-level metadata
    std::unordered_map<std::string, std::string> metadata;
    metadata["format"] = format_string(format);
    metadata["has_index"] = has_index ? "true" : "false";
    // create the output file with no suggested plots (user chooses what to plot)
    auto xyce_file = std::make_shared<XyceOutputFile>(filename, filename.stem().string(), is_complex, std::move(step_info), plot_type, abscissa_scale, std::move(expression_manager), nullptr, std::vector<std::vector<std::string>>{}, std::move(metadata));
    // log completion
    spdlog::info("Successfully parsed PRN file: {}, format: {}, variables: {}, steps: {}, elapsed time: {}ms", filename.string(), format_string(format), variables_count, steps_count, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count());
    // use file
    return xyce_file;
}

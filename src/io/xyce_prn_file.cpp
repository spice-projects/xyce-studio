#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "../core/step_information.h"
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
    VariableType detect_variable_type(const std::string& name) {
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

    // parse a whitespace-separated line into tokens
    std::vector<std::string> tokenize_line(const std::string& line) {
        std::vector<std::string> tokens;
        std::string token;
        for (char c : line) {
            // check if character is whitespace
            if (std::isspace(static_cast<unsigned char>(c))) {
                // flush the accumulated token
                if (!token.empty()) {
                    tokens.push_back(std::move(token));
                    token.clear();
                }
            }
            else {
                token += c;
            }
        }
        // flush the final token
        if (!token.empty()) {
            tokens.push_back(std::move(token));
        }
        return tokens;
    }

    // determine if a string represents a numeric value
    bool is_number(const std::string& str) {
        if (str.empty()) {
            return false;
        }
        size_t idx = 0;
        try {
            std::stod(str, &idx);
            return idx == str.size();
        }
        catch (...) {
            return false;
        }
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
} // namespace

std::optional<std::shared_ptr<XyceOutputFile>> xyce_prn_file_parser(const std::filesystem::path& filename) {
    // check if file exists
    if (!std::filesystem::exists(filename)) {
        return {};
    }
    // log start of parsing
    auto start_time = std::chrono::steady_clock::now();
    spdlog::info("Parsing PRN file: {}", filename.string());
    // open the file
    std::ifstream file(filename);
    if (!file.is_open()) {
        return {};
    }
    // read all lines
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();
    if (lines.empty()) {
        spdlog::warn("PRN file is empty: {}", filename.string());
        return {};
    }
    // find the header line (first non-empty line)
    size_t header_line_index = 0;
    while (header_line_index < lines.size() && lines[header_line_index].empty()) {
        ++header_line_index;
    }
    if (header_line_index >= lines.size()) {
        spdlog::warn("PRN file has no header: {}", filename.string());
        return {};
    }
    // tokenize header line
    std::vector<std::string> header_tokens = tokenize_line(lines[header_line_index]);
    if (header_tokens.empty()) {
        spdlog::warn("PRN header line is empty: {}", filename.string());
        return {};
    }
    // determine if file has INDEX column
    bool has_index = false;
    for (size_t i = 0; i < header_tokens.size(); ++i) {
        const std::string& token = header_tokens[i];
        if (token == "INDEX" || token == "index") {
            has_index = true;
            break;
        }
    }
    // store column names from header, excluding FORMAT= tokens
    std::vector<std::string> column_names;
    for (const auto& token : header_tokens) {
        // skip FORMAT= tokens, they are not column names
        if (token.rfind("FORMAT=", 0) == 0) {
            continue;
        }
        column_names.push_back(token);
    }
    if (column_names.empty()) {
        spdlog::warn("PRN header has no columns: {}", filename.string());
        return {};
    }
    // detect if this is GNUPLOT or SPLOT format (blank lines between steps)
    bool is_gnuplot_format = false;
    bool has_blank_lines = false;
    for (size_t i = header_line_index + 1; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            has_blank_lines = true;
            break;
        }
    }
    // detect format from header: look for FORMAT=<format> token
    PrnFormat format = PrnFormat::STD;
    for (const std::string& token : header_tokens) {
        if (token.rfind("FORMAT=", 0) == 0) {
            std::string format_str = token.substr(7);
            std::transform(format_str.begin(), format_str.end(), format_str.begin(), ::toupper);
            if (format_str == "NOINDEX") {
                format = PrnFormat::NOINDEX;
            }
            else if (format_str == "GNUPLOT") {
                format = PrnFormat::GNUPLOT;
                is_gnuplot_format = true;
            }
            else if (format_str == "SPLOT") {
                format = PrnFormat::SPLOT;
                is_gnuplot_format = true;
            }
            else if (format_str == "STD") {
                format = PrnFormat::STD;
            }
            break;
        }
    }
    // if no explicit format, infer from characteristics
    if (format == PrnFormat::STD && !has_index && !has_blank_lines) {
        format = PrnFormat::NOINDEX;
    }
    else if (format == PrnFormat::STD && has_blank_lines) {
        format = PrnFormat::GNUPLOT;
        is_gnuplot_format = true;
    }
    // parse data lines into step blocks
    // step_data is [step][row][column]
    std::vector<std::vector<std::vector<double>>> step_data;
    // value_ranges is [step] -> (min, max) for abscissa
    std::vector<std::pair<double, double>> value_ranges;
    // step_keys is [step] -> step identifier string
    std::vector<std::string> step_keys;
    size_t current_step = 0;
    step_data.push_back({});
    value_ranges.push_back({std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()});
    step_keys.push_back("step");
    // process lines after the header
    for (size_t i = header_line_index + 1; i < lines.size(); ++i) {
        const std::string& data_line = lines[i];
        // check for footer (single dot on line for STD format)
        if (format == PrnFormat::STD && data_line == ".") {
            break;
        }
        // check for blank line, which indicates step boundary in GNUPLOT/SPLOT
        if (is_gnuplot_format && data_line.empty() && !step_data[current_step].empty()) {
            // start new step
            ++current_step;
            step_data.push_back({});
            value_ranges.push_back({std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()});
            step_keys.push_back("step " + std::to_string(current_step + 1));
            continue;
        }
        // skip empty lines
        if (data_line.empty()) {
            continue;
        }
        // tokenize data line
        std::vector<std::string> data_tokens = tokenize_line(data_line);
        if (data_tokens.empty()) {
            continue;
        }
        // convert tokens to numeric values, skip lines with non-numeric tokens
        std::vector<double> numeric_values;
        bool all_numeric = true;
        for (const std::string& token : data_tokens) {
            if (is_number(token)) {
                numeric_values.push_back(std::stod(token));
            }
            else {
                all_numeric = false;
                break;
            }
        }
        if (!all_numeric || numeric_values.size() != column_names.size()) {
            continue;
        }
        // add data to current step
        step_data[current_step].push_back(numeric_values);
        // update value range for abscissa (first column, or first non-index column)
        size_t abscissa_column = has_index ? 1 : 0;
        if (abscissa_column < numeric_values.size()) {
            double value = numeric_values[abscissa_column];
            if (value < value_ranges[current_step].first) {
                value_ranges[current_step].first = value;
            }
            if (value > value_ranges[current_step].second) {
                value_ranges[current_step].second = value;
            }
        }
    }
    // handle case where no data was found
    if (step_data.empty() || step_data[0].empty()) {
        spdlog::warn("PRN file contains no data: {}", filename.string());
        return {};
    }
    // fill remaining step keys and value ranges if needed
    while (step_data.size() > step_keys.size()) {
        step_keys.push_back("step " + std::to_string(step_keys.size() + 1));
        value_ranges.push_back({std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()});
    }
    // build abscissa data from the first non-index column
    std::vector<double> abscissa_values;
    std::vector<std::pair<size_t, size_t>> abscissa_step_slices;
    size_t abscissa_offset = 0;
    for (size_t step = 0; step < step_data.size(); ++step) {
        const auto& step_rows = step_data[step];
        for (const auto& row : step_rows) {
            size_t col_idx = has_index ? 1 : 0;
            if (col_idx < row.size()) {
                abscissa_values.push_back(row[col_idx]);
            }
        }
        abscissa_step_slices.emplace_back(abscissa_offset, abscissa_offset + step_rows.size());
        abscissa_offset += step_rows.size();
    }
    // detect abscissa scale from the values
    AbscissaScale abscissa_scale = detect_abscissa_scale(abscissa_values);
    // build expressions: first abscissa, then each variable column
    std::vector<AnyExpression> expressions;
    expressions.reserve(column_names.size());
    // add abscissa expression first
    std::string abscissa_name = column_names[0];
    expressions.emplace_back(Expression<double>(abscissa_name, std::vector<double>(abscissa_values), abscissa_step_slices, ""));
    // add expressions for remaining columns
    for (size_t col_idx = 0; col_idx < column_names.size(); ++col_idx) {
        // skip the abscissa column
        size_t skip_col = has_index ? 1 : 0;
        if (col_idx == skip_col) {
            continue;
        }
        std::string column_name = column_names[col_idx];
        // collect data for this column across all steps and rows
        std::vector<double> column_values;
        std::vector<std::pair<size_t, size_t>> column_step_slices;
        size_t column_offset = 0;
        for (size_t step = 0; step < step_data.size(); ++step) {
            const auto& step_rows = step_data[step];
            for (const auto& row : step_rows) {
                if (col_idx < row.size()) {
                    column_values.push_back(row[col_idx]);
                }
            }
            column_step_slices.emplace_back(column_offset, column_offset + step_rows.size());
            column_offset += step_rows.size();
        }
        // determine variable unit from column name
        VariableType var_type = detect_variable_type(column_name);
        std::string unit = unit_for_type(var_type);
        expressions.emplace_back(Expression<double>(column_name, std::move(column_values), column_step_slices, unit));
    }
    // create expression manager (constructor takes lvalue references)
    ExpressionManager expression_manager(expressions, abscissa_step_slices);
    // build step information
    std::vector<std::vector<double>> step_values;
    std::vector<std::pair<double, double>> step_abscissa_ranges;
    for (size_t step = 0; step < step_keys.size(); ++step) {
        step_values.push_back({static_cast<double>(step + 1)});
        step_abscissa_ranges.push_back(value_ranges[step]);
    }
    StepInformation step_info(std::move(step_keys), std::move(step_values), std::move(step_abscissa_ranges));
    // determine plot type from filename content
    PlotType plot_type = PlotType::UNKNOWN;
    std::string name_lower = filename.filename().string();
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
    if (name_lower.find("tran") != std::string::npos || name_lower.find("transient") != std::string::npos) {
        plot_type = PlotType::TRANSIENT;
    }
    else if (name_lower.find("ac") != std::string::npos) {
        plot_type = PlotType::AC;
    }
    else if (name_lower.find("dc") != std::string::npos) {
        plot_type = PlotType::DC;
    }
    else if (name_lower.find("noise") != std::string::npos) {
        plot_type = PlotType::NOISE;
    }
    // determine if data is complex
    bool is_complex = false;
    for (const std::string& name : column_names) {
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        if (lower_name.find("imag") != std::string::npos || lower_name.find("phase") != std::string::npos) {
            is_complex = true;
            break;
        }
    }
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

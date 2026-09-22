#pragma once

#include <cctype>
#include <cerrno>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../core/step_information.h"
#include "../expression/expression.h"
#include "../expression/expression_manager.h"
#include "xyce_output_file.h"
#include "xyce_raw_file.h"

// shared engine for Xyce print output tables (.prn and .csv). The two formats
// differ only in how lines are split and where step boundaries lie; the column
// model, complex Re()/Im() pairing, abscissa scale detection and expression
// assembly are common and live here. All functions are inline: the engine is
// header-only so both parsers share one definition without a separate object.

// in-progress column data: rows append directly into the vectors that back the
// final Expression objects, no temporary copies are kept
struct PrintTableColumn
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

// determine the unit string for a variable type
inline std::string print_table_unit_for_type(VariableType vt) {
    switch (vt) {
    // time case
    case VariableType::TIME:
        return "s";
    // frequency case
    case VariableType::FREQUENCY:
        return "Hz";
    // voltage case
    case VariableType::VOLTAGE:
        return "V";
    // current case
    case VariableType::CURRENT:
        return "A";
    // power case
    case VariableType::POWER:
        return "W";
    // phase case
    case VariableType::PHASE:
        return "\u00b0";
    // no unit for the remaining types
    default:
        return "";
    }
}

// detect variable type from column name
inline VariableType print_table_detect_variable_type(std::string_view name) {
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

// parse a numeric token, returning false for non-numeric tokens
inline bool print_table_parse_number(std::string_view token, double& out) {
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

// detect a complex column part from a column name: Re(X) returns the base name
// X with is_real true, Im(X) returns the base name X with is_real false, and
// any other name returns nullopt
inline std::optional<std::pair<std::string_view, bool>> print_table_complex_column_part(std::string_view name) {
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

// build the column descriptors from the header column names: a Re(X)/Im(X) pair
// becomes a single complex variable X regardless of the order in which the
// parts appear, the remaining columns become plain double variables, and the
// abscissa column is excluded from the descriptors
inline std::vector<PrintTableColumn> print_table_build_columns(const std::vector<std::string>& column_names, size_t abscissa_column) {
    // track columns consumed by a complex pair
    std::vector<bool> column_consumed(column_names.size(), false);
    // the abscissa column is handled separately
    column_consumed[abscissa_column] = true;
    // return value
    std::vector<PrintTableColumn> columns;
    // loop columns to build the column descriptors
    for (size_t col_idx = 0; col_idx < column_names.size(); ++col_idx) {
        // skip the abscissa column and columns consumed by a complex pair
        if (column_consumed[col_idx])
            continue;
        // get the column name for the current index
        const std::string& column_name = column_names[col_idx];
        // check whether the column holds one part of a complex variable
        auto part = print_table_complex_column_part(column_name);
        if (part.has_value()) {
            // counter part column index
            size_t counterpart_column = col_idx;
            // search the counterpart column: the other part of the same complex variable, wherever it appears
            for (size_t other_idx = 0; other_idx < column_names.size(); ++other_idx) {
                // skip the abscissa column, the current column and consumed columns
                if (other_idx == abscissa_column || other_idx == col_idx || column_consumed[other_idx])
                    continue;
                // get the other column part
                auto other_part = print_table_complex_column_part(column_names[other_idx]);
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
                PrintTableColumn column;
                column.name = part->first;
                column.unit = print_table_unit_for_type(print_table_detect_variable_type(part->first));
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
        PrintTableColumn column;
        column.name = column_name;
        column.unit = print_table_unit_for_type(print_table_detect_variable_type(column_name));
        column.source_column = col_idx;
        // append the plain double column to the columns vector
        columns.push_back(std::move(column));
        // consume column
        column_consumed[col_idx] = true;
    }
    // return the column descriptors
    return columns;
}

// detect abscissa scale from a sequence of values
inline AbscissaScale print_table_detect_abscissa_scale(const std::vector<double>& values) {
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

// split a line into whitespace-separated tokens, reusing the token buffer
inline void print_table_tokenize_whitespace(const std::string& line, std::vector<std::string_view>& tokens) {
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

// split a delimited line into fields on the given delimiter character: a
// delimiter is only a field separator when it lies outside parentheses and
// braces, so names such as V(n1,n2) stay a single column; empty fields are
// kept so column alignment is preserved
inline void print_table_tokenize_delimited(const std::string& line, char delimiter, std::vector<std::string_view>& tokens) {
    // reset the buffer
    tokens.clear();
    // nesting depth of parentheses and braces
    int depth = 0;
    // start index of the field being scanned
    size_t start = 0;
    // scan the line
    for (size_t pos = 0; pos < line.size(); ++pos) {
        // current character
        const char c = line[pos];
        // open a nesting group
        if (c == '(' || c == '{')
            ++depth;
        // close a nesting group
        else if (c == ')' || c == '}')
            --depth;
        // a delimiter only splits when the scan is not inside a group
        else if (c == delimiter && depth <= 0) {
            // store the field as a view into the line
            tokens.emplace_back(line.data() + start, pos - start);
            // the next field starts after the delimiter
            start = pos + 1;
        }
    }
    // store the final field
    tokens.emplace_back(line.data() + start, line.size() - start);
}

// assemble the parsed abscissa, columns and step bookkeeping into a
// XyceOutputFile: complex columns make the file complex and promote the plain
// columns to the real component of a complex value; the abscissa stays real
inline std::shared_ptr<XyceOutputFile> print_table_assemble_output(const std::filesystem::path& filename, std::string title, std::string abscissa_name, std::string abscissa_unit, std::vector<double>& abscissa_values, std::vector<PrintTableColumn>& columns, std::vector<std::pair<size_t, size_t>>& step_slices, std::vector<std::pair<double, double>> value_ranges, std::vector<std::string> step_keys, AbscissaScale abscissa_scale, PlotType plot_type, std::unordered_map<std::string, std::string> metadata) {
    // a frequency-domain file carries Re(X)/Im(X) column pairs: when at least one pair exists the file holds complex data and every data expression is a complex number, the abscissa stays real
    bool is_complex = false;
    // process columns
    for (const PrintTableColumn& column : columns) {
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
    for (PrintTableColumn& column : columns) {
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
    // capture the complex flag for the output file constructor
    // create the output file with no suggested plots (user chooses what to plot)
    return std::make_shared<XyceOutputFile>(filename, std::move(title), is_complex, std::move(step_info), plot_type, abscissa_scale, std::move(expression_manager), nullptr, std::vector<std::vector<std::string>>{}, std::move(metadata));
}

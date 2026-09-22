#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <regex>
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
#include "xyce_csd_file.h"
#include "xyce_output_file.h"
#include "xyce_raw_file.h"

namespace
{
    // a completed csd block: the #H header fields and the #N variable names;
    // the data values stream directly into the group columns and never live here
    struct CsdBlock
    {
        // #H header key/value fields, exactly as written by the Xyce probe outputters
        std::unordered_map<std::string, std::string> metadata;
        // variable names from the #N section
        std::vector<std::string> variable_names;
    };

    // in-progress column data: values append directly into the vectors that back
    // the final Expression objects, no temporary copies are kept
    struct CsdColumn
    {
        // variable name from the #N section
        std::string name;
        // measurement unit derived from the variable name
        std::string unit;
        // real values
        std::vector<double> values;
        // complex values
        std::vector<std::complex<double>> complex_values;
    };

    // the topology a group of blocks must share to belong to one output file
    struct CsdTopology
    {
        // variable names from the #N section
        std::vector<std::string> variable_names;
        // whether the blocks carry complex values (COMPLEXVALUES='YES')
        bool is_complex = false;
        // sweep variable name from the SWEEPVAR header
        std::string sweep_var;
        // plot type classified from the ANALYSIS header and the sweep variable
        PlotType plot_type = PlotType::UNKNOWN;
    };

    // in-progress output file: a group of consecutive blocks with identical
    // topology; each block of the group is one step of the simulation
    struct CsdGroup
    {
        // topology of the group, decided by its first data block
        CsdTopology topology;
        // title from the first data block TITLE header, with the "* " comment marker stripped
        std::string title;
        // #H header fields of the first data block
        std::unordered_map<std::string, std::string> metadata;
        // step parameter names, from the first data block SUBTITLE and SWEEP<n>PARM headers
        std::vector<std::string> step_keys;
        // abscissa values, appended progressively from the #C lines of every block
        std::vector<double> abscissa_values;
        // abscissa measurement unit
        std::string abscissa_unit;
        // column data
        std::vector<CsdColumn> columns;
        // per-step slices into the shared abscissa and column data
        std::vector<std::pair<size_t, size_t>> step_slices;
        // per-step abscissa value ranges
        std::vector<std::pair<double, double>> value_ranges;
        // per-step parameter values
        std::vector<std::vector<double>> step_values;
    };

    // parse key-value pairs from a header line: keys are bare tokens followed by
    // an equals sign and values are either single-quoted (they may contain
    // spaces, like the TIME/DATE stamp and the SUBTITLE) or run to the next
    // whitespace; this is how the Xyce probe outputters write every #H line
    void parse_header_line(const std::string& line, std::unordered_map<std::string, std::string>& headers) {
        // initialize position
        size_t pos = 0;
        while (pos < line.size()) {
            // skip leading whitespace
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
                ++pos;
            // check for end of line
            if (pos >= line.size())
                break;
            // find the equals sign
            size_t eq = line.find('=', pos);
            // a segment without an equals sign carries no key, stop parsing the line
            if (eq == std::string::npos)
                break;
            // extract the key between the current position and the equals sign
            std::string key = trim(line.substr(pos, eq - pos));
            // an empty key is not a key, stop parsing the line
            if (key.empty())
                break;
            // advance past the equals sign
            pos = eq + 1;
            // check for a quoted value
            if (pos < line.size() && line[pos] == '\'') {
                // advance past the opening quote
                ++pos;
                // find the matching closing quote
                size_t close_quote = line.find('\'', pos);
                // without a closing quote the rest of the line is the value
                if (close_quote == std::string::npos) {
                    // store the remaining text as the value
                    headers[key] = line.substr(pos);
                    // exit loop
                    break;
                }
                // store the quoted value
                headers[key] = line.substr(pos, close_quote - pos);
                // advance past the closing quote
                pos = close_quote + 1;
            }
            else {
                // an unquoted value runs to the next whitespace
                size_t val_end = pos;
                while (val_end < line.size() && !std::isspace(static_cast<unsigned char>(line[val_end])))
                    ++val_end;
                // store the unquoted value
                headers[key] = line.substr(pos, val_end - pos);
                // advance past the value
                pos = val_end;
            }
        }
    }

    // parse the single-quoted variable names of one #N line; Xyce wraps the #N
    // section at 128 characters and 12 variables per line, so one block's names
    // can span several lines
    void parse_variable_names_line(const std::string& line, std::vector<std::string>& var_names) {
        // initialize position
        size_t pos = 0;
        while (pos < line.size()) {
            // find the opening quote
            size_t open_quote = line.find('\'', pos);
            // no opening quote, the line holds no variable name
            if (open_quote == std::string::npos)
                break;
            // find the matching closing quote
            size_t close_quote = line.find('\'', open_quote + 1);
            // without a closing quote the name is unterminated, stop parsing
            if (close_quote == std::string::npos)
                break;
            // extract the variable name between the quotes
            var_names.push_back(line.substr(open_quote + 1, close_quote - open_quote - 1));
            // advance past the closing quote
            pos = close_quote + 1;
        }
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

    // strip the column index suffix from a probe data token: Xyce writes every
    // value as "number:index" (FORMAT='0 VOLTSorAMPS;EFLOAT : NODEorBRANCH;NODE'),
    // the number itself may contain a slash separating the real and imaginary parts
    std::string_view strip_column_suffix(std::string_view token) {
        // find the colon that starts the column index suffix
        size_t colon = token.rfind(':');
        // no suffix, the token is the number itself
        if (colon == std::string::npos)
            return token;
        // return the numeric part before the suffix
        return token.substr(0, colon);
    }

    // parse a probe data number into a complex value: complex data carries the
    // real and imaginary parts separated by a slash, real data is a plain number;
    // returns false when the token holds no number at all
    bool parse_data_number(std::string_view token, const bool is_complex, std::complex<double>& out) {
        // strip the column index suffix
        std::string_view number = strip_column_suffix(token);
        // complex data carries the real and imaginary parts separated by a slash
        if (is_complex) {
            // find the real/imaginary separator
            size_t slash = number.find('/');
            // without a slash the number holds only the real part
            if (slash == std::string::npos) {
                // real part
                double real = 0.0;
                // attempt to parse the real part
                if (!parse_number(number, real))
                    return false;
                // update the output value with a zero imaginary part
                out = std::complex<double>(real, 0.0);
                // indicate successful parsing
                return true;
            }
            // real and imaginary parts
            double real = 0.0;
            double imag = 0.0;
            // attempt to parse both parts
            if (!parse_number(number.substr(0, slash), real) || !parse_number(number.substr(slash + 1), imag))
                return false;
            // update the output value
            out = std::complex<double>(real, imag);
            // indicate successful parsing
            return true;
        }
        // real value
        double real = 0.0;
        // attempt to parse the number
        if (!parse_number(number, real))
            return false;
        // update the output value
        out = std::complex<double>(real, 0.0);
        // indicate successful parsing
        return true;
    }

    // determine variable type and measurement unit from the variable name; the
    // probe file carries no variable type column, so the type derives from the
    // output variable operators described in the reference guide (section 2.1.31)
    std::pair<VariableType, std::string> classify_variable(const std::string& name, const PlotType plot_type, const bool is_abscissa) {
        // abscissa classification
        if (is_abscissa) {
            // transient and AC_IC output sweep time (SWEEPVAR='Time')
            if (plot_type == PlotType::TRANSIENT || name == "Time" || name == "TIME" || name == "time") {
                // time abscissa
                return {VariableType::TIME, "s"};
            }
            // AC output sweeps frequency (SWEEPVAR='FREQ')
            if (plot_type == PlotType::AC || name == "FREQ" || name == "Frequency" || name == "frequency") {
                // frequency abscissa
                return {VariableType::FREQUENCY, "Hz"};
            }
            // a DC sweep of a node voltage carries a voltage unit
            if (!name.empty() && (name[0] == 'V' || name[0] == 'v')) {
                // voltage abscissa
                return {VariableType::VOLTAGE, "V"};
            }
            // a DC sweep of a source current carries a current unit
            if (!name.empty() && (name[0] == 'I' || name[0] == 'i')) {
                // current abscissa
                return {VariableType::CURRENT, "A"};
            }
            // unknown abscissa type
            return {VariableType::UNKNOWN, ""};
        }
        // expression output variables are enclosed in braces
        if (!name.empty() && name.front() == '{') {
            // expression output variable
            return {VariableType::EXPRESSION, ""};
        }
        // power output variables
        if (name.starts_with("P(") || name.starts_with("p(") || name.starts_with("W(") || name.starts_with("w(")) {
            // power output variable
            return {VariableType::POWER, "W"};
        }
        // phase angle output variables
        if (name.starts_with("VP(") || name.starts_with("vp(") || name.starts_with("IP(") || name.starts_with("ip(")) {
            // phase angle output variable
            return {VariableType::PHASE, "\u00b0"};
        }
        // voltage output variables, including the real, imaginary, magnitude and decibel operators
        if (name.starts_with("V(") || name.starts_with("v(") || name.starts_with("VR(") || name.starts_with("vr(") || name.starts_with("VI(") || name.starts_with("vi(") || name.starts_with("VM(") || name.starts_with("vm(") || name.starts_with("VDB(") || name.starts_with("vdb(")) {
            // voltage output variable
            return {VariableType::VOLTAGE, "V"};
        }
        // current output variables, including the real, imaginary, magnitude and decibel operators
        if (name.starts_with("I(") || name.starts_with("i(") || name.starts_with("IR(") || name.starts_with("ir(") || name.starts_with("II(") || name.starts_with("ii(") || name.starts_with("IM(") || name.starts_with("im(") || name.starts_with("IDB(") || name.starts_with("idb(")) {
            // current output variable
            return {VariableType::CURRENT, "A"};
        }
        // device lead current variables carry a two-letter designator, like IC(Q1) or IB(M1)
        if (name.size() >= 3 && (name[0] == 'I' || name[0] == 'i') && name[2] == '(') {
            // device lead current variable
            return {VariableType::CURRENT, "A"};
        }
        // unknown output variable
        return {VariableType::UNKNOWN, ""};
    }

    // detect the abscissa scale from a sequence of values
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

    // classify the plot type of a block from the ANALYSIS header and the sweep
    // variable: Xyce writes 'Transient Analysis' for TRAN, 'DC Sweep' for DC and
    // 'AC Sweep' for both AC and AC_IC; the AC_IC print type shares the AC Sweep
    // header but sweeps time and produces a circuit-file.TD.csd file (reference
    // guide, print analysis tables 2-19 to 2-22)
    PlotType classify_plot_type(const std::unordered_map<std::string, std::string>& metadata, const std::string& sweep_var) {
        // normalized analysis header, empty when the header is missing
        std::string analysis;
        // extract the analysis header when present
        if (const auto it = metadata.find("ANALYSIS"); it != metadata.end())
            analysis = to_upper(it->second);
        // transient analysis
        if (analysis.find("TRANSIENT") != std::string::npos) {
            return PlotType::TRANSIENT;
        }
        // DC sweep analysis
        if (analysis.find("DC") != std::string::npos) {
            return PlotType::DC;
        }
        // AC sweep analysis, shared by the AC and AC_IC print types
        if (analysis.find("AC") != std::string::npos) {
            // the AC_IC output is time-domain data recognized by the Time sweep variable
            if (to_upper(sweep_var) == "TIME")
                return PlotType::TRANSIENT;
            // frequency-domain AC output
            return PlotType::AC;
        }
        // no analysis header, fall back to the sweep variable
        if (to_upper(sweep_var) == "TIME") {
            return PlotType::TRANSIENT;
        }
        if (to_upper(sweep_var) == "FREQ" || to_upper(sweep_var) == "FREQUENCY") {
            return PlotType::AC;
        }
        // unrecognized output
        return PlotType::UNKNOWN;
    }

    // extract the step parameter names and values of one block: with .STEP the
    // SUBTITLE header reads "Step param <name> = <value> ..." and nested DC
    // sweeps carry SWEEP<n>PARM/SWEEP<n>VALUE header pairs
    void extract_step_parameters(const std::unordered_map<std::string, std::string>& metadata, std::vector<std::string>& param_names, std::vector<double>& param_values) {
        // parse param = value pairs from the SUBTITLE header when present
        if (const auto it = metadata.find("SUBTITLE"); it != metadata.end()) {
            // subtitle string
            const std::string& subtitle = it->second;
            // regex for param = value pairs
            std::regex pair_re(R"((\S+)\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?))");
            // regex iterators
            auto begin = std::sregex_iterator(subtitle.begin(), subtitle.end(), pair_re);
            auto end = std::sregex_iterator();
            // loop matches
            for (auto match = begin; match != end; ++match) {
                // attempt to parse the parameter value
                try {
                    // parse the parameter value
                    double value = std::stod((*match)[2].str());
                    // append the parameter name
                    param_names.push_back((*match)[1].str());
                    // append the parameter value
                    param_values.push_back(value);
                }
                catch (...) {
                    // ignore malformed numbers
                }
            }
        }
        // nested DC sweep parameters, ordered by sweep index
        std::map<int, std::pair<std::string, double>> sweep_params;
        // scan the metadata for SWEEP<n>PARM keys
        for (const auto& [key, value] : metadata) {
            // a parameter key starts with SWEEP, ends with PARM and carries the sweep index in between
            if (!key.starts_with("SWEEP") || !key.ends_with("PARM") || key.size() <= 9)
                continue;
            // attempt to parse the sweep index and the matching value
            try {
                // parse the sweep index
                int index = std::stoi(key.substr(5, key.size() - 9));
                // find the matching SWEEP<n>VALUE key
                const auto value_it = metadata.find("SWEEP" + std::to_string(index) + "VALUE");
                // without a value key the pair is incomplete
                if (value_it == metadata.end())
                    continue;
                // parse the parameter value
                double param_value = std::stod(value_it->second);
                // record the pair
                sweep_params.emplace(index, std::make_pair(value, param_value));
            }
            catch (...) {
                // ignore malformed sweep indices and values
            }
        }
        // append the nested sweep parameters in sweep order
        for (const auto& [index, param] : sweep_params) {
            // append the parameter name
            param_names.push_back(param.first);
            // append the parameter value
            param_values.push_back(param.second);
        }
    }

    // build the output file from a completed group, returning nullptr when the group holds no usable data
    std::shared_ptr<XyceOutputFile> build_output_file(const std::filesystem::path& filename, CsdGroup& group) {
        // no data, nothing to build
        if (group.step_slices.empty())
            return nullptr;
        // detect the abscissa scale from the first step: logarithmic sweeps apply
        // to AC and DC analyses only, transient time steps are never logarithmic
        AbscissaScale abscissa_scale = AbscissaScale::LINEAR;
        if (group.topology.plot_type == PlotType::AC || group.topology.plot_type == PlotType::DC) {
            // first step slice
            const auto [start, end] = group.step_slices.front();
            // detect the scale from the first step abscissa points
            if (end - start >= 3)
                abscissa_scale = detect_abscissa_scale(std::vector<double>(group.abscissa_values.begin() + static_cast<long>(start), group.abscissa_values.begin() + static_cast<long>(end)));
        }
        // abscissa expression: the sweep values from the #C lines are always real
        Expression<double> abscissa(group.topology.sweep_var, std::move(group.abscissa_values), group.step_slices, group.abscissa_unit);
        // expressions list
        std::vector<AnyExpression> expressions;
        // reserve space for the abscissa plus the output variables
        expressions.reserve(group.columns.size() + 1);
        // append the abscissa as the first expression
        expressions.emplace_back(std::move(abscissa));
        // loop columns to append the variable expressions
        for (CsdColumn& column : group.columns) {
            // check whether the column holds complex values
            if (!column.complex_values.empty()) {
                // complex expression
                expressions.emplace_back(Expression<std::complex<double>>(column.name, std::move(column.complex_values), group.step_slices, column.unit));
                // next column
                continue;
            }
            // real expression
            expressions.emplace_back(Expression<double>(column.name, std::move(column.values), group.step_slices, column.unit));
        }
        // build expression manager (constructor takes lvalue references and moves from them)
        ExpressionManager expression_manager(expressions, group.step_slices);
        // build step information
        StepInformation step_info(std::move(group.step_keys), std::move(group.step_values), std::move(group.value_ranges));
        // capture values for logging before the moves
        const size_t variables_count = expressions.size() - 1;
        const size_t steps_count = step_info.length();
        // build the output file with no suggested plots and the first block header fields as metadata
        auto xyce_file = std::make_shared<XyceOutputFile>(filename, group.title, group.topology.is_complex, std::move(step_info), group.topology.plot_type, abscissa_scale, std::move(expression_manager), nullptr, std::vector<std::vector<std::string>>{}, std::move(group.metadata));
        // log completion
        spdlog::info("Successfully parsed Xyce CSD file: {}, variables: {}, steps: {}", filename.string(), variables_count, steps_count);
        // return the file
        return xyce_file;
    }
} // namespace

std::optional<std::shared_ptr<XyceOutputFile>> xyce_csd_file_parser(const std::filesystem::path& filename) {
    // check if file exists
    if (!std::filesystem::exists(filename))
        return {};
    // track timing
    auto start_time = std::chrono::steady_clock::now();
    // log information
    spdlog::info("Parsing CSD file: {}", filename.string());
    // open the file
    std::ifstream file(filename);
    if (!file.is_open())
        return {};
    // reused line and token buffers: lines are streamed one by one and no whole-file copy is kept in memory
    std::string line;
    std::vector<std::string_view> tokens;
    // parsing state: scanning for a block header, collecting the #H header
    // fields, collecting the #N variable names or reading probe data
    enum class State
    {
        SCAN,
        HEADER,
        NAMES,
        DATA
    };
    // current state
    State state = State::SCAN;
    // the block being parsed, reset at every #H marker
    CsdBlock block;
    // the group accumulates the blocks of the output file being built
    CsdGroup group;
    // whether the current block holds at least one data point
    bool block_has_data = false;
    // whether a data point is open, that is a #C line was seen and its values have not been consumed
    bool in_point = false;
    // sweep value of the current data point, parsed from the #C line
    double point_abscissa = 0.0;
    // number of values still expected for the current data point
    size_t point_values_left = 0;
    // number of values already appended for the current data point
    size_t point_values_seen = 0;
    // offset of the current block's first point into the shared abscissa data
    size_t block_row_start = 0;
    // whether the parse failed on a topology mismatch, aborting the run
    bool topology_failed = false;
    // whether the current block is the one that initialized the group
    bool group_owns_block = false;
    // initialize the group from the first data block of the file
    auto init_group = [&]() {
        // the block must carry variable names to become a group
        if (block.variable_names.empty()) {
            // mark the parse as failed
            topology_failed = true;
            // exit
            return;
        }
        // derive the topology fields from the block header
        group.topology.variable_names = block.variable_names;
        group.topology.is_complex = block.metadata.contains("COMPLEXVALUES") && to_upper(block.metadata.at("COMPLEXVALUES")) == "YES";
        group.topology.sweep_var = block.metadata.contains("SWEEPVAR") ? block.metadata.at("SWEEPVAR") : "";
        group.topology.plot_type = classify_plot_type(block.metadata, group.topology.sweep_var);
        // default the sweep variable name for unclassified sweeps
        if (group.topology.sweep_var.empty())
            group.topology.sweep_var = group.topology.plot_type == PlotType::AC ? "FREQ" : "Time";
        // assign the title, stripping the "* " comment marker Xyce prefixes to the netlist name
        group.title = block.metadata.contains("TITLE") ? block.metadata.at("TITLE") : "";
        if (group.title.starts_with("* "))
            group.title = group.title.substr(2);
        // capture the first block header fields as file metadata
        group.metadata = block.metadata;
        // extract the step parameter keys of the block
        std::vector<double> param_values;
        // parse the parameters, only the names are kept as keys
        extract_step_parameters(block.metadata, group.step_keys, param_values);
        // build the columns from the variable names
        group.columns.reserve(block.variable_names.size());
        for (const std::string& name : block.variable_names) {
            // classify the variable
            const auto [variable_type, unit] = classify_variable(name, group.topology.plot_type, false);
            // append the column descriptor
            CsdColumn column;
            column.name = name;
            column.unit = unit;
            group.columns.push_back(std::move(column));
        }
        // classify the abscissa
        group.abscissa_unit = classify_variable(group.topology.sweep_var, group.topology.plot_type, true).second;
    };
    // complete the current data point: pad the columns that received no value and
    // append the point abscissa, keeping every column aligned with the abscissa
    auto end_point = [&]() {
        // no open data point
        if (!in_point)
            return;
        // pad the columns that received no value for this point
        for (size_t i = point_values_seen; i < group.columns.size(); ++i) {
            // check whether the group holds complex data
            if (group.topology.is_complex)
                group.columns[i].complex_values.emplace_back(0.0, 0.0);
            else
                group.columns[i].values.push_back(0.0);
        }
        // append the abscissa value of the point
        group.abscissa_values.push_back(point_abscissa);
        // mark the block as holding data
        block_has_data = true;
        // close the data point
        in_point = false;
    };
    // finalize the current block on its terminator: the block that initialized
    // the group skips the topology check, later blocks must match the group
    // topology and become one step each
    auto finalize_block = [&]() {
        // complete the open data point, if any
        end_point();
        // the block initialized the group itself
        bool owns_group = group_owns_block;
        // clear the ownership flag
        group_owns_block = false;
        // the block holds no data or the parse already failed, nothing to record
        if (!block_has_data || topology_failed)
            return;
        // derive the topology fields of the block from its header
        const std::string sweep_var = block.metadata.contains("SWEEPVAR") ? block.metadata.at("SWEEPVAR") : "";
        // normalize the complex flag
        bool is_complex = false;
        // check for the COMPLEXVALUES header
        if (const auto it = block.metadata.find("COMPLEXVALUES"); it != block.metadata.end())
            is_complex = to_upper(it->second) == "YES";
        // classify the plot type of the block
        const PlotType plot_type = classify_plot_type(block.metadata, sweep_var);
        // the probe writers emit identical variable lists for every step of one
        // .PRINT statement, a mismatching block aborts the parse
        if (!owns_group && (block.variable_names != group.topology.variable_names || is_complex != group.topology.is_complex || sweep_var != group.topology.sweep_var || plot_type != group.topology.plot_type)) {
            // mark the parse as failed
            topology_failed = true;
            // exit
            return;
        }
        // record the step slice of the block over the shared data
        group.step_slices.emplace_back(block_row_start, group.abscissa_values.size());
        // compute the abscissa value range of the step
        double step_min = std::numeric_limits<double>::max();
        double step_max = -std::numeric_limits<double>::max();
        for (size_t i = block_row_start; i < group.abscissa_values.size(); ++i) {
            // update the step minimum
            if (group.abscissa_values[i] < step_min)
                step_min = group.abscissa_values[i];
            // update the step maximum
            if (group.abscissa_values[i] > step_max)
                step_max = group.abscissa_values[i];
        }
        // append the step value range, empty steps get a zero range
        if (step_min <= step_max)
            group.value_ranges.emplace_back(step_min, step_max);
        else
            group.value_ranges.emplace_back(0.0, 0.0);
        // extract the step parameter values of the block
        std::vector<std::string> param_names;
        std::vector<double> param_values;
        // parse the parameters from the header
        extract_step_parameters(block.metadata, param_names, param_values);
        // append the step parameter values of the block
        if (!param_values.empty())
            group.step_values.push_back(std::move(param_values));
        else if (!group.step_keys.empty())
            group.step_values.emplace_back(group.step_keys.size(), 0.0);
    };
    // process lines one by one
    while (std::getline(file, line) && !topology_failed) {
        // drop the carriage return left by Windows line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // trim whitespace
        line = trim(line);
        // skip blank lines
        if (line.empty())
            continue;
        // block header marker
        if (line == "#H") {
            // close the previous block when its terminator was not seen
            if (state != State::SCAN)
                finalize_block();
            // reset the block state
            block = CsdBlock();
            block_has_data = false;
            in_point = false;
            // the new block does not own the group
            group_owns_block = false;
            // collect the header lines
            state = State::HEADER;
            // next line
            continue;
        }
        // block terminator marker
        if (line == "#;") {
            // close the open data point and finalize the block
            end_point();
            finalize_block();
            // reset the block state
            block = CsdBlock();
            block_has_data = false;
            // the next block does not own the group
            group_owns_block = false;
            // scan for the next block header
            state = State::SCAN;
            // stray terminators are skipped: the AC probe writer may emit two in a
            // row at the end of each .STEP (reference guide print table description 16)
            continue;
        }
        // process the line by state
        switch (state) {
        case State::SCAN:
            // outside a block only block headers are meaningful
            break;
        case State::HEADER:
            // the variable names section follows
            if (line == "#N") {
                // collect the variable names
                state = State::NAMES;
                // next line
                continue;
            }
            // collect the key/value fields of the header line
            parse_header_line(line, block.metadata);
            // next line
            break;
        case State::NAMES:
            // data point marker: "#C <sweep value> <value count>"
            if (line.starts_with("#C")) {
                // record the block's first data row offset
                block_row_start = group.abscissa_values.size();
                // initialize the group from the first data block, or validate the
                // variable list against the group topology
                if (group.columns.empty()) {
                    // initialize the group
                    init_group();
                    // the block initialized the group itself
                    group_owns_block = true;
                }
                else if (block.variable_names != group.topology.variable_names) {
                    // the block variable list must match the group topology
                    topology_failed = true;
                }
                // exit the loop on failure
                if (topology_failed)
                    break;
                // tokenize the line
                tokenize_line(line, tokens);
                // the line must carry the sweep value and the value count
                if (tokens.size() < 3 || !parse_number(tokens[1], point_abscissa)) {
                    // skip the malformed point
                    in_point = false;
                    // next line
                    break;
                }
                // parse the announced value count, falling back to the variable count
                double announced = 0.0;
                // attempt to parse the count token
                if (!parse_number(tokens[2], announced) || announced < 1.0)
                    announced = static_cast<double>(block.variable_names.size());
                // the point cannot carry more values than variables
                point_values_left = std::min<size_t>(static_cast<size_t>(announced), block.variable_names.size());
                // start the data point
                in_point = true;
                point_values_seen = 0;
                // switch to the data state
                state = State::DATA;
                // next line
                break;
            }
            // any other control marker ends the block, which holds no data
            if (line.starts_with("#")) {
                // finalize the block
                finalize_block();
                // reset the block state
                block = CsdBlock();
                block_has_data = false;
                // the next block does not own the group
                group_owns_block = false;
                // scan for the next block header
                state = State::SCAN;
                // next line
                break;
            }
            // collect the quoted variable names of the line
            parse_variable_names_line(line, block.variable_names);
            // next line
            break;
        case State::DATA:
            // a #C line opens the following data point
            if (line.starts_with("#C")) {
                // complete the previous point when its values are exhausted
                end_point();
                // tokenize the line
                tokenize_line(line, tokens);
                // the line must carry the sweep value and the value count
                if (tokens.size() < 3 || !parse_number(tokens[1], point_abscissa)) {
                    // skip the malformed point
                    in_point = false;
                    // next line
                    break;
                }
                // parse the announced value count, falling back to the variable count
                double announced = 0.0;
                // attempt to parse the count token
                if (!parse_number(tokens[2], announced) || announced < 1.0) {
                    // count token invalid, fall back to the variable count
                    announced = static_cast<double>(group.columns.size());
                }
                // the point cannot carry more values than variables
                point_values_left = std::min<size_t>(static_cast<size_t>(announced), group.columns.size());
                // start the data point
                in_point = true;
                point_values_seen = 0;
                // next line
                break;
            }
            // probe data line: tokens of the form "number[:index]" or "real/imag[:index]"
            // tokenize the line
            tokenize_line(line, tokens);
            // process the tokens of the line
            for (std::string_view token : tokens) {
                // the point is complete, ignore surplus tokens
                if (point_values_left == 0)
                    break;
                // attempt to parse the token, complex data carries the real and imaginary parts
                std::complex<double> value;
                // a token without a parsable number contributes a zero value, keeping every column aligned
                if (!parse_data_number(token, group.topology.is_complex, value))
                    value = std::complex<double>(0.0, 0.0);
                // check whether the group holds complex data
                if (group.topology.is_complex) {
                    // append the complex value to the target column
                    group.columns[point_values_seen].complex_values.push_back(value);
                }
                else {
                    // append the real value to the target column
                    group.columns[point_values_seen].values.push_back(value.real());
                }
                // advance the value counters
                ++point_values_seen;
                --point_values_left;
            }
            // complete the point when all announced values arrived
            if (point_values_left == 0)
                end_point();
            // next line
            break;
        }
    }
    // finalize the last block, terminated by the end of the file
    if (!topology_failed)
        finalize_block();
    // the parse failed on a topology mismatch, no output file is produced
    if (topology_failed) {
        // log a warning
        spdlog::warn("CSD file contains mismatching steps: {}", filename.string());
        // exit
        return {};
    }
    // check a usable group was built
    if (group.columns.empty() || group.step_slices.empty()) {
        // log a warning
        spdlog::warn("CSD file contains no data: {}", filename.string());
        // exit
        return {};
    }
    // build the output file
    auto output_file = build_output_file(filename, group);
    // the build fails when no file was produced
    if (!output_file)
        return {};
    // log completion with the elapsed time
    spdlog::info("Parsed CSD file: {}, elapsed time: {}ms", filename.string(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count());
    // return the file
    return output_file;
}

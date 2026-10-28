#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "core/util.h"
#include "step_parameters.h"

// normalize spaces around equals sign in a directive string
static std::string normalize_equals(const std::string& directive) {
    // use regex to normalize whitespace around '='
    static const std::regex eq_re("\\s*=\\s*");
    // replace with plain '='
    return std::regex_replace(directive, eq_re, "=");
}

StepParameters::StepParameters() :
    sweep_mode("LIN"), enabled(false) {}

StepParameters::StepParameters(std::string sweep_mode, std::string variable, std::string start, std::string stop, std::string step, std::string points, std::vector<std::string> list_values, std::string data_table_name, bool enabled) :
    sweep_mode(std::move(sweep_mode)), variable(std::move(variable)), start(std::move(start)), stop(std::move(stop)), step(std::move(step)), points(std::move(points)), list_values(std::move(list_values)), data_table_name(std::move(data_table_name)), enabled(enabled) {}

std::optional<StepParameters> StepParameters::from_single_directive(const std::string& directive) {
    // normalize spaces around equals sign
    const std::string normalized = normalize_equals(directive);
    // tokenize the normalized directive
    const auto tokens = tokenize(normalized);
    // skip empty or malformed directives
    if (tokens.empty()) {
        // return none
        return std::nullopt;
    }
    // only process .STEP directives
    if (to_upper(tokens[0]) != ".STEP") {
        // return none
        return std::nullopt;
    }
    // init default values for all possible sweep parameters
    std::string sweep_mode = "LIN";
    // init variable
    std::string variable;
    // init start
    std::string start;
    // init stop
    std::string stop;
    // init step value
    std::string step_val;
    // init points
    std::string points;
    // init list values
    std::vector<std::string> list_values;
    // init data table name
    std::string data_table_name;
    // handle data-driven sweep syntax: .STEP DATA=<tablename>
    if (tokens.size() == 2) {
        const auto upper2 = to_upper(tokens[1]);
        if (upper2.substr(0, 5) == "DATA=" && tokens[1].find('=') != std::string::npos) {
            // set data sweep mode
            sweep_mode = "DATA";
            // extract table name from the assignment token
            data_table_name = tokens[1].substr(tokens[1].find('=') + 1);
            // return populated instance
            return StepParameters(sweep_mode, variable, start, stop, step_val, points, list_values, data_table_name, true);
        }
    }
    // skip processing if only the command was provided
    if (tokens.size() < 2) {
        // return none for incomplete directive
        return std::nullopt;
    }
    // capture the second token for mode detection
    const std::string second = to_upper(tokens[1]);
    // handle decade or octave log sweeps: .STEP DEC|OCT var start stop points
    if (second == "DEC" || second == "OCT") {
        // set the log sweep mode
        sweep_mode = second;
        // parse positional parameters when enough tokens are present
        if (tokens.size() >= 6) {
            // capture sweep variable name
            variable = tokens[2];
            // capture start value
            start = tokens[3];
            // capture stop value
            stop = tokens[4];
            // capture points count
            points = tokens[5];
        }
        // return populated instance
        return StepParameters(sweep_mode, variable, start, stop, step_val, points, list_values, data_table_name, true);
    }
    // handle explicit list sweeps: .STEP var LIST val [val ...]
    if (tokens.size() >= 3 && to_upper(tokens[2]) == "LIST") {
        // set list sweep mode
        sweep_mode = "LIST";
        // capture sweep variable name
        variable = tokens[1];
        // capture all subsequent tokens as list values
        for (size_t i = 3; i < tokens.size(); ++i)
            list_values.push_back(std::string(tokens[i]));
        // return populated instance
        return StepParameters(sweep_mode, variable, start, stop, step_val, points, list_values, data_table_name, true);
    }
    // handle explicit LIN keyword: .STEP LIN var start stop step
    if (second == "LIN") {
        // parse parameters from explicit linear syntax
        if (tokens.size() >= 6) {
            // capture sweep variable name
            variable = tokens[2];
            // capture start value
            start = tokens[3];
            // capture stop value
            stop = tokens[4];
            // capture step value
            step_val = tokens[5];
        }
        // return populated instance
        return StepParameters("LIN", variable, start, stop, step_val, points, list_values, data_table_name, true);
    }
    // handle implicit linear syntax: .STEP var start stop step
    if (tokens.size() >= 5) {
        // capture sweep variable name
        variable = tokens[1];
        // capture start value
        start = tokens[2];
        // capture stop value
        stop = tokens[3];
        // capture step value
        step_val = tokens[4];
    }
    // return populated instance
    return StepParameters("LIN", variable, start, stop, step_val, points, list_values, data_table_name, true);
}

std::vector<StepParameters> StepParameters::all_from_xyce_directives(const std::vector<std::string>& directives) {
    // collect all parsed step parameters in order
    std::vector<StepParameters> results;
    // iterate all directives to find .STEP statements
    for (const auto& directive : directives) {
        // attempt to parse each directive as a step
        auto parsed = from_single_directive(directive);
        // add to results if a valid step was found
        if (parsed.has_value()) {
            // append to list
            results.push_back(std::move(*parsed));
        }
    }
    // return vector preserving declaration order
    return results;
}

std::optional<StepParameters> StepParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // parse all step directives and return the first, or a disabled default
    const auto all_steps = all_from_xyce_directives(directives);
    // return first step if any are found
    if (!all_steps.empty()) {
        // return the first entry
        return all_steps[0];
    }
    // return a disabled default when no step directive is present
    return StepParameters();
}

std::vector<std::string> StepParameters::to_xyce_directives() const {
    // return empty list when sweep is disabled
    if (!enabled) {
        // return empty list
        return {};
    }
    // build the directive string based on the active sweep mode
    std::string directive;
    if (sweep_mode == "DATA") {
        // format data-driven sweep
        directive = ".STEP DATA=" + data_table_name;
    }
    // handle list sweep
    else if (sweep_mode == "LIST") {
        // format explicit list sweep
        directive = ".STEP " + variable + " LIST";
        for (const auto& val : list_values) {
            directive += ' ' + val;
        }
    }
    // handle log sweeps (DEC/OCT)
    else if (sweep_mode == "DEC" || sweep_mode == "OCT") {
        // format log sweep with explicit keyword
        directive = ".STEP " + sweep_mode + " " + variable + " " + start + " " + stop + " " + points;
    }
    // handle linear sweep — always emit explicit LIN keyword
    else {
        // format linear sweep with explicit LIN keyword
        directive = ".STEP LIN " + variable + " " + start + " " + stop + " " + step;
    }
    // return the directive as a single-item list
    return {directive};
}

namespace
{
    // parse the leading numeric portion of a value (supports SI suffixes like 1k);
    // returns false when the value does not start with a number
    [[nodiscard]] bool parse_number(const std::string& text, double& value) {
        if (text.empty()) {
            return false;
        }
        const char* start = text.c_str();
        char* end = nullptr;
        value = std::strtod(start, &end);
        return end != start;
    }
} // namespace

std::optional<std::string> StepParameters::validate() const {
    // only validate when the step is enabled
    if (!enabled) {
        return std::nullopt;
    }
    // data sweeps require a data table name
    if (sweep_mode == "DATA") {
        if (data_table_name.empty()) {
            return "STEP DATA sweep requires a data table name";
        }
        return std::nullopt;
    }
    // list sweeps require a sweep variable and at least one value
    if (sweep_mode == "LIST") {
        if (variable.empty()) {
            return "STEP LIST sweep requires a sweep variable";
        }
        if (list_values.empty()) {
            return "STEP LIST sweep requires at least one value";
        }
        return std::nullopt;
    }
    // validate common sweep fields
    if (variable.empty()) {
        return "STEP " + sweep_mode + " sweep: sweep variable is required";
    }
    if (start.empty()) {
        return "STEP " + sweep_mode + " sweep: start value is required";
    }
    if (stop.empty()) {
        return "STEP " + sweep_mode + " sweep: stop value is required";
    }
    // dec/oct validate points is a positive integer and start > 0
    if (sweep_mode == "DEC" || sweep_mode == "OCT") {
        if (points.empty()) {
            return "STEP " + sweep_mode + " sweep: points value is required";
        }
        double points_value = 0.0;
        if (parse_number(points, points_value) && (points_value < 1.0 || std::floor(points_value) != points_value)) {
            return "STEP " + sweep_mode + " sweep: points value must be a positive integer";
        }
        double start_value = 0.0;
        if (parse_number(start, start_value) && start_value <= 0.0) {
            return "STEP " + sweep_mode + " sweep: start value must be greater than zero";
        }
        return std::nullopt;
    }
    // linear sweep: step is required; for descending sweeps, step must be negative
    if (step.empty()) {
        return "STEP LIN sweep: step value is required";
    }
    double start_value = 0.0;
    double stop_value = 0.0;
    if (parse_number(start, start_value) && parse_number(stop, stop_value) && start_value > stop_value) {
        double step_value = 0.0;
        if (parse_number(step, step_value) && step_value >= 0.0) {
            return "STEP LIN sweep: step value must be negative for descending sweep";
        }
    }
    return std::nullopt;
}

bool StepParameters::operator==(const StepParameters& other) const {
    // compare all fields for equality
    return sweep_mode == other.sweep_mode && variable == other.variable && start == other.start && stop == other.stop && step == other.step && points == other.points && list_values == other.list_values && data_table_name == other.data_table_name && enabled == other.enabled;
}

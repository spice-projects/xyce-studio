#include <cctype>
#include <ranges>
#include <string>
#include <vector>

#include "../core/util.h"
#include "../netlist/netlist.h"
#include "sens_parameter.h"

SensParameter::SensParameter(std::string analysis_context, std::string objective_mode, std::vector<std::string> objective_values, std::vector<std::string> parameter_list, bool direct, bool adjoint, std::optional<PrintParameters> print_parameters, std::vector<std::string> option_tokens) :
    analysis_context(std::move(analysis_context)), objective_mode(std::move(objective_mode)), objective_values(std::move(objective_values)), parameter_list(std::move(parameter_list)), direct(direct), adjoint(adjoint), print_parameters(std::move(print_parameters)), option_tokens(std::move(option_tokens)) {}

std::optional<SensParameter> SensParameter::from_xyce_directives(const std::vector<std::string>& directives) {
    // init directive found flag
    bool found = false;
    // init parsed parameters
    std::string analysis_context;
    // init objective mode
    std::string objective_mode;
    // init objective values
    std::vector<std::string> objective_values;
    // init parameter list
    std::vector<std::string> parameter_list;
    // init direct method flag
    bool direct = false;
    // init adjoint method flag
    bool adjoint = false;
    // init print parameters
    std::optional<PrintParameters> print_parameters;
    // capture the verbatim option tokens so every RG 2.1.25.16 key round-trips unchanged
    std::vector<std::string> option_tokens;
    // iterate provided directives
    for (const auto& directive : directives) {
        // tokenize directive
        const auto tokens = tokenize(directive);
        // skip empty directives
        if (tokens.empty()) {
            // continue to next iteration
            continue;
        }
        // extract command keyword
        const auto cmd = to_upper(tokens[0]);
        // check for sens command
        if (cmd == ".SENS") {
            // set found flag
            found = true;
            // iterate over tokens
            for (size_t i = 1; i < tokens.size(); ++i) {
                const auto& token = tokens[i];
                // skip if no equals sign
                const auto eq_pos = token.find('=');
                if (eq_pos == std::string::npos) {
                    // continue iteration
                    continue;
                }
                // split key and value
                const std::string key = to_lower(token.substr(0, eq_pos));
                const auto val = token.substr(eq_pos + 1);
                // check for objective modes
                if (key == "objfunc" || key == "objvars" || key == "acobjfunc") {
                    // set objective mode
                    objective_mode = key;
                    // parse and clean values by stripping braces and splitting by comma;
                    // keep the stripped string alive so the split views stay valid
                    const auto stripped = strip_chars(val, "{}");
                    const auto parts = split_by(stripped, ',');
                    // store objective values
                    for (const auto& part : parts) {
                        // trim whitespace
                        const auto start = part.find_first_not_of(" \t");
                        const auto end = part.find_last_not_of(" \t");
                        if (start != std::string::npos) {
                            objective_values.push_back(std::string(part.substr(start, end - start + 1)));
                        }
                    }
                }
                // check for parameters
                else if (key == "param") {
                    // split parameter list by comma
                    const auto parts = split_by(val, ',');
                    // trim whitespace from each part
                    for (const auto& part : parts) {
                        // find first non-whitespace
                        const auto start = part.find_first_not_of(" \t");
                        // find last non-whitespace
                        const auto end = part.find_last_not_of(" \t");
                        if (start != std::string::npos) {
                            // add trimmed part
                            parameter_list.push_back(std::string(part.substr(start, end - start + 1)));
                        }
                    }
                }
            }
        }
        // check for sensitivity options
        if (cmd == ".OPTIONS" && tokens.size() > 1 && to_upper(tokens[1]) == "SENSITIVITY") {
            // iterate over tokens
            for (size_t i = 2; i < tokens.size(); ++i) {
                const auto& token = tokens[i];
                // keep the original token so the statement round-trips unchanged
                option_tokens.push_back(std::string(token));
                const auto lower_token = to_lower(token);
                // check direct method
                if (lower_token.substr(0, 7) == "direct=") {
                    // set direct flag
                    direct = (lower_token.substr(7) == "1");
                }
                // check adjoint method
                if (lower_token.substr(0, 8) == "adjoint=") {
                    // set adjoint flag
                    adjoint = (lower_token.substr(8) == "1");
                }
            }
        }
        // check for print directive
        if (cmd == ".PRINT" && tokens.size() > 1 && to_upper(tokens[1]) == "SENS") {
            // create print parameters
            print_parameters = PrintParameters::from_xyce_statement(directive);
        }
    }
    // check if directive was found
    if (!found) {
        // return none
        return std::nullopt;
    }
    // return new instance carrying the captured option tokens
    return SensParameter(analysis_context, objective_mode, std::move(objective_values), std::move(parameter_list), direct, adjoint, std::move(print_parameters), std::move(option_tokens));
}

std::vector<std::string> SensParameter::to_xyce_directives(const NetlistTopology& topology) const {
    // init line list
    std::vector<std::string> lines;
    // topology reserved for future wildcard expansion; pass-through for now
    (void)topology;
    // build objective directive string
    std::string obj_str;
    for (size_t i = 0; i < objective_values.size(); ++i) {
        // add separator
        if (i > 0) {
            obj_str += ',';
        }
        // add value
        obj_str += objective_values[i];
    }
    // build parameter string
    std::string param_str;
    for (size_t i = 0; i < parameter_list.size(); ++i) {
        // add separator
        if (i > 0) {
            param_str += ',';
        }
        // add parameter
        param_str += parameter_list[i];
    }
    // format objective line for objfunc mode
    if (objective_mode == "objfunc") {
        // add directive line
        lines.push_back(".SENS objfunc={" + obj_str + "} param=" + param_str);
    }
    // emit the options line from the captured tokens when the statement came from the netlist
    if (!option_tokens.empty()) {
        // join the captured tokens back into a single line
        std::string joined;
        for (const auto& token : option_tokens) {
            // separate every token but the first
            if (!joined.empty()) {
                // append the space separator
                joined += " ";
            }
            // append the token verbatim
            joined += token;
        }
        // add the verbatim options line preserving every RG 2.1.25.16 key
        lines.push_back(".OPTIONS SENSITIVITY " + joined);
    }
    // synthesize the direct/adjoint options line for instances built without a parsed statement
    else {
        // format options string
        const std::string options_line = ".OPTIONS SENSITIVITY direct=" + std::string(direct ? "1" : "0") + " adjoint=" + std::string(adjoint ? "1" : "0");
        // add options line
        lines.push_back(options_line);
    }
    // check for print parameters
    if (print_parameters.has_value()) {
        // add print directive
        lines.push_back(print_parameters->to_xyce_statement());
    }
    // return directive lines
    return lines;
}

bool SensParameter::operator==(const SensParameter& other) const {
    // compare all fields for equality
    return analysis_context == other.analysis_context && objective_mode == other.objective_mode && objective_values == other.objective_values && parameter_list == other.parameter_list && direct == other.direct && adjoint == other.adjoint && print_parameters == other.print_parameters && option_tokens == other.option_tokens;
}

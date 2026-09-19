#include <cctype>
#include <string>
#include <vector>

#include "../core/util.h"
#include "../netlist/netlist.h"
#include "pce_parameters.h"

namespace
{
    // parse a comma-separated directive value into a trimmed string list
    [[nodiscard]] std::vector<std::string> split_list(const std::string_view value) {
        // init the parsed list
        std::vector<std::string> parts;
        // split by comma and trim each entry
        for (const auto& part : split_by(value, ',')) {
            // find the trimmed bounds of the entry
            const auto start = part.find_first_not_of(" \t");
            // skip empty entries
            if (start == std::string_view::npos) {
                continue;
            }
            // find the trailing bound of the entry
            const auto end = part.find_last_not_of(" \t");
            // append the trimmed entry
            parts.emplace_back(part.substr(start, end - start + 1));
        }
        // return the parsed list
        return parts;
    }

    // join a string list into a comma-separated directive value
    [[nodiscard]] std::string join_list(const std::vector<std::string>& values) {
        // init the joined value
        std::string joined;
        // append each entry with its separator
        for (size_t i = 0; i < values.size(); ++i) {
            // add separator
            if (i > 0) {
                joined += ',';
            }
            // add the entry
            joined += values[i];
        }
        // return the joined value
        return joined;
    }

    // parse KEY=VALUE tokens into the options map with uppercased keys;
    // bare tokens without '=' are stored as KEY=1 flags
    void parse_option_tokens(const std::vector<std::string_view>& tokens, size_t start_index, std::map<std::string, std::string>& options) {
        // iterate over the option tokens
        for (size_t i = start_index; i < tokens.size(); ++i) {
            // find the key/value separator
            const auto eq_pos = tokens[i].find('=');
            // skip tokens without a separator
            if (eq_pos == std::string_view::npos) {
                continue;
            }
            // extract and uppercase the key
            const auto key = to_upper(tokens[i].substr(0, eq_pos));
            // extract the value
            const auto value = std::string(tokens[i].substr(eq_pos + 1));
            // store the entry
            options[key] = value;
        }
    }
} // namespace

PceParameters::PceParameters(bool use_expression, std::vector<std::string> parameters, std::vector<std::string> distribution_types, std::vector<std::string> means, std::vector<std::string> std_deviations, std::vector<std::string> lower_bounds, std::vector<std::string> upper_bounds, std::vector<std::string> alphas, std::vector<std::string> betas, std::map<std::string, std::string> pces_options, std::optional<PrintParameters> print_parameters) :
    use_expression(use_expression), parameters(std::move(parameters)), distribution_types(std::move(distribution_types)), means(std::move(means)), std_deviations(std::move(std_deviations)), lower_bounds(std::move(lower_bounds)), upper_bounds(std::move(upper_bounds)), alphas(std::move(alphas)), betas(std::move(betas)), pces_options(std::move(pces_options)), print_parameters(std::move(print_parameters)) {}

std::optional<PceParameters> PceParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init directive found flag
    bool found = false;
    // init expression-based inputs flag
    bool use_expression = false;
    // init parsed parameter lists
    std::vector<std::string> parameters;
    std::vector<std::string> distribution_types;
    std::vector<std::string> means;
    std::vector<std::string> std_deviations;
    std::vector<std::string> lower_bounds;
    std::vector<std::string> upper_bounds;
    std::vector<std::string> alphas;
    std::vector<std::string> betas;
    // init PCES package entries
    std::map<std::string, std::string> pces_options;
    // init print parameters
    std::optional<PrintParameters> print_parameters;
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
        // check for PCE command
        if (cmd == ".PCE") {
            // set found flag
            found = true;
            // iterate over tokens
            for (size_t i = 1; i < tokens.size(); ++i) {
                const auto& token = tokens[i];
                // skip if no equals sign
                const auto eq_pos = token.find('=');
                if (eq_pos == std::string_view::npos) {
                    // continue iteration
                    continue;
                }
                // split key and value
                const std::string key = to_lower(token.substr(0, eq_pos));
                const auto val = token.substr(eq_pos + 1);
                // check for expression-based inputs flag
                if (key == "useexpr") {
                    // set the flag
                    use_expression = to_lower(val) == "true" || val == "1";
                }
                // check for parameters
                else if (key == "param") {
                    // store the parameter list
                    parameters = split_list(val);
                }
                // check for distribution types
                else if (key == "type") {
                    // store the type list
                    distribution_types = split_list(val);
                }
                // check for means
                else if (key == "means") {
                    // store the means list
                    means = split_list(val);
                }
                // check for standard deviations
                else if (key == "std_deviations") {
                    // store the standard deviation list
                    std_deviations = split_list(val);
                }
                // check for lower bounds
                else if (key == "lower_bounds") {
                    // store the lower bound list
                    lower_bounds = split_list(val);
                }
                // check for upper bounds
                else if (key == "upper_bounds") {
                    // store the upper bound list
                    upper_bounds = split_list(val);
                }
                // check for alpha values
                else if (key == "alpha") {
                    // store the alpha list
                    alphas = split_list(val);
                }
                // check for beta values
                else if (key == "beta") {
                    // store the beta list
                    betas = split_list(val);
                }
            }
        }
        // check for PCES package options
        if (cmd == ".OPTIONS" && tokens.size() > 1 && to_upper(tokens[1]) == "PCES") {
            // parse the package entries
            parse_option_tokens(tokens, 2, pces_options);
        }
        // check for print directive
        if (cmd == ".PRINT" && tokens.size() > 1 && to_upper(tokens[1]) == "PCE") {
            // create print parameters
            print_parameters = PrintParameters::from_xyce_statement(directive);
        }
    }
    // check if directive was found
    if (!found) {
        // return none
        return std::nullopt;
    }
    // return new instance
    return PceParameters(use_expression, std::move(parameters), std::move(distribution_types), std::move(means), std::move(std_deviations), std::move(lower_bounds), std::move(upper_bounds), std::move(alphas), std::move(betas), std::move(pces_options), std::move(print_parameters));
}

std::vector<std::string> PceParameters::to_xyce_directives(const NetlistTopology& topology) const {
    // init line list
    std::vector<std::string> lines;
    // topology reserved for future wildcard expansion; pass-through for now
    (void)topology;
    // build the PCE directive with the arguments present on the instance
    std::string pce_line = ".PCE";
    // append the expression-based inputs flag when set
    if (use_expression) {
        pce_line += " useExpr=true";
    }
    // append the parameter list when present
    if (!parameters.empty()) {
        pce_line += " param=" + join_list(parameters);
    }
    // append the distribution type list when present
    if (!distribution_types.empty()) {
        pce_line += " type=" + join_list(distribution_types);
    }
    // append the means list when present
    if (!means.empty()) {
        pce_line += " means=" + join_list(means);
    }
    // append the standard deviation list when present
    if (!std_deviations.empty()) {
        pce_line += " std_deviations=" + join_list(std_deviations);
    }
    // append the lower bound list when present
    if (!lower_bounds.empty()) {
        pce_line += " lower_bounds=" + join_list(lower_bounds);
    }
    // append the upper bound list when present
    if (!upper_bounds.empty()) {
        pce_line += " upper_bounds=" + join_list(upper_bounds);
    }
    // append the alpha list when present
    if (!alphas.empty()) {
        pce_line += " alpha=" + join_list(alphas);
    }
    // append the beta list when present
    if (!betas.empty()) {
        pce_line += " beta=" + join_list(betas);
    }
    // add the PCE directive line
    lines.push_back(pce_line);
    // build the PCES package directive with the entries present on the instance
    if (!pces_options.empty()) {
        // init the options line
        std::string options_line = ".OPTIONS PCES";
        // append each entry in map order
        for (const auto& [key, value] : pces_options) {
            options_line += " " + key + "=" + value;
        }
        // add the options line
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

std::optional<std::string> PceParameters::validate() const {
    // validation only applies to the explicit parameter lists
    if (use_expression) {
        return std::nullopt;
    }
    // a PCE directive requires at least one parameter
    if (parameters.empty()) {
        return "PCE requires at least one parameter or useExpr=true";
    }
    // distribution types are required for every parameter
    if (distribution_types.size() != parameters.size()) {
        return "PCE type is required for every parameter";
    }
    // count the parameters per distribution type
    size_t uniform_count = 0;
    size_t normal_count = 0;
    size_t gamma_count = 0;
    for (const auto& type : distribution_types) {
        // resolve the distribution type
        const std::string lowered = to_lower(type);
        // count the parameter
        if (lowered == "uniform") {
            ++uniform_count;
        }
        else if (lowered == "normal") {
            ++normal_count;
        }
        else if (lowered == "gamma") {
            ++gamma_count;
        }
        else {
            return "PCE unknown distribution type " + type;
        }
    }
    // the per-type value lists must be aligned with the parameter counts
    if (!lower_bounds.empty() && lower_bounds.size() != uniform_count) {
        return "PCE lower_bounds list length does not match the uniform parameters";
    }
    if (!upper_bounds.empty() && upper_bounds.size() != uniform_count) {
        return "PCE upper_bounds list length does not match the uniform parameters";
    }
    if (!means.empty() && means.size() != normal_count) {
        return "PCE means list length does not match the normal parameters";
    }
    if (!std_deviations.empty() && std_deviations.size() != normal_count) {
        return "PCE std_deviations list length does not match the normal parameters";
    }
    if (!alphas.empty() && alphas.size() != gamma_count) {
        return "PCE alpha list length does not match the gamma parameters";
    }
    if (!betas.empty() && betas.size() != gamma_count) {
        return "PCE beta list length does not match the gamma parameters";
    }
    // per-type positional index for the value lists
    size_t uniform_index = 0;
    size_t normal_index = 0;
    size_t gamma_index = 0;
    // validate each parameter against its distribution type
    for (size_t i = 0; i < parameters.size(); ++i) {
        // resolve the distribution type
        const std::string type = to_lower(distribution_types[i]);
        // build the user-facing parameter prefix for error messages
        const std::string prefix = "PCE parameter " + parameters[i];
        // uniform distributions require lower and upper bounds
        if (type == "uniform") {
            if (uniform_index >= lower_bounds.size() || lower_bounds[uniform_index].empty()) {
                return prefix + ": a uniform distribution requires lower_bounds";
            }
            if (uniform_index >= upper_bounds.size() || upper_bounds[uniform_index].empty()) {
                return prefix + ": a uniform distribution requires upper_bounds";
            }
            ++uniform_index;
        }
        // normal distributions require mean and standard deviation
        else if (type == "normal") {
            if (normal_index >= means.size() || means[normal_index].empty()) {
                return prefix + ": a normal distribution requires means";
            }
            if (normal_index >= std_deviations.size() || std_deviations[normal_index].empty()) {
                return prefix + ": a normal distribution requires std_deviations";
            }
            ++normal_index;
        }
        // gamma distributions require alpha and beta
        else if (type == "gamma") {
            if (gamma_index >= alphas.size() || alphas[gamma_index].empty()) {
                return prefix + ": a gamma distribution requires alpha";
            }
            if (gamma_index >= betas.size() || betas[gamma_index].empty()) {
                return prefix + ": a gamma distribution requires beta";
            }
            ++gamma_index;
        }
    }
    return std::nullopt;
}

bool PceParameters::operator==(const PceParameters& other) const {
    // compare all fields for equality
    return use_expression == other.use_expression && parameters == other.parameters && distribution_types == other.distribution_types && means == other.means && std_deviations == other.std_deviations && lower_bounds == other.lower_bounds && upper_bounds == other.upper_bounds && alphas == other.alphas && betas == other.betas && pces_options == other.pces_options && print_parameters == other.print_parameters;
}

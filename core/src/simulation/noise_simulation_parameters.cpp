#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "core/util.h"
#include "noise_simulation_parameters.h"

DeviceNoiseOperator::DeviceNoiseOperator(std::string type, std::string node, std::string source) :
    type(std::move(type)), node(std::move(node)), source(std::move(source)) {}

bool DeviceNoiseOperator::operator==(const DeviceNoiseOperator& other) const {
    // compare all fields for equality
    return type == other.type && node == other.node && source == other.source;
}

NoiseSimulationParameters::NoiseSimulationParameters(std::string output_node, std::string ref_node, std::string source_name, std::string start_freq_value, std::string end_freq_value, std::string num_points_value, std::string sweep_type, std::vector<DeviceNoiseOperator> device_noise_operators, std::string data_table_name, std::optional<PrintParameters> print_parameters) :
    output_node(std::move(output_node)), ref_node(std::move(ref_node)), source_name(std::move(source_name)), start_freq_value(std::move(start_freq_value)), end_freq_value(std::move(end_freq_value)), num_points_value(std::move(num_points_value)), sweep_type(std::move(sweep_type)), device_noise_operators(std::move(device_noise_operators)), data_table_name(std::move(data_table_name)), print_parameters(std::move(print_parameters)) {}

std::optional<NoiseSimulationParameters> NoiseSimulationParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init defaults
    std::string output_node;
    std::string ref_node;
    std::string source_name;
    std::string start_freq_value;
    std::string end_freq_value;
    std::string num_points_value;
    std::string sweep_type;
    std::vector<DeviceNoiseOperator> device_noise_operators;
    std::string data_table_name;
    std::optional<PrintParameters> print_parameters;

    // flag indicating whether a valid directive was found
    bool found = false;

    // parse directives
    for (const auto& directive : directives) {
        // tokenize the directive
        const auto tokens = tokenize(directive);

        // skip empty directives
        if (tokens.empty()) {
            continue;
        }

        const std::string cmd = to_upper(tokens[0]);

        // parse print directives and retain noise-specific output config
        if (cmd == ".PRINT") {
            // parse the print statement from the directive
            const auto print_statement = PrintParameters::from_xyce_statement(directive);
            // retain noise print parameters when found
            if (print_statement) {
                const std::string print_type_upper = to_upper(print_statement->print_type);
                if (print_type_upper == "NOISE") {
                    // extract device noise operators (DNI/DNO) from the output variables
                    std::vector<std::string> filtered_variables;
                    for (const auto& var : print_statement->output_variables) {
                        const std::string var_upper = to_upper(var);
                        if (var_upper.rfind("DNI(", 0) == 0 || var_upper.rfind("DNO(", 0) == 0) {
                            // parse the operator token DNI(node) or DNI(node,source)
                            const size_t open = var.find('(');
                            const size_t close = var.find(')');
                            if (open != std::string::npos && close != std::string::npos && close > open) {
                                const std::string inner = var.substr(open + 1, close - open - 1);
                                const size_t comma = inner.find(',');
                                std::string node = inner;
                                std::string source;
                                if (comma != std::string::npos) {
                                    node = inner.substr(0, comma);
                                    source = inner.substr(comma + 1);
                                }
                                device_noise_operators.emplace_back(var_upper.substr(0, 3), node, source);
                            }
                        }
                        else {
                            filtered_variables.push_back(var);
                        }
                    }
                    // store the parsed print parameters without the operator tokens
                    PrintParameters filtered_print = *print_statement;
                    filtered_print.output_variables = filtered_variables;
                    print_parameters = filtered_print;
                    continue;
                }
            }
        }

        // skip non-NOISE directives
        if (cmd != ".NOISE") {
            continue;
        }

        // flag indicating a valid NOISE directive was found
        found = true;

        // parse output node (position 1); supports V(node), V(node,ref), or a bare node
        if (tokens.size() >= 2) {
            const std::string_view node_token = tokens[1];
            if (to_upper(node_token).substr(0, 2) == "V(" && node_token.size() >= 3 && node_token.back() == ')') {
                // split inner content on comma
                const std::string inner(node_token.substr(2, node_token.size() - 3));
                const size_t comma = inner.find(',');
                if (comma != std::string::npos) {
                    output_node = inner.substr(0, comma);
                    ref_node = inner.substr(comma + 1);
                }
                else {
                    output_node = inner;
                }
            }
            else {
                // bare node
                output_node = node_token;
            }
        }

        // parse source name (position 2)
        if (tokens.size() >= 3) {
            source_name = tokens[2];
        }

        // parse the sweep type and frequency triple starting at position 3
        size_t idx = 3;
        // check for inline DATA table form (DATA=<table>)
        if (idx < tokens.size() && to_upper(tokens[idx]).substr(0, 5) == "DATA=") {
            sweep_type = "DATA";
            data_table_name = tokens[idx].substr(5);
            idx++;
        }
        // check for a leading sweep type keyword
        else if (idx < tokens.size()) {
            const std::string candidate = to_upper(tokens[idx]);
            if (candidate == "LIN" || candidate == "DEC" || candidate == "OCT" || candidate == "DATA") {
                sweep_type = candidate;
                idx++;
                // read the table name for DATA sweeps
                if (candidate == "DATA" && idx < tokens.size()) {
                    data_table_name = tokens[idx];
                    idx++;
                }
            }
        }
        // parse the frequency triple: number of points, start, end
        if (sweep_type != "DATA") {
            if (idx < tokens.size()) {
                num_points_value = tokens[idx];
                idx++;
            }
            if (idx < tokens.size()) {
                start_freq_value = tokens[idx];
                idx++;
            }
            if (idx < tokens.size()) {
                end_freq_value = tokens[idx];
                idx++;
            }
        }
        // default to a LIN sweep when none is specified
        if (sweep_type.empty()) {
            sweep_type = "LIN";
        }
    }

    // return instance if a valid directive was found
    if (!found) {
        return std::nullopt;
    }

    return NoiseSimulationParameters(output_node, ref_node, source_name, start_freq_value, end_freq_value, num_points_value, sweep_type, device_noise_operators, data_table_name, print_parameters);
}

std::vector<std::string> NoiseSimulationParameters::to_xyce_directives(const NetlistTopology& topology) const {
    (void)topology;
    // init output directive list
    std::vector<std::string> directives;
    // build the noise analysis directive
    std::string noise_directive = ".NOISE V(" + output_node;
    // append the reference node
    if (!ref_node.empty()) {
        noise_directive += "," + ref_node;
    }
    noise_directive += ") " + source_name + " ";
    // append the sweep type and frequency triple
    if (!sweep_type.empty() && sweep_type != "DATA") {
        noise_directive += sweep_type + " " + num_points_value + " " + start_freq_value + " " + end_freq_value;
    }
    else {
        // DATA sweep uses the inline table name
        noise_directive += "DATA=" + data_table_name;
    }
    directives.push_back(noise_directive);

    // append the noise print directive with device noise operators
    if (print_parameters && (!print_parameters->output_variables.empty() || !device_noise_operators.empty())) {
        // build the print directive
        std::string print_directive = print_parameters->to_xyce_statement();
        // append the device noise operators
        for (const auto& dno : device_noise_operators) {
            print_directive += " " + dno.type + "(" + dno.node;
            // append the noise source when set
            if (!dno.source.empty()) {
                print_directive += "," + dno.source;
            }
            print_directive += ")";
        }
        directives.push_back(print_directive);
    }

    // return the full directive list
    return directives;
}

bool NoiseSimulationParameters::operator==(const NoiseSimulationParameters& other) const {
    // compare all fields for equality
    return output_node == other.output_node && ref_node == other.ref_node && source_name == other.source_name && start_freq_value == other.start_freq_value && end_freq_value == other.end_freq_value && num_points_value == other.num_points_value && sweep_type == other.sweep_type && device_noise_operators == other.device_noise_operators && data_table_name == other.data_table_name && print_parameters == other.print_parameters;
}

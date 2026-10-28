#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "ac_simulation_parameters.h"
#include "core/util.h"

AcSimulationParameters::AcSimulationParameters(std::string sweep_mode, std::string points, std::string start, std::string end, std::string data_table_name, std::optional<PrintParameters> print_parameters, std::vector<MeasureEntry> measure_parameters, std::optional<SensParameter> sensitivity) :
    sweep_mode(std::move(sweep_mode)), points(std::move(points)), start(std::move(start)), end(std::move(end)), data_table_name(std::move(data_table_name)), print_parameters(std::move(print_parameters)), measure_parameters(std::move(measure_parameters)), sensitivity(std::move(sensitivity)) {}

std::optional<AcSimulationParameters> AcSimulationParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init defaults
    std::string sweep_mode = "LIN";
    std::string points;
    std::string start;
    std::string end;
    std::string data_table_name;
    std::optional<PrintParameters> print_parameters;
    std::vector<MeasureEntry> measure_parameters;
    std::optional<SensParameter> sensitivity;
    // flag indicating whether a valid directive was found
    bool found = false;
    // parse directives
    for (const auto& directive : directives) {
        // tokenize the directive
        const auto tokens = tokenize(directive);
        // skip empty directives
        if (tokens.empty())
            continue;
        // convert the first token to uppercase for case-insensitive comparison
        const std::string cmd = to_upper(tokens[0]);
        // parse print directives and retain ac-specific output config
        if (cmd == ".PRINT") {
            // parse the print statement from the directive
            const auto print_statement = PrintParameters::from_xyce_statement(directive);
            // retain ac print parameters when found
            if (print_statement) {
                // convert the print type to uppercase for case-insensitive comparison
                const std::string print_type_upper = to_upper(print_statement->print_type);
                // check if the print type is relevant for AC analysis
                if (print_type_upper == "AC" || print_type_upper == "AC_IC") {
                    // store the parsed print parameters
                    print_parameters = *print_statement;
                    // next
                    continue;
                }
            }
        }
        // parse measure directives
        if (cmd == ".MEASURE" || cmd == ".MEAS") {
            // parse the measure statement from the directive
            const auto measure_statement = MeasureEntry::from_xyce_statement(directive);
            // retain measure parameters when found and analysis type matches
            if (measure_statement) {
                // convert the analysis type to uppercase for case-insensitive comparison
                const std::string analysis_type_upper = to_upper(measure_statement->analysis_type);
                // check if the analysis type is relevant for AC analysis
                if (analysis_type_upper == "AC" || analysis_type_upper == "AC_CONT") {
                    // append the parsed measure parameters
                    measure_parameters.push_back(*measure_statement);
                }
            }
            continue;
        }
        // skip non-AC directives
        if (cmd != ".AC")
            continue;
        // flag indicating a valid AC directive was found
        found = true;
        // minimum number of tokens, TODO: validate this with the Xyce reference guide
        if (tokens.size() < 2)
            continue;
        // convert the second token to uppercase for case-insensitive comparison
        const std::string second = to_upper(tokens[1]);
        // handle DATA sweep: .AC DATA=<tablename>
        if (second.substr(0, 5) == "DATA=" && second.find('=') != std::string::npos) {
            // set sweep mode and data table name
            sweep_mode = "DATA";
            data_table_name = tokens[1].substr(5);
            // next
            continue;
        }
        // detect decade or octave log sweep: .AC DEC|OCT <points> <start> <end>
        if (second == "DEC" || second == "OCT") {
            // set sweep mode
            sweep_mode = second;
            // parse points, start, and end if enough tokens are present
            if (tokens.size() >= 5) {
                points = tokens[2];
                start = tokens[3];
                end = tokens[4];
            }
            // next
            continue;
        }
        // linear sweep: .AC [LIN] <points> <start> <end>
        if (second == "LIN") {
            // set sweep mode
            sweep_mode = second;
            // explicit LIN keyword
            if (tokens.size() >= 5) {
                points = tokens[2];
                start = tokens[3];
                end = tokens[4];
            }
        }
        // implicit LIN
        else if (tokens.size() >= 4) {
            sweep_mode = "LIN";
            points = tokens[1];
            start = tokens[2];
            end = tokens[3];
        }
    }
    // parse sensitivity as a companion directive before analysis detection
    sensitivity = SensParameter::from_xyce_directives(directives);
    // return instance if a valid directive was found
    if (!found)
        return std::nullopt;
    // exit with a new instance of AcSimulationParameters
    return AcSimulationParameters(sweep_mode, points, start, end, data_table_name, print_parameters, measure_parameters, sensitivity);
}

std::vector<std::string> AcSimulationParameters::to_xyce_directives(const NetlistTopology& topology) const {
    // init output directive list
    std::vector<std::string> directives;
    // build the core ac directive
    std::string ac_directive = ".AC";
    // append sweep mode and parameters based on the sweep type
    if (sweep_mode == "DATA") {
        // data sweep
        ac_directive += " DATA=" + data_table_name;
    }
    else if (sweep_mode == "DEC" || sweep_mode == "OCT") {
        // decade or octave sweep
        ac_directive += " " + sweep_mode + " " + points + " " + start + " " + end;
    }
    else {
        // lin sweep (explicit)
        ac_directive += " LIN " + points + " " + start + " " + end;
    }
    // append the ac directive to the output list
    directives.push_back(ac_directive);
    // append ac print directive with topology-aware wildcard expansion
    if (print_parameters.has_value())
        directives.push_back(print_parameters->to_xyce_statement());
    // append sensitivity directives when configured
    if (sensitivity.has_value()) {
        // process directives
        const auto sensitivity_directives = sensitivity->to_xyce_directives(topology);
        // append collection
        directives.insert(directives.end(), sensitivity_directives.begin(), sensitivity_directives.end());
    }
    // append measure directives
    for (const auto& measure : measure_parameters)
        directives.push_back(measure.to_xyce_statement());
    // return the full directive list
    return directives;
}

bool AcSimulationParameters::operator==(const AcSimulationParameters& other) const {
    // compare all fields for equality
    return sweep_mode == other.sweep_mode && points == other.points && start == other.start && end == other.end && data_table_name == other.data_table_name && print_parameters == other.print_parameters && measure_parameters == other.measure_parameters && sensitivity == other.sensitivity;
}

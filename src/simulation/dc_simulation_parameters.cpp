#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "../core/util.h"
#include "dc_simulation_parameters.h"
#include "pce_parameters.h"

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

    // validate one sweep tuple; returns an error message or nullopt when valid
    [[nodiscard]] std::optional<std::string> validate_sweep(const std::string& mode, const std::string& variable, const std::string& start, const std::string& stop, const std::string& step, const std::string& points) {
        if (variable.empty()) {
            return "sweep variable is required";
        }
        if (start.empty()) {
            return "start value is required";
        }
        if (stop.empty()) {
            return "stop value is required";
        }
        if (mode == "LIN") {
            if (step.empty()) {
                return "step value is required";
            }
        }
        else {
            if (points.empty()) {
                return "points value is required";
            }
            // a log sweep requires a positive integer number of points per decade/octave
            double points_value = 0.0;
            if (parse_number(points, points_value) && (points_value < 1.0 || std::floor(points_value) != points_value)) {
                return "points value must be a positive integer";
            }
            // a log sweep cannot start at or below zero
            double start_value = 0.0;
            if (parse_number(start, start_value) && start_value <= 0.0) {
                return "start value must be greater than zero";
            }
        }
        return std::nullopt;
    }

    // consume log sweep tuples (DEC/OCT) from tokens starting at a given index;
    // each tuple is: DEC|OCT var start stop points
    void parse_log_sweeps(const std::vector<std::string_view>& tokens, size_t start_index, std::vector<DcSweep>& sweeps) {
        size_t i = start_index;
        while (i + 4 < tokens.size()) {
            const std::string mode = to_upper(tokens[i]);
            if (mode != "DEC" && mode != "OCT")
                break;
            DcSweep sweep;
            sweep.variable = std::string(tokens[i + 1]);
            sweep.start = std::string(tokens[i + 2]);
            sweep.stop = std::string(tokens[i + 3]);
            sweep.points = std::string(tokens[i + 4]);
            sweeps.push_back(std::move(sweep));
            i += 5;
        }
    }

    // consume LIN sweep tuples from tokens starting at a given index;
    // each tuple is: var start stop step  (no leading LIN keyword when implicit)
    void parse_lin_sweeps(const std::vector<std::string_view>& tokens, size_t start_index, std::vector<DcSweep>& sweeps) {
        size_t i = start_index;
        while (i + 3 < tokens.size()) {
            // If the next token looks like a mode keyword (DEC/OCT/LIN/LIST/DATA), stop
            const std::string next_upper = to_upper(tokens[i]);
            if (next_upper == "DEC" || next_upper == "OCT" || next_upper == "LIN" || next_upper == "LIST" || next_upper == "DATA")
                break;
            DcSweep sweep;
            sweep.variable = std::string(tokens[i]);
            sweep.start = std::string(tokens[i + 1]);
            sweep.stop = std::string(tokens[i + 2]);
            sweep.step = std::string(tokens[i + 3]);
            sweeps.push_back(std::move(sweep));
            i += 4;
        }
    }
} // namespace

DCSimulationParameters::DCSimulationParameters(std::string sweep_mode, std::vector<DcSweep> sweeps, std::string data_table_name, std::optional<PrintParameters> print_parameters, std::vector<MeasureEntry> measure_parameters, std::optional<SensParameter> sensitivity, std::optional<PceParameters> pce) :
    sweep_mode(std::move(sweep_mode)), sweeps(std::move(sweeps)), data_table_name(std::move(data_table_name)), print_parameters(std::move(print_parameters)), measure_parameters(std::move(measure_parameters)), sensitivity(std::move(sensitivity)), pce(std::move(pce)) {}

std::optional<DCSimulationParameters> DCSimulationParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init defaults
    std::string sweep_mode = "LIN";
    std::vector<DcSweep> sweeps;
    std::string data_table_name;
    std::optional<PrintParameters> print_parameters;
    std::vector<MeasureEntry> measure_parameters;
    std::optional<SensParameter> sensitivity;
    std::optional<PceParameters> pce;

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

        // parse print directives and retain dc-specific output config
        if (cmd == ".PRINT") {
            // parse the print statement from the directive
            const auto print_statement = PrintParameters::from_xyce_statement(directive);
            // retain dc print parameters when found
            if (print_statement) {
                const std::string print_type_upper = to_upper(print_statement->print_type);
                if (print_type_upper == "DC" || print_type_upper == "HOMOTOPY") {
                    // store the parsed print parameters
                    print_parameters = *print_statement;
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
                const std::string analysis_type_upper = to_upper(measure_statement->analysis_type);
                if (analysis_type_upper == "DC" || analysis_type_upper == "DC_CONT") {
                    // append the parsed measure parameters
                    measure_parameters.push_back(*measure_statement);
                }
            }
            continue;
        }

        // skip non-DC directives
        if (cmd != ".DC") {
            continue;
        }

        // flag indicating a valid DC directive was found
        found = true;

        // a new .DC directive supersedes any previously parsed one: reset sweep
        // state so duplicate directives do not merge into a combined analysis
        sweeps.clear();
        data_table_name.clear();

        // handle DATA sweep: .DC DATA=<tablename>
        if (tokens.size() == 2 && tokens[1].find('=') != std::string::npos && to_upper(tokens[1].substr(0, 5)) == "DATA=") {
            // set sweep mode and data table name
            sweep_mode = "DATA";
            data_table_name = tokens[1].substr(5);
            continue;
        }

        if (tokens.size() < 2) {
            continue;
        }

        const std::string second = to_upper(tokens[1]);
        const std::string third = tokens.size() > 2 ? to_upper(tokens[2]) : "";

        // detect LIST sweep: .DC var LIST val [val ...] [var2 LIST val2 ...]
        if (tokens.size() >= 3 && third == "LIST") {
            sweep_mode = "LIST";
            size_t i = 1;
            while (i < tokens.size()) {
                DcSweep sweep;
                sweep.variable = std::string(tokens[i]);
                i += 2; // skip variable and LIST keyword
                while (i < tokens.size() && !(i + 1 < tokens.size() && to_upper(tokens[i + 1]) == "LIST")) {
                    sweep.list_values.push_back(std::string(tokens[i]));
                    ++i;
                }
                sweeps.push_back(std::move(sweep));
            }
            continue;
        }

        // detect decade or octave log sweep: .DC DEC|OCT var start stop points ...
        if (second == "DEC" || second == "OCT") {
            sweep_mode = second;
            parse_log_sweeps(tokens, 1, sweeps);
            continue;
        }

        // explicit LIN keyword: .DC LIN var start stop step [var2 start2 stop2 step2 ...]
        if (second == "LIN") {
            sweep_mode = "LIN";
            parse_lin_sweeps(tokens, 2, sweeps);
            continue;
        }

        // implicit linear syntax: .DC var start stop step [var2 start2 stop2 step2 ...]
        sweep_mode = "LIN";
        parse_lin_sweeps(tokens, 1, sweeps);
    }

    // parse sensitivity as a companion directive before analysis detection
    sensitivity = SensParameter::from_xyce_directives(directives);

    // parse PCE as a companion directive before analysis detection
    pce = PceParameters::from_xyce_directives(directives);

    // return instance if a valid directive was found
    if (!found) {
        return std::nullopt;
    }

    return DCSimulationParameters(sweep_mode, sweeps, data_table_name, print_parameters, measure_parameters, sensitivity, pce);
}

std::vector<std::string> DCSimulationParameters::to_xyce_directives(const NetlistTopology& topology) const {
    // init output directive list
    std::vector<std::string> directives;
    // build the core dc directive based on the selected sweep mode
    std::string dc_directive = ".DC";

    if (sweep_mode == "DATA") {
        dc_directive += " DATA=" + data_table_name;
    }
    else if (sweep_mode == "LIST") {
        for (size_t i = 0; i < sweeps.size(); ++i) {
            dc_directive += " " + sweeps[i].variable + " LIST";
            for (const auto& val : sweeps[i].list_values) {
                dc_directive += " " + val;
            }
        }
    }
    else {
        // LIN or log sweep — emit each sweep tuple
        for (size_t i = 0; i < sweeps.size(); ++i) {
            const auto& s = sweeps[i];
            if (sweep_mode == "LIN") {
                dc_directive += " " + s.variable + " " + s.start + " " + s.stop + " " + s.step;
            }
            else {
                // DEC or OCT — emit mode keyword before each sweep segment
                dc_directive += " " + sweep_mode + " " + s.variable + " " + s.start + " " + s.stop + " " + s.points;
            }
        }
    }

    directives.push_back(dc_directive);

    // append dc print directive with topology-aware wildcard expansion
    if (print_parameters) {
        directives.push_back(print_parameters->to_xyce_statement());
    }

    // append sensitivity directives when configured
    if (sensitivity) {
        const auto sens_directives = sensitivity->to_xyce_directives(topology);
        directives.insert(directives.end(), sens_directives.begin(), sens_directives.end());
    }

    // append PCE directives when configured
    if (pce) {
        const auto pce_directives = pce->to_xyce_directives(topology);
        directives.insert(directives.end(), pce_directives.begin(), pce_directives.end());
    }

    // append measure directives
    for (const auto& measure : measure_parameters) {
        directives.push_back(measure.to_xyce_statement());
    }

    // return the full directive list
    return directives;
}

bool DCSimulationParameters::operator==(const DCSimulationParameters& other) const {
    // compare all fields for equality
    return sweep_mode == other.sweep_mode && sweeps == other.sweeps && data_table_name == other.data_table_name && print_parameters == other.print_parameters && measure_parameters == other.measure_parameters && sensitivity == other.sensitivity && pce == other.pce;
}

std::optional<std::string> DCSimulationParameters::validate() const {
    // data sweeps require a data table name
    if (sweep_mode == "DATA") {
        if (data_table_name.empty()) {
            return "DC DATA sweep requires a data table name";
        }
        return std::nullopt;
    }
    // list sweeps require at least one sweep variable with values
    if (sweep_mode == "LIST") {
        if (sweeps.empty()) {
            return "DC LIST sweep requires at least one sweep variable";
        }
        for (size_t i = 0; i < sweeps.size(); ++i) {
            if (sweeps[i].variable.empty()) {
                return "DC LIST sweep " + std::to_string(i + 1) + ": sweep variable is required";
            }
            if (sweeps[i].list_values.empty()) {
                return "DC LIST sweep " + std::to_string(i + 1) + ": at least one value is required";
            }
        }
        return std::nullopt;
    }
    // LIN/DEC/OCT sweeps require at least one sweep tuple
    if (sweeps.empty()) {
        return "DC " + sweep_mode + " sweep requires at least one sweep variable";
    }
    // validate each nested sweep
    for (size_t i = 0; i < sweeps.size(); ++i) {
        const auto& s = sweeps[i];
        const std::string prefix = (i == 0) ? "DC " + sweep_mode + " sweep" : "DC " + sweep_mode + " sweep " + std::to_string(i + 1);
        auto error = validate_sweep(sweep_mode, s.variable, s.start, s.stop, s.step, s.points);
        if (error) {
            return prefix + ": " + *error;
        }
    }
    return std::nullopt;
}

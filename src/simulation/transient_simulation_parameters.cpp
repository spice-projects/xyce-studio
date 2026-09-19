#include <cctype>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "../core/util.h"
#include "pce_parameters.h"
#include "transient_simulation_parameters.h"

TransientSchedulePoint::TransientSchedulePoint(std::string time_value, std::string max_time_step_value) :
    time_value(std::move(time_value)), max_time_step_value(std::move(max_time_step_value)) {}

bool TransientSchedulePoint::operator==(const TransientSchedulePoint& other) const {
    // compare all fields for equality
    return time_value == other.time_value && max_time_step_value == other.max_time_step_value;
}

TransientSimulationParameters::TransientSimulationParameters(std::string initial_step_value, std::string final_time_value, std::string start_time_value, std::string step_ceiling_value, std::string op_keyword, std::vector<TransientSchedulePoint> schedule_points, std::optional<PrintParameters> print_parameters, std::vector<FftParameters> fft_parameters, std::vector<FourParameters> four_parameters, std::vector<MeasureEntry> measure_parameters, std::optional<SensParameter> sensitivity, std::optional<PceParameters> pce) :
    initial_step_value(std::move(initial_step_value)), final_time_value(std::move(final_time_value)), start_time_value(std::move(start_time_value)), step_ceiling_value(std::move(step_ceiling_value)), op_keyword(std::move(op_keyword)), schedule_points(std::move(schedule_points)), print_parameters(std::move(print_parameters)), fft_parameters(std::move(fft_parameters)), four_parameters(std::move(four_parameters)), measure_parameters(std::move(measure_parameters)), sensitivity(std::move(sensitivity)), pce(std::move(pce)) {}

std::optional<TransientSimulationParameters> TransientSimulationParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init defaults
    std::string initial_step_value;
    std::string final_time_value;
    std::string start_time_value;
    std::string step_ceiling_value;
    std::string op_keyword;
    std::vector<TransientSchedulePoint> schedule_points;
    std::optional<PrintParameters> print_parameters;
    std::vector<FftParameters> fft_parameters;
    std::vector<FourParameters> four_parameters;
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
        if (tokens.empty())
            continue;
        // extract the command (first token) in uppercase
        const std::string cmd = to_upper(tokens[0]);
        // parse print directives and retain transient-specific output config
        if (cmd == ".PRINT") {
            // parse the print statement from the directive
            const auto print_statement = PrintParameters::from_xyce_statement(directive);
            if (print_statement) {
                // print type
                const std::string print_type_upper = to_upper(print_statement->print_type);
                // retain print parameters only for transient analysis types
                if (print_type_upper == "TRAN" || print_type_upper == "TRANADJOINT") {
                    // store the parsed print parameters
                    print_parameters = *print_statement;
                    // next
                    continue;
                }
            }
        }
        // parse fft directives
        if (cmd == ".FFT") {
            // parse the fft statement from the directive
            const auto fft_statement = FftParameters::from_xyce_statement(directive);
            // retain fft parameters when found
            if (fft_statement) {
                // append the parsed fft parameters
                fft_parameters.push_back(*fft_statement);
            }
            continue;
        }
        // parse four directives
        if (cmd == ".FOUR") {
            // parse the four statement from the directive
            const auto four_statement = FourParameters::from_xyce_statement(directive);
            // retain four parameters when found
            if (four_statement) {
                // append the parsed four parameters
                four_parameters.push_back(*four_statement);
            }
            continue;
        }
        // parse measure directives
        if (cmd == ".MEASURE" || cmd == ".MEAS") {
            // parse the measure statement from the directive
            const auto measure_statement = MeasureEntry::from_xyce_statement(directive);
            if (measure_statement) {
                // type
                const std::string analysis_type_upper = to_upper(measure_statement->analysis_type);
                // retain measure parameters only for transient analysis types
                if (analysis_type_upper == "TRAN" || analysis_type_upper == "TRAN_CONT") {
                    // append the parsed measure parameters
                    measure_parameters.push_back(*measure_statement);
                }
            }
            continue;
        }
        // skip non-TRAN directives
        if (cmd != ".TRAN")
            continue;
        // flag indicating a valid TRAN directive was found
        found = true;
        // extract schedule clause from raw directive if present
        std::string directive_without_schedule = directive;
        std::smatch schedule_match;
        static const std::regex schedule_re(R"(\{schedule\s*\(([^}]*)\)\s*\})", std::regex::icase);
        if (std::regex_search(directive, schedule_match, schedule_re)) {
            // split comma-separated time/step pairs from schedule content
            const std::string schedule_content = schedule_match[1].str();
            // strip each value and filter empties
            std::vector<std::string> schedule_values;
            std::string current_val;
            for (const char c : schedule_content) {
                if (c == ',') {
                    if (!current_val.empty()) {
                        schedule_values.push_back(current_val);
                        current_val.clear();
                    }
                }
                else if (!std::isspace(static_cast<unsigned char>(c))) {
                    current_val += c;
                }
            }
            if (!current_val.empty()) {
                schedule_values.push_back(current_val);
            }

            // pair consecutive entries as (time, max_step)
            for (size_t i = 0; i + 1 < schedule_values.size(); i += 2) {
                schedule_points.emplace_back(schedule_values[i], schedule_values[i + 1]);
            }

            // strip schedule clause so remaining tokens parse cleanly
            directive_without_schedule = directive.substr(0, schedule_match.position());
        }

        // re-tokenize after schedule removal
        const auto tokens_after_schedule = tokenize(directive_without_schedule);

        // parse initial step (required, position 1)
        if (tokens_after_schedule.size() >= 2) {
            initial_step_value = tokens_after_schedule[1];
        }

        // parse final time (required, position 2)
        if (tokens_after_schedule.size() >= 3) {
            final_time_value = tokens_after_schedule[2];
        }

        // separate NOOP/UIC keywords from positional arguments
        std::vector<std::string> positional;
        for (size_t i = 3; i < tokens_after_schedule.size(); ++i) {
            const std::string upper = to_upper(tokens_after_schedule[i]);
            if (upper == "NOOP" || upper == "UIC") {
                // capture op keyword
                op_keyword = upper;
            }
            else {
                // accumulate remaining positional args
                positional.push_back(std::string(tokens_after_schedule[i]));
            }
        }

        // assign optional start time (position 3)
        if (positional.size() >= 1) {
            start_time_value = positional[0];
        }

        // assign optional step ceiling (position 4)
        if (positional.size() >= 2) {
            step_ceiling_value = positional[1];
        }
    }

    // parse sensitivity as a companion directive before analysis detection
    sensitivity = SensParameter::from_xyce_directives(directives);

    // parse PCE as a companion directive before analysis detection
    pce = PceParameters::from_xyce_directives(directives);

    // return instance if a valid directive was found
    if (!found) {
        return std::nullopt;
    }

    return TransientSimulationParameters(initial_step_value, final_time_value, start_time_value, step_ceiling_value, op_keyword, schedule_points, print_parameters, fft_parameters, four_parameters, measure_parameters, sensitivity, pce);
}

std::vector<std::string> TransientSimulationParameters::to_xyce_directives(const NetlistTopology& topology) const {
    // init output directive list
    std::vector<std::string> directives;
    // start with the transient analysis directive
    std::string tran_directive = ".TRAN " + initial_step_value + " " + final_time_value;
    // add start time if specified
    if (!start_time_value.empty())
        tran_directive += " " + start_time_value;
    // add step ceiling if specified
    if (!step_ceiling_value.empty())
        tran_directive += " " + step_ceiling_value;
    // add NOOP/UIC keyword if specified
    if (!op_keyword.empty())
        tran_directive += " " + op_keyword;
    // add schedule points if present
    if (!schedule_points.empty()) {
        // build schedule clause
        std::string schedule_str = " {schedule(";
        // loop through schedule points and append as time,max_step pairs
        for (size_t i = 0; i < schedule_points.size(); ++i) {
            // add comma separator for subsequent points
            if (i > 0)
                schedule_str += ", ";
            // append time and max_step values
            schedule_str += schedule_points[i].time_value + ", " + schedule_points[i].max_time_step_value;
        }
        // close schedule clause
        schedule_str += ")}";
        // append to the transient directive
        tran_directive += schedule_str;
    }
    // append the constructed transient directive to the output list
    directives.push_back(tran_directive);
    // append transient print directive with topology-aware wildcard expansion
    if (print_parameters)
        directives.push_back(print_parameters->to_xyce_statement());
    // append sensitivity directives when configured
    if (sensitivity) {
        // retrieve sensitivity directives
        const auto sens_directives = sensitivity->to_xyce_directives(topology);
        // append to the output list
        directives.insert(directives.end(), sens_directives.begin(), sens_directives.end());
    }
    // append PCE directives when configured
    if (pce) {
        // retrieve PCE directives
        const auto pce_directives = pce->to_xyce_directives(topology);
        // append to the output list
        directives.insert(directives.end(), pce_directives.begin(), pce_directives.end());
    }
    // append fft directives
    for (const auto& fft : fft_parameters)
        directives.push_back(fft.to_xyce_statement());
    // append four directives
    for (const auto& four : four_parameters)
        directives.push_back(four.to_xyce_statement());
    // append measure directives
    for (const auto& measure : measure_parameters)
        directives.push_back(measure.to_xyce_statement());
    // return the full directive list
    return directives;
}

bool TransientSimulationParameters::operator==(const TransientSimulationParameters& other) const {
    // compare all fields for equality
    return initial_step_value == other.initial_step_value && final_time_value == other.final_time_value && start_time_value == other.start_time_value && step_ceiling_value == other.step_ceiling_value && op_keyword == other.op_keyword && schedule_points == other.schedule_points && print_parameters == other.print_parameters && fft_parameters == other.fft_parameters && four_parameters == other.four_parameters && measure_parameters == other.measure_parameters && sensitivity == other.sensitivity && pce == other.pce;
}

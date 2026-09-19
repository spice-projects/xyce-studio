#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../netlist/netlist.h"
#include "fft_parameters.h"
#include "four_parameters.h"
#include "measure_parameters.h"
#include "pce_parameters.h"
#include "print_parameters.h"
#include "sens_parameter.h"

// transient schedule point for transient analysis
struct TransientSchedulePoint
{
    // construct a schedule point from time and max time step
    TransientSchedulePoint(std::string time_value, std::string max_time_step_value);

    // time value
    std::string time_value;
    // maximum time step value
    std::string max_time_step_value;

    // equality operator
    [[nodiscard]] bool operator==(const TransientSchedulePoint& other) const;
};

// transient simulation parameters class — parses and serializes Xyce .TRAN directives
class TransientSimulationParameters
{
public:
    // construct a transient simulation parameters instance from individual fields
    TransientSimulationParameters(std::string initial_step_value, std::string final_time_value, std::string start_time_value, std::string step_ceiling_value, std::string op_keyword, std::vector<TransientSchedulePoint> schedule_points, std::optional<PrintParameters> print_parameters, std::vector<FftParameters> fft_parameters, std::vector<FourParameters> four_parameters, std::vector<MeasureEntry> measure_parameters, std::optional<SensParameter> sensitivity, std::optional<PceParameters> pce);

    // parse all directives into a TransientSimulationParameters instance;
    // returns nullopt when no .TRAN directive is found
    [[nodiscard]] static std::optional<TransientSimulationParameters> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // equality operator
    [[nodiscard]] bool operator==(const TransientSimulationParameters& other) const;

    // initial time step (required)
    std::string initial_step_value;
    // final time (required)
    std::string final_time_value;
    // start time (optional)
    std::string start_time_value;
    // step ceiling (optional)
    std::string step_ceiling_value;
    // NOOP/UIC keyword
    std::string op_keyword;
    // schedule points for variable time stepping
    std::vector<TransientSchedulePoint> schedule_points;
    // optional print parameters
    std::optional<PrintParameters> print_parameters;
    // FFT parameters
    std::vector<FftParameters> fft_parameters;
    // Fourier parameters
    std::vector<FourParameters> four_parameters;
    // measure directives
    std::vector<MeasureEntry> measure_parameters;
    // optional sensitivity parameters
    std::optional<SensParameter> sensitivity;
    // optional PCE (Polynomial Chaos Expansion) parameters
    std::optional<PceParameters> pce;
};

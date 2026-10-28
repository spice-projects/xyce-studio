#pragma once

#include <optional>
#include <string>
#include <vector>

#include "measure_parameters.h"
#include "netlist/netlist.h"
#include "print_parameters.h"
#include "sens_parameter.h"

// AC simulation parameters class — parses and serializes Xyce .AC directives
class AcSimulationParameters
{
public:
    // construct an AC simulation parameters instance from individual fields
    AcSimulationParameters(std::string sweep_mode, std::string points, std::string start, std::string end, std::string data_table_name, std::optional<PrintParameters> print_parameters, std::vector<MeasureEntry> measure_parameters, std::optional<SensParameter> sensitivity);

    // parse all directives into an AcSimulationParameters instance;
    // returns nullopt when no .AC directive is found
    [[nodiscard]] static std::optional<AcSimulationParameters> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // equality operator
    [[nodiscard]] bool operator==(const AcSimulationParameters& other) const;

    // sweep mode: "LIN", "DEC", "OCT", "DATA"
    std::string sweep_mode;
    // number of points (for LIN/DEC/OCT sweeps)
    std::string points;
    // start value of the sweep
    std::string start;
    // stop value of the sweep
    std::string end;
    // data table name (for DATA sweeps)
    std::string data_table_name;
    // optional print parameters
    std::optional<PrintParameters> print_parameters;
    // measure directives
    std::vector<MeasureEntry> measure_parameters;
    // optional sensitivity parameters
    std::optional<SensParameter> sensitivity;
};

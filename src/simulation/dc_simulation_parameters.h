#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../netlist/netlist.h"
#include "measure_parameters.h"
#include "pce_parameters.h"
#include "print_parameters.h"
#include "sens_parameter.h"

// one sweep tuple for a nested DC sweep
struct DcSweep
{
    // variable name
    std::string variable;
    // start value
    std::string start;
    // stop value
    std::string stop;
    // step size (LIN sweep only)
    std::string step;
    // number of points (DEC/OCT sweep only)
    std::string points;
    // explicit list of values (LIST sweep only)
    std::vector<std::string> list_values{};

    // equality operator for vector comparison
    [[nodiscard]] bool operator==(const DcSweep& other) const { return variable == other.variable && start == other.start && stop == other.stop && step == other.step && points == other.points && list_values == other.list_values; }
};

// DC simulation parameters class — parses and serializes Xyce .DC directives
class DCSimulationParameters
{
public:
    // construct a DC simulation parameters instance from individual fields
    DCSimulationParameters(std::string sweep_mode, std::vector<DcSweep> sweeps, std::string data_table_name, std::optional<PrintParameters> print_parameters, std::vector<MeasureEntry> measure_parameters, std::optional<SensParameter> sensitivity, std::optional<PceParameters> pce);

    // parse all directives into a DCSimulationParameters instance;
    // returns nullopt when no .DC directive is found
    [[nodiscard]] static std::optional<DCSimulationParameters> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // validate the sweep parameters against the Xyce reference guide;
    // returns a user-facing error message, or nullopt when the parameters are valid
    [[nodiscard]] std::optional<std::string> validate() const;

    // equality operator
    [[nodiscard]] bool operator==(const DCSimulationParameters& other) const;

    // sweep mode: "LIN", "DEC", "OCT", "LIST", "DATA"
    std::string sweep_mode;
    // nested sweep entries (arbitrary nesting per RG §2.1.3)
    std::vector<DcSweep> sweeps;
    // data table name (DATA sweep only)
    std::string data_table_name;
    // optional print parameters
    std::optional<PrintParameters> print_parameters;
    // measure directives
    std::vector<MeasureEntry> measure_parameters;
    // optional sensitivity parameters
    std::optional<SensParameter> sensitivity;
    // optional PCE (Polynomial Chaos Expansion) parameters
    std::optional<PceParameters> pce;
};

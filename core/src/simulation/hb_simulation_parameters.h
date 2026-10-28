#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "netlist/netlist.h"
#include "print_parameters.h"

// HB simulation parameters class — parses and serializes Xyce .HB directives
class HbSimulationParameters
{
public:
    // construct an HB simulation parameters instance from individual fields
    HbSimulationParameters(std::vector<std::string> frequencies, std::vector<int> harmonics, std::optional<int> tahb, std::optional<std::string> selectharms, std::optional<int> startup_periods, std::optional<PrintParameters> print_parameters, std::map<std::string, std::string> nonlin_options, std::map<std::string, std::string> linsol_options);

    // parse all directives into an HbSimulationParameters instance;
    // returns nullopt when no .HB directive is found
    [[nodiscard]] static std::optional<HbSimulationParameters> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // equality operator
    [[nodiscard]] bool operator==(const HbSimulationParameters& other) const;

    // fundamental frequencies
    std::vector<std::string> frequencies;
    // harmonics (NUMFREQ option)
    std::vector<int> harmonics;
    // transient analysis horizon (TAHB option)
    std::optional<int> tahb;
    // selective harmonic selection (SELECTHARMS option)
    std::optional<std::string> selectharms;
    // startup periods (STARTUPPERIODS option)
    std::optional<int> startup_periods;
    // optional print parameters
    std::optional<PrintParameters> print_parameters;
    // nonlinear solver options (NONLIN-HB package)
    std::map<std::string, std::string> nonlin_options;
    // linear solver options (LINSOL-HB package)
    std::map<std::string, std::string> linsol_options;
};

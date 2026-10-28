#pragma once

#include <optional>
#include <string>
#include <vector>

#include "netlist/netlist.h"
#include "print_parameters.h"

// LIN simulation parameters class — parses and serializes Xyce .LIN directives
class LinSimulationParameters
{
public:
    // construct a LIN simulation parameters instance from individual fields
    LinSimulationParameters(bool sparcalc, std::string format, std::string lintype, std::string dataformat, std::string file, std::string width, std::string precision, std::string sweep_mode, std::string points, std::string start, std::string end, std::string data_table_name, std::optional<PrintParameters> print_parameters);

    // parse all directives into a LinSimulationParameters instance;
    // returns nullopt when no .LIN directive is found
    [[nodiscard]] static std::optional<LinSimulationParameters> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // equality operator
    [[nodiscard]] bool operator==(const LinSimulationParameters& other) const;

    // .LIN keyword arguments
    bool sparcalc;
    std::string format;
    std::string lintype;
    std::string dataformat;
    std::string file;
    std::string width;
    std::string precision;
    // embedded AC sweep fields
    std::string sweep_mode;
    std::string points;
    std::string start;
    std::string end;
    std::string data_table_name;
    // optional print parameters
    std::optional<PrintParameters> print_parameters;
};

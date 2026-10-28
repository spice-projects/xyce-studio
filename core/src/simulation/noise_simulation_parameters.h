#pragma once

#include <optional>
#include <string>
#include <vector>

#include "netlist/netlist.h"
#include "print_parameters.h"

// device noise operator for .NOISE directives
struct DeviceNoiseOperator
{
    // construct a device noise operator from type, node, and source
    DeviceNoiseOperator(std::string type, std::string node, std::string source);

    // operator type (DNI or DNO)
    std::string type;
    // output node
    std::string node;
    // reference node
    std::string source;

    // equality operator
    [[nodiscard]] bool operator==(const DeviceNoiseOperator& other) const;
};

// noise simulation parameters class — parses and serializes Xyce .NOISE directives
class NoiseSimulationParameters
{
public:
    // construct a noise simulation parameters instance from individual fields
    NoiseSimulationParameters(std::string output_node, std::string ref_node, std::string source_name, std::string start_freq_value, std::string end_freq_value, std::string num_points_value, std::string sweep_type, std::vector<DeviceNoiseOperator> device_noise_operators, std::string data_table_name, std::optional<PrintParameters> print_parameters);

    // parse all directives into a NoiseSimulationParameters instance;
    // returns nullopt when no .NOISE directive is found
    [[nodiscard]] static std::optional<NoiseSimulationParameters> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // equality operator
    [[nodiscard]] bool operator==(const NoiseSimulationParameters& other) const;

    // output node (required)
    std::string output_node;
    // reference node (required)
    std::string ref_node;
    // source name (required)
    std::string source_name;
    // start frequency (required)
    std::string start_freq_value;
    // end frequency (required)
    std::string end_freq_value;
    // number of points (required)
    std::string num_points_value;
    // sweep type (LIN, OCT, DEC, DATA)
    std::string sweep_type;
    // device noise operators
    std::vector<DeviceNoiseOperator> device_noise_operators;
    // data table name (DATA sweep only)
    std::string data_table_name;
    // optional print parameters
    std::optional<PrintParameters> print_parameters;
};

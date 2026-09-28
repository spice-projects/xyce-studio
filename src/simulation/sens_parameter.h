#pragma once

#include <optional>
#include <string>
#include <vector>

struct NetlistTopology;

#include "print_parameters.h"

// sensitivity parameter class — parses and serializes Xyce .SENS directives
class SensParameter
{
public:
    // construct a sens parameter instance from individual fields
    SensParameter(std::string analysis_context, std::string objective_mode, std::vector<std::string> objective_values, std::vector<std::string> parameter_list, bool direct, bool adjoint, std::optional<PrintParameters> print_parameters, std::vector<std::string> option_tokens = {});

    // parse .SENS and companion directives into a SensParameter instance;
    // returns nullopt when no .SENS directive is found
    [[nodiscard]] static std::optional<SensParameter> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // equality operator
    [[nodiscard]] bool operator==(const SensParameter& other) const;

    // analysis context for the sensitivity directive
    std::string analysis_context;
    // objective specification mode (e.g. "objfunc", "objvars")
    std::string objective_mode;
    // output objective values
    std::vector<std::string> objective_values;
    // list of device parameters
    std::vector<std::string> parameter_list;
    // direct method flag
    bool direct;
    // adjoint method flag
    bool adjoint;
    // optional print parameters
    std::optional<PrintParameters> print_parameters;
    // verbatim key=value tokens captured from the .OPTIONS SENSITIVITY statement (RG 2.1.25.16)
    std::vector<std::string> option_tokens;
};

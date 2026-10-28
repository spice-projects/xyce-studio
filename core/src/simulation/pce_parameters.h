#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

struct NetlistTopology;

#include "print_parameters.h"

// PCE parameter class — parses and serializes Xyce .PCE directives and the
// companion .OPTIONS PCES package
class PceParameters
{
public:
    // construct a PCE parameter instance from individual fields
    PceParameters(bool use_expression, std::vector<std::string> parameters, std::vector<std::string> distribution_types, std::vector<std::string> means, std::vector<std::string> std_deviations, std::vector<std::string> lower_bounds, std::vector<std::string> upper_bounds, std::vector<std::string> alphas, std::vector<std::string> betas, std::map<std::string, std::string> pces_options, std::optional<PrintParameters> print_parameters);

    // parse .PCE and companion directives into a PceParameters instance;
    // returns nullopt when no .PCE directive is found
    [[nodiscard]] static std::optional<PceParameters> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // validate the parameters against the Xyce reference guide;
    // returns a user-facing error message, or nullopt when the parameters are valid
    [[nodiscard]] std::optional<std::string> validate() const;

    // equality operator
    [[nodiscard]] bool operator==(const PceParameters& other) const;

    // expression-based random inputs flag (useExpr=true ignores the parameter lists)
    bool use_expression;
    // names of the parameters to be sampled
    std::vector<std::string> parameters;
    // distribution type per parameter (uniform, normal or gamma)
    std::vector<std::string> distribution_types;
    // mean per parameter (normal distributions)
    std::vector<std::string> means;
    // standard deviation per parameter (normal distributions)
    std::vector<std::string> std_deviations;
    // lower bound per parameter (uniform distributions; optional for normal)
    std::vector<std::string> lower_bounds;
    // upper bound per parameter (uniform distributions; optional for normal)
    std::vector<std::string> upper_bounds;
    // alpha value per parameter (gamma distributions)
    std::vector<std::string> alphas;
    // beta value per parameter (gamma distributions)
    std::vector<std::string> betas;
    // .OPTIONS PCES package entries (OUTPUTS, COVMATRIX, SEED, ...)
    std::map<std::string, std::string> pces_options;
    // optional .PRINT PCE parameters
    std::optional<PrintParameters> print_parameters;
};

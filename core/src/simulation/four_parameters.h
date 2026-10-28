#pragma once

#include <optional>
#include <string>
#include <vector>

// four parameters class — parses and serializes Xyce .FOUR directives
class FourParameters
{
public:
    // construct a four parameters instance from individual fields
    FourParameters(std::string fundamental_frequency, std::vector<std::string> output_variables);

    // parse a .FOUR directive string into a FourParameters instance;
    // returns nullopt when the string is not a valid .FOUR statement
    [[nodiscard]] static std::optional<FourParameters> from_xyce_statement(const std::string& four_statement);

    // serialize this instance back to a .FOUR directive string
    [[nodiscard]] std::string to_xyce_statement() const;

    // equality operator
    [[nodiscard]] bool operator==(const FourParameters& other) const;

    // fundamental frequency of the Fourier analysis
    std::string fundamental_frequency;
    // ordered list of output variables
    std::vector<std::string> output_variables;
};

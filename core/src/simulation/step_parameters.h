#pragma once

#include <optional>
#include <string>
#include <vector>

// step parameters class — parses and serializes Xyce .STEP directives
class StepParameters
{
public:
    // default constructor — creates a disabled step
    StepParameters();

    // construct a step parameters instance from individual fields
    StepParameters(std::string sweep_mode, std::string variable, std::string start, std::string stop, std::string step, std::string points, std::vector<std::string> list_values, std::string data_table_name, bool enabled);

    // parse a single .STEP directive into a StepParameters instance;
    // returns nullptr (empty optional) when the string is not a valid .STEP
    [[nodiscard]] static std::optional<StepParameters> from_single_directive(const std::string& directive);

    // parse all .STEP directives from the list and return them in order
    [[nodiscard]] static std::vector<StepParameters> all_from_xyce_directives(const std::vector<std::string>& directives);

    // parse the first .STEP directive from the list;
    // returns a disabled default when no .STEP directive is found
    [[nodiscard]] static std::optional<StepParameters> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives() const;

    // validate the step parameters against the Xyce reference guide;
    // returns a user-facing error message, or nullopt when the parameters are valid
    [[nodiscard]] std::optional<std::string> validate() const;

    // equality operator
    [[nodiscard]] bool operator==(const StepParameters& other) const;

    // sweep mode: "LIN", "DEC", "OCT", "LIST", "DATA"
    std::string sweep_mode;
    // swept variable name
    std::string variable;
    // start value of the sweep
    std::string start;
    // stop value of the sweep
    std::string stop;
    // step size (LIN sweep only)
    std::string step;
    // number of points (DEC/OCT sweep only)
    std::string points;
    // explicit list of values (LIST sweep only)
    std::vector<std::string> list_values;
    // data table name (DATA sweep only)
    std::string data_table_name;
    // whether this step directive is active
    bool enabled;
};

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "xyce_output_file.h"

enum class VariableType
{
    FREQUENCY,
    VOLTAGE,
    CURRENT,
    TIME,
    POWER,
    PARAMETER,
    PHASE,
    EXPRESSION,
    UNKNOWN
};

// measurement unit of a variable type; expression, parameter and unknown
// variables carry no declared unit
inline std::string get_variable_unit(const VariableType vt) {
    switch (vt) {
    // frequency case
    case VariableType::FREQUENCY:
        return "Hz";
    // voltage case
    case VariableType::VOLTAGE:
        return "V";
    // current case
    case VariableType::CURRENT:
        return "A";
    // time case
    case VariableType::TIME:
        return "s";
    // power case
    case VariableType::POWER:
        return "W";
    // parameter case
    case VariableType::PARAMETER:
        return "";
    // phase case
    case VariableType::PHASE:
        return "°";
    // expression case
    case VariableType::EXPRESSION:
        return "";
    // unknown case
    case VariableType::UNKNOWN:
    default:
        return "";
    }
}

inline VariableType parse_variable_type(const std::string& type_str) {
    // check frequency
    if (type_str == "frequency") {
        return VariableType::FREQUENCY;
    }
    // check voltage
    if (type_str == "voltage") {
        return VariableType::VOLTAGE;
    }
    // check current
    if (type_str == "current") {
        return VariableType::CURRENT;
    }
    // check time
    if (type_str == "time") {
        return VariableType::TIME;
    }
    // check power
    if (type_str == "power") {
        return VariableType::POWER;
    }
    // check parameter
    if (type_str == "parameter") {
        return VariableType::PARAMETER;
    }
    // check phase
    if (type_str == "phase") {
        return VariableType::PHASE;
    }
    // check expression, expressions printed on .PRINT lines are reported as
    // expression in the raw file header
    if (type_str == "expression") {
        return VariableType::EXPRESSION;
    }
    // unknown
    return VariableType::UNKNOWN;
}

inline VariableType classify_variable_type(const std::string& name, const std::string& type_str) {
    // the DC sweep abscissa is hardcoded as voltage in the raw file header
    if (name == "sweep") {
        return VariableType::UNKNOWN;
    }
    // power variables are reported as unknown in the raw file header
    if (name.size() >= 2 && name[0] == 'P' && name[1] == '(') {
        return VariableType::POWER;
    }
    // fall back to the type reported in the raw file header
    return parse_variable_type(type_str);
}

std::optional<std::shared_ptr<XyceOutputFile>> xyce_raw_file_parser(const std::filesystem::path& filename);

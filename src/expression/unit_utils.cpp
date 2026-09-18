#include <array>
#include <string>
#include <unordered_map>

#include "../core/util.h"
#include "probe_names.h"
#include "unit_utils.h"
#include "xyce_parser.h"

namespace
{
    // built-in identifier to unit mapping (mirrors Python _CONSTANT_UNITS)
    const std::unordered_map<std::string, std::string> CONSTANT_UNITS{
        {"e", ""}, {"f", ""}, {"g", ""}, {"j", ""}, {"k", ""}, {"m", ""}, {"meg", ""}, {"mho", "S"}, {"mil", ""}, {"n", ""}, {"p", ""}, {"pi", ""}, {"s", "s"}, {"t", ""}, {"u", ""},
    };

    // functions that preserve the unit of their argument
    const std::array<std::string, 11> PRESERVING = {"abs", "real", "imag", "mag", "conj", "uramp", "round", "floor", "ceil", "int", "sqr"};

    // resolve an identifier to its unit from context or built-in constants
    std::string unit_for_identifier(const std::string& name, const std::unordered_map<std::string, std::string>& unit_context) {
        const auto key = to_lower(name);
        // check context first
        if (const auto it = unit_context.find(key); it != unit_context.end())
            return it->second;
        // fall back to built-in constants
        if (const auto it = CONSTANT_UNITS.find(key); it != CONSTANT_UNITS.end())
            return it->second;
        // unknown identifiers are dimensionless
        return "";
    }

    // propagate units through binary operations (mirrors Python _propagate_binary_unit)
    std::string propagate_binary_unit(const std::string& left_unit, BinaryOperator op, const std::string& right_unit) {
        // addition and subtraction require matching units
        if (op == BinaryOperator::ADD || op == BinaryOperator::SUB)
            return left_unit == right_unit ? left_unit : "";
        // multiplication
        if (op == BinaryOperator::MUL) {
            // V * A or A * V
            if ((left_unit == "V" && right_unit == "A") || (left_unit == "A" && right_unit == "V"))
                return "W";
            // V * S or S * V
            if ((left_unit == "S" && right_unit == "V") || (left_unit == "V" && right_unit == "S"))
                return "A";
            // scalar * X
            if (left_unit.empty())
                return right_unit;
            // X * scalar
            return left_unit;
        }
        // division
        if (op == BinaryOperator::DIV) {
            // X / X
            if (left_unit == right_unit && !left_unit.empty())
                return "";
            // V / A
            if (left_unit == "V" && right_unit == "A")
                return "\u03A9"; // omega
            // A / V
            if (left_unit == "A" && right_unit == "V")
                return "S";
            // X / scalar
            if (right_unit.empty())
                return left_unit;
            // scalar / X
            if (left_unit.empty()) {
                // scalar / S -> omega
                if (right_unit == "S")
                    return "\u03A9";
                // scalar / omega -> siemens
                if (right_unit == "\u03A9")
                    return "S";
                // scalar / s -> Hz
                if (right_unit == "s")
                    return "Hz";
                // scalar / Hz -> s
                if (right_unit == "Hz")
                    return "s";
            }
            return "";
        }
        // all other operators are dimensionless
        return "";
    }

    // infer unit for unary function calls (mirrors Python _function_unit)
    std::string function_unit(const std::string& name, const std::string& arg_unit) {
        // handle special cases for certain functions
        const auto key = to_lower(name);
        // db composes the decibel prefix with the argument unit; dimensionless
        // arguments (ratios like gain or loss) keep plain dB
        if (key == "db")
            return arg_unit.empty() ? "dB" : "dB" + arg_unit;
        // angle aliases return degrees
        if (key == "angle" || key == "ph" || key == "phase")
            return "\u00B0";
        // these functions preserve their argument unit
        for (const auto& fn : PRESERVING) {
            // if the function name matches, return the argument unit
            if (key == fn)
                return arg_unit;
        }
        return "";
    }

    // infer unit for multi-argument function calls
    std::string function_unit_multi(const std::string& name, const std::string& first_arg_unit) {
        // lowercase
        const auto key = to_lower(name);
        // min, max, limit preserve the first argument unit
        if (key == "min" || key == "max" || key == "limit")
            return first_arg_unit;
        // all other multi-argument functions are dimensionless
        return "";
    }

    // reconstruct the canonical probe key from a function call AST
    std::string probe_key(const FunctionCallNode& probe) {
        auto arg_text = [](const ExpressionNode& arg) -> std::string {
            if (const auto* id = dynamic_cast<const IdentifierNode*>(&arg)) {
                return id->name;
            }
            if (const auto* num = dynamic_cast<const NumberNode*>(&arg)) {
                return num->text;
            }
            return "";
        };
        std::string result = probe.name;
        result += "(";
        for (size_t i = 0; i < probe.args.size(); ++i) {
            if (i > 0)
                result += ", ";
            result += arg_text(*probe.args[i]);
        }
        result += ")";
        return result;
    }

    // infer unit for probe function calls (mirrors Python _unit_for_probe)
    std::string unit_for_probe(const FunctionCallNode& probe, const std::unordered_map<std::string, std::string>& unit_context) {
        const auto probe_key_str = to_lower(probe_key(probe));
        // check for a directly stored probe unit
        if (const auto it = unit_context.find(probe_key_str); it != unit_context.end()) {
            if (!it->second.empty())
                return it->second;
        }
        const auto name = to_lower(probe.name);
        // voltage probe
        if (name == "v")
            return "V";
        // current probe
        if (name == "i" || name == "id")
            return "A";
        // network parameter probes
        if (is_network_parameter_probe_name(name)) {
            if (name[0] == 'z')
                return "\u03A9"; // impedance -> omega
            if (name[0] == 'y')
                return "S"; // admittance -> siemens
            // S and H parameters are dimensionless
        }
        return "";
    }
} // namespace

std::string infer_unit(const ExpressionNode& node, const std::unordered_map<std::string, std::string>& unit_context) {
    // numeric literals are dimensionless
    if (dynamic_cast<const NumberNode*>(&node)) {
        return "";
    }
    // resolve identifier unit from context or built-in constants
    if (const auto* id = dynamic_cast<const IdentifierNode*>(&node)) {
        return unit_for_identifier(id->name, unit_context);
    }
    // unary operators preserve the operand unit
    if (const auto* unary = dynamic_cast<const UnaryOperationNode*>(&node)) {
        return infer_unit(*unary->operand, unit_context);
    }
    // binary operators combine units
    if (const auto* binary = dynamic_cast<const BinaryOperationNode*>(&node)) {
        const auto left_unit = infer_unit(*binary->left, unit_context);
        const auto right_unit = infer_unit(*binary->right, unit_context);
        return propagate_binary_unit(left_unit, binary->operator_value, right_unit);
    }
    // function calls
    if (const auto* func = dynamic_cast<const FunctionCallNode*>(&node)) {
        const auto name = to_lower(func->name);
        // probe calls
        if (name == "v" || name == "i" || name == "id") {
            return unit_for_probe(*func, unit_context);
        }
        if (is_network_parameter_probe_name(name)) {
            // only resolve if the probe key exists in the unit context
            const auto key = to_lower(probe_key(*func));
            if (unit_context.find(key) != unit_context.end()) {
                return unit_for_probe(*func, unit_context);
            }
        }
        // nullary functions are dimensionless
        if (func->args.empty())
            return "";
        // infer first argument unit
        const auto first_arg_unit = infer_unit(*func->args[0], unit_context);
        // unary function calls
        if (func->args.size() == 1) {
            return function_unit(name, first_arg_unit);
        }
        // multi-argument function calls
        return function_unit_multi(name, first_arg_unit);
    }
    // ternary: units must match on both branches
    if (const auto* ternary = dynamic_cast<const TernaryOperationNode*>(&node)) {
        const auto true_unit = infer_unit(*ternary->if_true, unit_context);
        const auto false_unit = infer_unit(*ternary->if_false, unit_context);
        return true_unit == false_unit ? true_unit : "";
    }
    // step selector: preserves the base expression unit
    if (const auto* step = dynamic_cast<const StepSelectorNode*>(&node)) {
        return infer_unit(*step->base, unit_context);
    }
    return "";
}

bool has_step_selector(const ExpressionNode& node) {
    if (dynamic_cast<const StepSelectorNode*>(&node)) {
        return true;
    }
    if (const auto* unary = dynamic_cast<const UnaryOperationNode*>(&node)) {
        return has_step_selector(*unary->operand);
    }
    if (const auto* binary = dynamic_cast<const BinaryOperationNode*>(&node)) {
        return has_step_selector(*binary->left) || has_step_selector(*binary->right);
    }
    if (const auto* ternary = dynamic_cast<const TernaryOperationNode*>(&node)) {
        return has_step_selector(*ternary->condition) || has_step_selector(*ternary->if_true) || has_step_selector(*ternary->if_false);
    }
    if (const auto* func = dynamic_cast<const FunctionCallNode*>(&node)) {
        for (const auto& arg : func->args) {
            if (has_step_selector(*arg))
                return true;
        }
    }
    return false;
}

std::string format_expression(const ExpressionNode& node) {
    if (const auto* num = dynamic_cast<const NumberNode*>(&node)) {
        return num->text;
    }
    if (const auto* id = dynamic_cast<const IdentifierNode*>(&node)) {
        return id->name;
    }
    if (const auto* unary = dynamic_cast<const UnaryOperationNode*>(&node)) {
        std::string op;
        switch (unary->operator_value) {
        case UnaryOperator::POS:
            op = "+";
            break;
        case UnaryOperator::NEG:
            op = "-";
            break;
        case UnaryOperator::NOT:
            op = "~";
            break;
        }
        return op + format_expression(*unary->operand);
    }
    if (const auto* binary = dynamic_cast<const BinaryOperationNode*>(&node)) {
        std::string op;
        switch (binary->operator_value) {
        case BinaryOperator::ADD:
            op = "+";
            break;
        case BinaryOperator::SUB:
            op = "-";
            break;
        case BinaryOperator::MUL:
            op = "*";
            break;
        case BinaryOperator::DIV:
            op = "/";
            break;
        case BinaryOperator::MOD:
            op = "%";
            break;
        case BinaryOperator::POW:
            op = "**";
            break;
        case BinaryOperator::LOGICAL_AND:
            op = "&";
            break;
        case BinaryOperator::LOGICAL_OR:
            op = "|";
            break;
        case BinaryOperator::LOGICAL_XOR:
            op = "^";
            break;
        case BinaryOperator::EQUAL:
            op = "==";
            break;
        case BinaryOperator::NOT_EQUAL:
            op = "!=";
            break;
        case BinaryOperator::LESS:
            op = "<";
            break;
        case BinaryOperator::LESS_EQUAL:
            op = "<=";
            break;
        case BinaryOperator::GREATER:
            op = ">";
            break;
        case BinaryOperator::GREATER_EQUAL:
            op = ">=";
            break;
        }
        return "(" + format_expression(*binary->left) + op + format_expression(*binary->right) + ")";
    }
    if (const auto* func = dynamic_cast<const FunctionCallNode*>(&node)) {
        std::string result = func->name;
        result += "(";
        for (size_t i = 0; i < func->args.size(); ++i) {
            if (i > 0)
                result += ",";
            result += format_expression(*func->args[i]);
        }
        result += ")";
        return result;
    }
    if (const auto* ternary = dynamic_cast<const TernaryOperationNode*>(&node)) {
        return "(" + format_expression(*ternary->condition) + "?" + format_expression(*ternary->if_true) + ":" + format_expression(*ternary->if_false) + ")";
    }
    if (const auto* step = dynamic_cast<const StepSelectorNode*>(&node)) {
        return format_expression(*step->base) + "@" + std::to_string(step->step_index);
    }
    return "";
}

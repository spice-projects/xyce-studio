#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

#ifdef _MSC_VER
#pragma warning(disable : 4702)
#endif

#include <spdlog/spdlog.h>

#include "../core/util.h"
#include "../core/view.h"
#include "probe_names.h"
#include "xyce_evaluator.h"
#include "xyce_value.h"

namespace
{
    struct Context
    {
        std::unordered_map<std::string, XyceValue>* expressions;
        const std::function<std::optional<XyceValue>(const std::string&)>& loader;
        const std::unordered_map<std::string, const FunctionDefinitionNode*>& functions;
        const std::unordered_map<std::string, const XyceValue*>& constants;
        const std::vector<std::pair<size_t, size_t>>& step_slices;

        const std::unordered_map<std::string, XyceValue>* local_variables = nullptr;
        const Context* parent_var_scope = nullptr;

        std::string_view current_function_name{};
        const Context* parent_call_frame = nullptr;

        [[nodiscard]] std::optional<XyceValue> find_expression(const std::string& key) const {
            // already in memory
            if (const auto it = expressions->find(key); it != expressions->end()) {
                // return a copy of the cached value
                return it->second;
            }
            // not in memory, delegate to the loader
            if (auto value = loader(key); value) {
                // cache the loaded value in the context
                expressions->emplace(key, *value);
                // return the loaded value
                return value;
            }
            // unknown expression
            return std::nullopt;
        }

        [[nodiscard]] bool has_expression(const std::string& key) const {
            // check whether the expression is available
            return find_expression(key).has_value();
        }

        [[nodiscard]] const XyceValue* find_variable(std::string_view key) const {
            // check local variables first
            if (local_variables) {
                // find in local map
                if (const auto it = local_variables->find(std::string(key)); it != local_variables->end()) {
                    // return pointer to value
                    return &it->second;
                }
            }
            // fallback to parent scope if present
            return parent_var_scope ? parent_var_scope->find_variable(key) : nullptr;
        }

        [[nodiscard]] bool has_variable(std::string_view key) const {
            // return whether variable was found
            return find_variable(key) != nullptr;
        }

        [[nodiscard]] bool has_function_in_stack(std::string_view func_name) const {
            // check if current frame matches function name
            if (current_function_name == func_name) {
                // recursion detected
                return true;
            }
            // check parent call frame
            return parent_call_frame ? parent_call_frame->has_function_in_stack(func_name) : false;
        }

        [[nodiscard]] Context with_function_call(std::string_view func_name, const std::unordered_map<std::string, XyceValue>& args) const {
            // create copy of current context
            Context child = *this;
            // update local variables pointer
            child.local_variables = &args;
            // set parent variable scope
            child.parent_var_scope = this;
            // set current function name
            child.current_function_name = func_name;
            // set parent call frame
            child.parent_call_frame = this;
            // exit
            return child;
        }
    };

    template <typename Fn>
    XyceValue broadcast_binary_real(const XyceValue& left, const XyceValue& right, Fn&& fn) {
        // find left & right operand type
        bool left_is_scalar = is_scalar(left);
        bool right_is_scalar = is_scalar(right);
        // check both operands are scalars
        if (left_is_scalar && right_is_scalar) {
            // apply the function and return the result
            return fn(scalar_value<double>(left), scalar_value<double>(right));
        }
        // check both are vectors
        if (!left_is_scalar && !right_is_scalar) {
            // vectors
            auto left_vector = to_real_vector(left);
            auto right_vector = to_real_vector(right);
            // check sizes match
            if (left_vector->size() != right_vector->size())
                throw std::invalid_argument("Vector sizes do not match for binary operation");
            // result
            std::vector<double> out;
            // reserve space for the result
            out.reserve(left_vector->size());
            // loop vectors
            for (size_t i = 0; i < left_vector->size(); ++i)
                out.push_back(fn((*left_vector)[i], (*right_vector)[i]));
            // exit
            return std::make_shared<View<double>>(std::move(out));
        }
        // check left is scalar and right is vector
        if (left_is_scalar) {
            // operands
            auto left_scalar = scalar_value<double>(left);
            auto right_vector = to_real_vector(right);
            // result
            std::vector<double> out;
            // reserve space for the result
            out.reserve(right_vector->size());
            // loop vector
            for (size_t i = 0; i < right_vector->size(); ++i)
                out.push_back(fn(left_scalar, (*right_vector)[i]));
            // exit
            return std::make_shared<View<double>>(std::move(out));
        }
        // operands
        auto left_vector = to_real_vector(left);
        auto right_scalar = scalar_value<double>(right);
        // result
        std::vector<double> out;
        // reserve space for the result
        out.reserve(left_vector->size());
        // loop vector
        for (size_t i = 0; i < left_vector->size(); ++i)
            out.push_back(fn((*left_vector)[i], right_scalar));
        // exit
        return std::make_shared<View<double>>(std::move(out));
    }

    template <typename Fn>
    XyceValue broadcast_binary_complex(const XyceValue& left, const XyceValue& right, Fn&& fn) {
        // find left & right operand type
        bool left_is_scalar = !is_vector(left);
        bool right_is_scalar = !is_vector(right);
        // check both operands are scalars
        if (left_is_scalar && right_is_scalar) {
            // apply the function and return the result
            return fn(scalar_value<std::complex<double>>(left), scalar_value<std::complex<double>>(right));
        }
        // check both are vectors
        if (!left_is_scalar && !right_is_scalar) {
            // vectors
            auto left_vector = to_complex_vector(left);
            auto right_vector = to_complex_vector(right);
            // check sizes match
            if (left_vector->size() != right_vector->size())
                throw std::invalid_argument("Vector sizes do not match for binary operation");
            // result
            std::vector<std::complex<double>> out;
            // reserve space for the result
            out.reserve(left_vector->size());
            // loop vectors
            for (size_t i = 0; i < left_vector->size(); ++i)
                out.push_back(fn((*left_vector)[i], (*right_vector)[i]));
            // exit
            return std::make_shared<View<std::complex<double>>>(std::move(out));
        }
        // check left is scalar and right is vector
        if (left_is_scalar) {
            // operands
            auto left_scalar = scalar_value<std::complex<double>>(left);
            auto right_vector = to_complex_vector(right);
            // result
            std::vector<std::complex<double>> out;
            // reserve space for the result
            out.reserve(right_vector->size());
            // loop vector
            for (size_t i = 0; i < right_vector->size(); ++i)
                out.push_back(fn(left_scalar, (*right_vector)[i]));
            // exit
            return std::make_shared<View<std::complex<double>>>(std::move(out));
        }
        // operands
        auto left_vector = to_complex_vector(left);
        auto right_scalar = scalar_value<std::complex<double>>(right);
        // result
        std::vector<std::complex<double>> out;
        // reserve space for the result
        out.reserve(left_vector->size());
        // loop vector
        for (size_t i = 0; i < left_vector->size(); ++i)
            out.push_back(fn((*left_vector)[i], right_scalar));
        // exit
        return std::make_shared<View<std::complex<double>>>(std::move(out));
    }

    template <typename Fn>
    XyceValue broadcast_unary_real(const XyceValue& value, Fn&& fn) {
        // check if the value is a scalar and apply the function directly
        if (is_scalar(value)) {
            // apply the function and return the result
            return fn(scalar_value<double>(value));
        }
        // it is a vector
        auto operand = to_real_vector(value);
        // result
        std::vector<double> out;
        // reserve space for the result
        out.reserve(operand->size());
        // loop vectors
        for (size_t i = 0; i < operand->size(); ++i)
            out.push_back(fn(operand->operator[](i)));
        // exit
        return std::make_shared<View<double>>(std::move(out));
    }

    template <typename Fn>
    XyceValue broadcast_unary_complex(const XyceValue& value, Fn&& fn) {
        // check if the value is a scalar and apply the function directly
        if (!is_vector(value)) {
            // apply the function and return the result
            return fn(scalar_value<std::complex<double>>(value));
        }
        // it is a vector
        auto operand = to_complex_vector(value);
        // result
        std::vector<std::complex<double>> out;
        // reserve space for the result
        out.reserve(operand->size());
        // loop vectors
        for (size_t i = 0; i < operand->size(); ++i)
            out.push_back(fn(operand->operator[](i)));
        // exit
        return std::make_shared<View<std::complex<double>>>(std::move(out));
    }

    XyceValue evaluate(const ExpressionNode& expression, const Context& context);

    XyceValue evaluate_unary(const UnaryOperationNode& expression, const Context& context) {
        // evaluate the operand first
        auto value = evaluate(*expression.operand, context);
        // apply unary plus
        if (expression.operator_value == UnaryOperator::POS) {
            // return value directly
            return value;
        }
        // apply unary minus
        if (expression.operator_value == UnaryOperator::NEG) {
            // processor
            auto l = []<typename T0>(T0&& arg) {
                // actual parameter type
                using TX = std::decay_t<T0>;
                // double
                if constexpr (std::is_same_v<TX, double>) {
                    // apply -
                    return XyceValue{-arg};
                }
                // complex
                if constexpr (std::is_same_v<TX, std::complex<double>>) {
                    // apply -
                    return XyceValue{-arg};
                }
                // View<double>
                if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
                    // vector
                    std::vector<double> out;
                    // allocate space
                    out.reserve(arg->size());
                    // append mapped values
                    std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto& v) { return -v; });
                    // exit
                    return XyceValue{std::make_shared<View<double>>(std::move(out))};
                }
                // View<complex>
                if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                    // vector
                    std::vector<std::complex<double>> out;
                    // allocate space
                    out.reserve(arg->size());
                    // append mapped values
                    std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto& v) { return -v; });
                    // exit
                    return XyceValue{std::make_shared<View<std::complex<double>>>(std::move(out))};
                }
                // not possible value type
                throw std::invalid_argument("unsupported type");
            };
            // visit and return
            return std::visit(l, value);
        }
        // apply logical not
        if (expression.operator_value == UnaryOperator::NOT) {
            // processor
            auto l = []<typename T0>(T0&& arg) {
                // actual parameter type
                using TX = std::decay_t<T0>;
                // double
                if constexpr (std::is_same_v<TX, double>) {
                    // apply not
                    return XyceValue{arg != 0.0 ? 0.0 : 1.0};
                }
                // complex
                if constexpr (std::is_same_v<TX, std::complex<double>>) {
                    // apply not
                    return XyceValue{arg.real() != 0.0 ? 0.0 : 1.0};
                }
                // View<double>
                if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
                    // vector
                    std::vector<double> out;
                    // allocate space
                    out.reserve(arg->size());
                    // append mapped values
                    std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto& v) { return v != 0.0 ? 0.0 : 1.0; });
                    // exit
                    return XyceValue{std::make_shared<View<double>>(std::move(out))};
                }
                // View<complex>
                if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                    // vector
                    std::vector<double> out;
                    // allocate space
                    out.reserve(arg->size());
                    // append mapped values
                    std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto& v) { return v.real() != 0.0 ? 0.0 : 1.0; });
                    // exit
                    return XyceValue{std::make_shared<View<double>>(std::move(out))};
                }
                // not possible value type
                throw std::invalid_argument("unsupported type");
            };
            // visit and return
            return std::visit(l, value);
        }
        // unsupported operator
        throw std::invalid_argument("Unsupported unary operator");
    }

    XyceValue evaluate_binary(const BinaryOperationNode& expression, const Context& context) {
        // short-circuit logical and
        if (expression.operator_value == BinaryOperator::LOGICAL_AND) {
            // eveluate left, extract type
            const auto left_value = evaluate(*expression.left, context);
            const auto left_is_scalar = is_scalar(left_value);
            // shortcut if left is scalar and false
            if (left_is_scalar && scalar_value<double>(left_value) == 0.0)
                return 0.0;
            // evaluate right, extract type
            const auto right_value = evaluate(*expression.right, context);
            const auto right_is_scalar = is_scalar(right_value);
            // shortcut if right is scalar and false
            if (right_is_scalar && scalar_value<double>(right_value) == 0.0)
                return 0.0;
            // easiest case: both are scalars, return 1.0 since both are non-zero
            if (left_is_scalar && right_is_scalar)
                return 1.0;
            // at least one value is a vector
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return (lhs != 0.0 && rhs != 0.0) ? 1.0 : 0.0; });
        }
        // short-circuit logical or
        if (expression.operator_value == BinaryOperator::LOGICAL_OR) {
            // eveluate left, extract type
            const auto left_value = evaluate(*expression.left, context);
            const auto left_is_scalar = is_scalar(left_value);
            // shortcut if left is scalar and true
            if (left_is_scalar && scalar_value<double>(left_value) != 0.0)
                return 1.0;
            // evaluate right, extract type
            const auto right_value = evaluate(*expression.right, context);
            const auto right_is_scalar = is_scalar(right_value);
            // shortcut if right is scalar and true
            if (right_is_scalar && scalar_value<double>(right_value) != 0.0)
                return 1.0;
            // easiest case: both are scalars, return 0.0 since both are zero
            if (left_is_scalar && right_is_scalar)
                return 0.0;
            // at least one value is a vector
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return (lhs != 0.0 || rhs != 0.0) ? 1.0 : 0.0; });
        }
        // evaluate both operands for the remaining operators
        const auto left_value = evaluate(*expression.left, context);
        const auto right_value = evaluate(*expression.right, context);
        // +
        if (expression.operator_value == BinaryOperator::ADD) {
            // check at least one operand is complex or a complex vector
            if (is_complex(left_value) || is_complex(right_value))
                return broadcast_binary_complex(left_value, right_value, [](std::complex<double> lhs, std::complex<double> rhs) { return lhs + rhs; });
            // it is safe to assume both operands are real-valued
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs + rhs; });
        }
        // -
        if (expression.operator_value == BinaryOperator::SUB) {
            // check at least one operand is complex or a complex vector
            if (is_complex(left_value) || is_complex(right_value))
                return broadcast_binary_complex(left_value, right_value, [](std::complex<double> lhs, std::complex<double> rhs) { return lhs - rhs; });
            // it is safe to assume both operands are real-valued
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs - rhs; });
        }
        // *
        if (expression.operator_value == BinaryOperator::MUL) {
            // check at least one operand is complex or a complex vector
            if (is_complex(left_value) || is_complex(right_value))
                return broadcast_binary_complex(left_value, right_value, [](std::complex<double> lhs, std::complex<double> rhs) { return lhs * rhs; });
            // it is safe to assume both operands are real-valued
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs * rhs; });
        }
        // /
        if (expression.operator_value == BinaryOperator::DIV) {
            // check at least one operand is complex or a complex vector
            if (is_complex(left_value) || is_complex(right_value))
                return broadcast_binary_complex(left_value, right_value, [](std::complex<double> lhs, std::complex<double> rhs) { return lhs / rhs; });
            // it is safe to assume both operands are real-valued
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs / rhs; });
        }
        // pow(x, y)
        if (expression.operator_value == BinaryOperator::POW) {
            // check at least one operand is complex or a complex vector
            if (is_complex(left_value) || is_complex(right_value))
                return broadcast_binary_complex(left_value, right_value, [](std::complex<double> lhs, std::complex<double> rhs) { return std::pow(lhs, rhs); });
            // it is safe to assume both operands are real-valued
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return std::pow(lhs, rhs); });
        }
        // x % y
        if (expression.operator_value == BinaryOperator::MOD) {
            // perform operation on real values only
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs - std::floor(lhs / rhs) * rhs; });
        }
        // x == y
        if (expression.operator_value == BinaryOperator::EQUAL) {
            // check at least one operand is complex or a complex vector
            if (is_complex(left_value) || is_complex(right_value))
                return broadcast_binary_complex(left_value, right_value, [](std::complex<double> lhs, std::complex<double> rhs) { return lhs == rhs; });
            // it is safe to assume both operands are real-valued
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs == rhs; });
        }
        // x != y
        if (expression.operator_value == BinaryOperator::NOT_EQUAL) {
            // check at least one operand is complex or a complex vector
            if (is_complex(left_value) || is_complex(right_value))
                return broadcast_binary_complex(left_value, right_value, [](std::complex<double> lhs, std::complex<double> rhs) { return lhs != rhs; });
            // it is safe to assume both operands are real-valued
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs != rhs; });
        }
        // <
        if (expression.operator_value == BinaryOperator::LESS) {
            // check log level
            if (spdlog::get_level() <= spdlog::level::warn) {
                // check at least one operand is complex or a complex vector
                if (is_complex(left_value) || is_complex(right_value))
                    spdlog::warn("Evaluating binary operator '<' with complex operands");
            }
            // xyce evaluates `<, <=, >, >=` using the real part of complex numbers
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs < rhs; });
        }
        // <=
        if (expression.operator_value == BinaryOperator::LESS_EQUAL) {
            // check log level
            if (spdlog::get_level() <= spdlog::level::warn) {
                // check at least one operand is complex or a complex vector
                if (is_complex(left_value) || is_complex(right_value))
                    spdlog::warn("Evaluating binary operator '<=' with complex operands");
            }
            // xyce evaluates `<, <=, >, >=` using the real part of complex numbers
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs <= rhs; });
        }
        // >
        if (expression.operator_value == BinaryOperator::GREATER) {
            // check log level
            if (spdlog::get_level() <= spdlog::level::warn) {
                // check at least one operand is complex or a complex vector
                if (is_complex(left_value) || is_complex(right_value))
                    spdlog::warn("Evaluating binary operator '>' with complex operands");
            }
            // xyce evaluates `<, <=, >, >=` using the real part of complex numbers
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs > rhs; });
        }
        // >=
        if (expression.operator_value == BinaryOperator::GREATER_EQUAL) {
            // check log level
            if (spdlog::get_level() <= spdlog::level::warn) {
                // check at least one operand is complex or a complex vector
                if (is_complex(left_value) || is_complex(right_value))
                    spdlog::warn("Evaluating binary operator '>=' with complex operands");
            }
            // xyce evaluates `<, <=, >, >=` using the real part of complex numbers
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return lhs >= rhs; });
        }
        // x ^ y
        if (expression.operator_value == BinaryOperator::LOGICAL_XOR) {
            // apply on the real part of complex numbers, and return 1.0 for true and 0.0 for false
            return broadcast_binary_real(left_value, right_value, [](double lhs, double rhs) { return ((lhs != 0.0) ^ (rhs != 0.0)) ? 1.0 : 0.0; });
        }
        // unsupported operator
        throw std::invalid_argument("Unsupported binary operator");
    }

    XyceValue evaluate_ternary(const TernaryOperationNode& expression, const Context& context) {
        // evaluate condition
        const auto condition = evaluate(*expression.condition, context);
        // branch for scalar condition (short-circuit non-taken branch)
        if (is_scalar(condition)) {
            // the condition selects a branch, it carries no complex part of its own
            require_real_argument("?:", condition);
            // check condition and evaluate corresponding branch
            return scalar_value<double>(condition) != 0.0 ? evaluate(*expression.if_true, context) : evaluate(*expression.if_false, context);
        }
        // evaluate both branches for vector condition
        const auto if_true = evaluate(*expression.if_true, context);
        const auto if_false = evaluate(*expression.if_false, context);
        // elementwise conditional selection using builtin if
        return BUILTIN_FUNCTIONS.at("if")({condition, if_true, if_false});
    }

    std::string node_name_from_expr(const ExpressionNode& expression) {
        // cast to identifier node
        if (const auto* node = dynamic_cast<const IdentifierNode*>(&expression); node != nullptr) {
            // return identifier name
            return node->name;
        }
        // cast to number node
        if (const auto* node = dynamic_cast<const NumberNode*>(&expression); node != nullptr) {
            // return number text
            return node->text;
        }
        // throw exception
        throw std::invalid_argument("Cannot extract node name from complex expression");
    }

    std::string reconstruct_probe_name(const FunctionCallNode& expression) {
        // reconstruct probe string
        std::string result = expression.name;
        // append closing parenthesis
        result += ")";
        // Note: original reconstruct_probe_name implementation reconstructed from args, keeping exact style
        result = expression.name;
        // append opening parenthesis
        result += "(";
        // loop arguments
        for (size_t index = 0; index < expression.args.size(); ++index) {
            // append separator if needed
            if (index > 0) {
                // append comma and space
                result += ", ";
            }
            // append node name
            result += node_name_from_expr(*expression.args[index]);
        }
        // append closing parenthesis
        result += ")";
        // return reconstructed probe name
        return result;
    }

    std::string extract_node_name(const ExpressionNode& expression) { return node_name_from_expr(expression); }

    std::optional<XyceValue> resolve_probe_value(const std::string& key, const Context& context) {
        // check variables first
        if (const auto* val = context.find_variable(key); val != nullptr) {
            // return variable value
            return *val;
        }
        // check expressions, loading them on demand
        return context.find_expression(key);
    }

    XyceValue evaluate_probe(const FunctionCallNode& expression, const Context& context, std::string probe_key = {}) {
        // reconstruct probe name if not provided
        if (probe_key.empty())
            probe_key = to_lower(reconstruct_probe_name(expression));
        // check variables and expressions, loading expressions on demand
        if (const auto value = resolve_probe_value(probe_key, context); value) {
            // return probe value
            return *value;
        }
        // check differential probe with 2 arguments
        if (expression.args.size() == 2) {
            // node names
            const auto node_a_name = extract_node_name(*expression.args[0]);
            const auto node_b_name = extract_node_name(*expression.args[1]);
            // check node b is ground
            if (to_lower(node_b_name) == "0") {
                // probe key
                const auto probe_a_key = to_lower("V(" + node_a_name + ")");
                // check variables and expressions, loading expressions on demand
                if (const auto value = resolve_probe_value(probe_a_key, context); value) {
                    // return probe value
                    return *value;
                }
            }
            // check node a is ground
            if (to_lower(node_a_name) == "0") {
                // probe key
                const auto probe_b_key = to_lower("V(" + node_b_name + ")");
                // resolve the probe value
                const auto value = resolve_probe_value(probe_b_key, context);
                // negate probe value if found
                if (value) {
                    // check complex
                    if (is_complex(*value))
                        return broadcast_unary_complex(*value, [](std::complex<double> value) { return -value; });
                    // real negation
                    return broadcast_unary_real(*value, [](double value) { return -value; });
                }
            }
            // differential probe keys
            const auto probe_a_key = to_lower("V(" + node_a_name + ")");
            const auto probe_b_key = to_lower("V(" + node_b_name + ")");
            // resolve both probe values
            const auto value_a = resolve_probe_value(probe_a_key, context);
            const auto value_b = resolve_probe_value(probe_b_key, context);
            // calculate difference if available
            if (value_a) {
                // check value b
                if (value_b) {
                    // complex subtract
                    if (is_complex(*value_a) || is_complex(*value_b))
                        return broadcast_binary_complex(*value_a, *value_b, [](std::complex<double> lhs, std::complex<double> rhs) { return lhs - rhs; });
                    // real subtract
                    return broadcast_binary_real(*value_a, *value_b, [](double lhs, double rhs) { return lhs - rhs; });
                }
                // single reference value
                return *value_a;
            }
        }
        // throw exception
        throw std::invalid_argument("Unknown probe: " + (probe_key.empty() ? reconstruct_probe_name(expression) : probe_key));
    }

    bool has_simple_probe_args(const FunctionCallNode& expression) {
        // loop arguments
        for (const auto& arg : expression.args) {
            // check node type
            if (!dynamic_cast<const IdentifierNode*>(arg.get()) && !dynamic_cast<const NumberNode*>(arg.get())) {
                // complex argument found
                return false;
            }
        }
        // all arguments are simple
        return true;
    }

    std::optional<std::string> probe_call_key(const FunctionCallNode& expression, const Context& context) {
        // lowercase function name
        const auto name = to_lower(expression.name);
        // canonical SPICE probe families: v, i, id
        if ((name == "v" || name == "i" || name == "id") && !expression.args.empty())
            return to_lower(reconstruct_probe_name(expression));
        // network parameter probes: Sxy, Zxy, Yxy, Hxy — only when the key exists in context
        if (is_network_parameter_probe_name(name) && !expression.args.empty()) {
            // network parameter probes must have simple arguments to be unambiguous
            if (has_simple_probe_args(expression)) {
                // reconstruct the probe name and check if it exists in the context
                auto key = to_lower(reconstruct_probe_name(expression));
                // check if the probe name exists in context variables or expressions
                if (context.has_variable(key) || context.has_expression(key))
                    return key;
            }
        }
        // not a probe call
        return std::nullopt;
    }

    XyceValue evaluate_function_call(const FunctionCallNode& expression, const Context& context) {
        // probe calls are handled specially
        if (auto probe_key = probe_call_key(expression, context)) {
            // evaluate probe
            return evaluate_probe(expression, context, std::move(*probe_key));
        }
        // resolve a builtin function
        const auto builtin_key = to_lower(expression.name);
        // find in builtin map
        if (const auto it = BUILTIN_FUNCTIONS.find(builtin_key); it != BUILTIN_FUNCTIONS.end()) {
            // arguments vector
            std::vector<XyceValue> args;
            // reserve space
            args.reserve(expression.args.size());
            // evaluate arguments
            for (const auto& arg : expression.args) {
                // append evaluated argument
                args.push_back(evaluate(*arg, context));
            }
            // execute builtin function
            return it->second(args);
        }
        // resolve a user function
        const auto function_key = to_lower(expression.name);
        // find in functions map
        const auto it = context.functions.find(function_key);
        // check function exists
        if (it == context.functions.end()) {
            // throw unknown function
            throw std::invalid_argument("Unknown function: " + expression.name);
        }
        // check recursion
        if (context.has_function_in_stack(function_key)) {
            // throw recursive function error
            throw std::invalid_argument("Recursive function call detected: " + expression.name);
        }
        // function definition reference
        const auto& definition = *it->second;
        // check argument count matches parameter count
        if (expression.args.size() != definition.params.size()) {
            // throw argument count error
            throw std::invalid_argument("Function '" + expression.name + "' expects " + std::to_string(definition.params.size()) + " arguments, got " + std::to_string(expression.args.size()));
        }
        // argument map for local variables scope
        std::unordered_map<std::string, XyceValue> arg_map;
        // reserve space for parameters
        arg_map.reserve(definition.params.size());
        // evaluate arguments and bind to parameter names
        for (size_t index = 0; index < definition.params.size(); ++index) {
            // bind parameter value
            arg_map[to_lower(definition.params[index])] = evaluate(*expression.args[index], context);
        }
        // create child context with function call frame and local variables
        const auto next_context = context.with_function_call(function_key, arg_map);
        // evaluate function body in child context
        return evaluate(*definition.body, next_context);
    }

    XyceValue evaluate_step_selector(const StepSelectorNode& expression, const Context& context) {
        // steps from context
        if (expression.step_index < 1 || expression.step_index > context.step_slices.size())
            throw std::invalid_argument("Step selector @" + std::to_string(expression.step_index) + " is out of range: file has " + std::to_string(context.step_slices.size()) + " step(s)");
        // evaluate the base expression
        const auto base_value = evaluate(*expression.base, context);
        // slice @ steo index
        const auto [begin, end] = context.step_slices.at(expression.step_index - 1);
        // processor
        auto l = [&begin, &end]<typename T0>(T0& arg) -> XyceValue {
            // actual parameter type
            using TX = std::decay_t<T0>;
            // View<double>
            if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
                // return slice on original argument to avoid copying data
                return std::make_shared<View<double>>(arg->data() + static_cast<std::ptrdiff_t>(begin), end - begin, 1, arg);
            }
            // view<complex>
            else if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                // return slice on original argument to avoid copying data
                return std::make_shared<View<std::complex<double>>>(arg->data() + static_cast<std::ptrdiff_t>(begin), end - begin, 1, arg);
            }
            // return argument when scalar
            return {arg};
        };
        // visit base value
        return std::visit(l, base_value);
    }

    XyceValue lookup_name(const std::string& name, const Context& context) {
        // lowercase the name for case-insensitive lookup
        const auto key = to_lower(name);
        // find in context variables
        if (const auto* val = context.find_variable(key); val != nullptr)
            return *val;
        // find in context expressions, loading them on demand
        if (const auto value = context.find_expression(key); value)
            return *value;
        // find in context constants
        if (const auto it = context.constants.find(key); it != context.constants.end())
            return *it->second;
        // not found
        throw std::invalid_argument("Unknown identifier: " + name);
    }

    XyceValue evaluate(const ExpressionNode& expression, const Context& context) {
        // literal number
        if (const auto* node = dynamic_cast<const NumberNode*>(&expression); node != nullptr)
            return std::stod(node->text);
        // literal string
        if (const auto* node = dynamic_cast<const IdentifierNode*>(&expression); node != nullptr)
            return lookup_name(node->name, context);
        // unary expression
        if (const auto* node = dynamic_cast<const UnaryOperationNode*>(&expression); node != nullptr)
            return evaluate_unary(*node, context);
        // binary expression
        if (const auto* node = dynamic_cast<const BinaryOperationNode*>(&expression); node != nullptr)
            return evaluate_binary(*node, context);
        // ternary expression
        if (const auto* node = dynamic_cast<const TernaryOperationNode*>(&expression); node != nullptr)
            return evaluate_ternary(*node, context);
        // function call expression
        if (const auto* node = dynamic_cast<const FunctionCallNode*>(&expression); node != nullptr)
            return evaluate_function_call(*node, context);
        // step selector expression
        if (const auto* node = dynamic_cast<const StepSelectorNode*>(&expression); node != nullptr)
            return evaluate_step_selector(*node, context);
        // unsupported expression node
        throw std::invalid_argument("Unsupported expression node");
    }
} // namespace

XyceValue evaluate_expression(const ExpressionNode& expression, std::unordered_map<std::string, XyceValue>& expressions, const std::function<std::optional<XyceValue>(const std::string&)>& loader, const std::unordered_map<std::string, FunctionDefinitionNode>& functions, const std::unordered_map<std::string, XyceValue>& constants, const std::vector<std::pair<size_t, size_t>>& step_slices) {
    // build evaluation function lookup map
    std::unordered_map<std::string, const FunctionDefinitionNode*> func_map;
    // loop functions and store pointers
    for (const auto& [name, value] : functions)
        func_map.insert_or_assign(to_lower(name), &value);
    // build evaluation constants lookup map
    std::unordered_map<std::string, const XyceValue*> const_map;
    // loop builtin constants
    for (const auto& [name, value] : BUILTIN_CONSTANTS)
        const_map[to_lower(name)] = &value;
    // loop user constants
    for (const auto& [name, value] : constants)
        const_map[to_lower(name)] = &value;
    // build initial evaluation context
    Context context{
        .expressions = &expressions,
        .loader = loader,
        .functions = func_map,
        .constants = const_map,
        .step_slices = step_slices,
    };
    // evaluate the tree
    return evaluate(expression, context);
}

XyceValue evaluate_expression(const ExpressionNode& expression, const std::unordered_map<std::string, XyceValue>& expressions, const std::unordered_map<std::string, FunctionDefinitionNode>& functions, const std::unordered_map<std::string, XyceValue>& constants, const std::vector<std::pair<size_t, size_t>>& step_slices) {
    // copy expressions into a mutable map with lowercase keys
    std::unordered_map<std::string, XyceValue> mutable_expressions;
    // reserve space for all expressions
    mutable_expressions.reserve(expressions.size());
    // loop expressions, lowercase keys
    for (const auto& [name, value] : expressions)
        mutable_expressions.emplace(to_lower(name), value);
    // no loader for pre-populated contexts
    auto loader = [](const std::string&) -> std::optional<XyceValue> { return std::nullopt; };
    // evaluate using the lazy-capable implementation
    return evaluate_expression(expression, mutable_expressions, loader, functions, constants, step_slices);
}

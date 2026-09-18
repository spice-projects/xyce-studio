#include <algorithm>
#include <deque>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>

#ifdef _MSC_VER
#pragma warning(disable : 4702)
#endif

#include "../core/util.h"
#include "expression_manager.h"
#include "unit_utils.h"
#include "xyce_evaluator.h"

ExpressionManager::ExpressionManager(std::vector<AnyExpression>& expressions, std::vector<std::pair<size_t, size_t>>& step_slices) :
    m_expressions(std::make_move_iterator(expressions.begin()), std::make_move_iterator(expressions.end())), m_step_slices(std::move(step_slices)) {
    // ensure step are non empty
    if (m_step_slices.empty())
        throw std::invalid_argument("step slices cannot be empty");
    // reserve capacity for expression map
    m_context.reserve(m_expressions.size());
    // index each expression by its lowercased name for fast lookup
    for (size_t idx = 0; idx < m_expressions.size(); ++idx)
        std::visit([this, &idx](auto&& expression) { this->m_context[to_lower(expression.name())] = idx; }, m_expressions[idx]);
}

Expression<double>& ExpressionManager::abscissa() { return std::get<Expression<double>>(m_expressions[0]); }

std::vector<AnyExpression*> ExpressionManager::expressions() {
    // result
    std::vector<AnyExpression*> result;
    // reserve space for all expressions
    result.reserve(m_expressions.size());
    // transform each expression into a pointer to the expression
    std::ranges::transform(m_expressions.begin(), m_expressions.end(), std::back_inserter(result), [](auto& expression) { return &expression; });
    // exit
    return result;
}

std::vector<std::string> ExpressionManager::expression_names() const {
    // result
    std::vector<std::string> names;
    // reserve space for all expression names
    names.reserve(m_expressions.size());
    // transform each expression into its name
    for (const auto& expression : m_expressions)
        names.push_back(std::visit([](auto&& exp) { return exp.name(); }, expression));
    // exit
    return names;
}

const std::vector<std::pair<size_t, size_t>>& ExpressionManager::step_slices() const { return m_step_slices; }

AnyExpression* ExpressionManager::evaluate(const std::string& expression, const std::optional<std::string>& name) {
    try {
        // determine the lookup key
        const std::string key = to_lower(name.value_or(expression));
        // return the existing expression if already evaluated
        if (const auto it = m_context.find(key); it != m_context.end())
            return &m_expressions.at(it->second);
        // parse the expression string into an AST
        auto ast = m_parser.parse_expression(expression);
        // lazy loader materializes an expression the first time it is referenced
        auto loader = [this](const std::string& key) -> std::optional<XyceValue> {
            // find the expression in the context
            if (const auto it = m_context.find(key); it != m_context.end()) {
                // materialize and return the expression value
                return from_expression(m_expressions.at(it->second));
            }
            // unknown expression
            return std::nullopt;
        };
        // evaluate the parsed expression, materializing expressions on demand
        auto evaluated = evaluate_expression(*ast, m_expression_data, loader, {}, {}, m_step_slices);
        // rematerialize the result if step slices require tiling
        evaluated = rematerialize(evaluated, *ast);
        // unix context
        std::unordered_map<std::string, std::string> unit_context;
        // reserve space for all expressions
        unit_context.reserve(m_expressions.size());
        // loop expressions
        for (auto& expr : m_expressions) {
            // append to map
            std::visit([&unit_context](auto&& e) { unit_context[to_lower(e.name())] = e.unit(); }, expr);
        }
        // infer the unit of the result expression
        auto inferred_unit = ::infer_unit(*ast, unit_context);
        // derive the result name
        auto result_name = name.value_or(expression);
        // create expression
        auto* result = build_expression(evaluated, result_name, inferred_unit);
        // update context
        m_context[key] = m_expressions.size() - 1;
        // exit
        return result;
    }
    catch (const std::exception&) {
        // return nullptr on any error
        return nullptr;
    }
}

std::string ExpressionManager::infer_unit(const std::string& expression) {
    // determine the lookup key
    const std::string key = to_lower(expression);
    // return unit from an already evaluated expression
    if (const auto it = m_context.find(key); it != m_context.end()) {
        // get the expression
        const auto& any_expr = m_expressions.at(it->second);
        // return the unit of the expression
        return std::visit([](auto&& expr) { return expr.unit(); }, any_expr);
    }
    try {
        // parse and infer unit from the expression AST
        auto ast = m_parser.parse_expression(expression);
        // unit context
        std::unordered_map<std::string, std::string> unit_context;
        // reserve space for all expressions
        unit_context.reserve(m_expressions.size());
        // loop expressions, append unit to context
        for (const auto& any_expr : m_expressions)
            std::visit([&unit_context](auto&& expr) { unit_context[to_lower(expr.name())] = expr.unit(); }, any_expr);
        // use unit inference implementation
        return ::infer_unit(*ast, unit_context);
    }
    catch (const std::exception&) {
        // unknown
        return "";
    }
}

AnyExpression* ExpressionManager::build_expression(XyceValue& value, const std::string& name, const std::string& unit) {
    // factory
    auto l = [this, &name, &unit]<typename T0>(T0&& arg) -> AnyExpression* {
        // actual parameter type
        using TX = std::decay_t<T0>;
        // double
        if constexpr (std::is_same_v<TX, double>) {
            // calculate total vector size across all steps
            size_t total_points = 0;
            for (const auto& [start, end] : m_step_slices)
                total_points += (end - start);
            // create vector to hold the scalar value repeated for each step
            std::vector<double> data(total_points, arg);
            // create expression, append it to expressions
            m_expressions.emplace_back(Expression<double>{name, std::move(data), m_step_slices, unit, "expression manager"});
            // return expression
            return &m_expressions.back();
        }
        // complex
        if constexpr (std::is_same_v<TX, std::complex<double>>) {
            // calculate total vector size across all steps
            size_t total_points = 0;
            for (const auto& [start, end] : m_step_slices)
                total_points += (end - start);
            // create vector to hold the scalar value repeated for each step
            std::vector<std::complex<double>> data(total_points, arg);
            // create expression, append it to expressions
            m_expressions.emplace_back(Expression<std::complex<double>>{name, std::move(data), m_step_slices, unit, "expression manager"});
            // return expression
            return &m_expressions.back();
        }
        // View<double>
        if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
            // create expression, append it to expressions
            m_expressions.emplace_back(Expression<double>{name, std::move(*arg), m_step_slices, unit, "expression manager"});
            // return expression
            return &m_expressions.back();
        }
        // vector<complex>
        else if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
            // create expression, append it to expressions
            m_expressions.emplace_back(Expression<std::complex<double>>{name, std::move(*arg), m_step_slices, unit, "expression manager"});
            // return expression
            return &m_expressions.back();
        }
        // not possible value type
        throw std::invalid_argument("unsupported type");
    };
    // convert value to expression
    return std::visit(l, value);
}

XyceValue ExpressionManager::rematerialize(XyceValue& value, const ExpressionNode& ast) {
    // no steps in data
    if (m_step_slices.size() == 1)
        return value;
    // scalar values never need tiling
    if (is_scalar(value))
        return value;
    // no step selector in the AST means no tiling is needed
    if (!has_step_selector(ast))
        return value;
    // compute the total number of points across all steps
    size_t total_points = 0;
    for (const auto& [start, end] : m_step_slices)
        total_points += (end - start);
    // determine the current vector size
    size_t current_size = std::holds_alternative<std::shared_ptr<View<double>>>(value) ? std::get<std::shared_ptr<View<double>>>(value)->size() : std::get<std::shared_ptr<View<std::complex<double>>>>(value)->size();
    // already the right size
    if (current_size == total_points)
        return value;
    // the value must match a single step length to be tileable
    const auto step_length = m_step_slices[0].second - m_step_slices[0].first;
    if (current_size != step_length)
        return value;
    // tile the single-step data across all steps
    if (std::holds_alternative<std::shared_ptr<View<double>>>(value)) {
        // view
        auto& view = std::get<std::shared_ptr<View<double>>>(value);
        // data
        std::vector<double> tiled;
        // reserve space for all steps
        tiled.reserve(view->size() * m_step_slices.size());
        // loop steps, append the same vector for each step
        for (size_t i = 0; i < m_step_slices.size(); ++i)
            tiled.insert(tiled.end(), view->begin(), view->end());
        // exit
        return std::make_shared<View<double>>(std::move(tiled));
    }
    // view
    auto& view = std::get<std::shared_ptr<View<std::complex<double>>>>(value);
    // data
    std::vector<std::complex<double>> tiled;
    // reserve space for all steps
    tiled.reserve(view->size() * m_step_slices.size());
    // loop steps, append the same vector for each step
    for (size_t i = 0; i < m_step_slices.size(); ++i)
        tiled.insert(tiled.end(), view->begin(), view->end());
    // exit
    return std::make_shared<View<std::complex<double>>>(std::move(tiled));
}

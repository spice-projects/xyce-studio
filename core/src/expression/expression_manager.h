#pragma once

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "expression.h"
#include "xyce_parser.h"
#include "xyce_value.h"

class ExpressionManager
{
public:
    ExpressionManager() = default;

    ExpressionManager(const ExpressionManager&) = delete;

    ExpressionManager(ExpressionManager&&) noexcept = default;

    ExpressionManager(std::vector<AnyExpression>& expressions, std::vector<std::pair<size_t, size_t>>& step_slices);

    ~ExpressionManager() = default;

    ExpressionManager& operator=(const ExpressionManager&) = delete;

    ExpressionManager& operator=(ExpressionManager&&) noexcept = default;

    [[nodiscard]] Expression<double>& abscissa();

    [[nodiscard]] std::vector<AnyExpression*> expressions();

    [[nodiscard]] std::vector<std::string> expression_names() const;

    [[nodiscard]] const std::vector<std::pair<size_t, size_t>>& step_slices() const;

    [[nodiscard]] AnyExpression* evaluate(const std::string& expression, const std::optional<std::string>& name = std::nullopt);

    [[nodiscard]] std::string infer_unit(const std::string& expression);

private:
    std::deque<AnyExpression> m_expressions;
    std::vector<std::pair<size_t, size_t>> m_step_slices;
    std::unordered_map<std::string, size_t> m_context;
    std::unordered_map<std::string, XyceValue> m_expression_data;
    XyceParser m_parser;

    AnyExpression* build_expression(XyceValue& value, const std::string& name, const std::string& unit);

    XyceValue rematerialize(XyceValue& value, const ExpressionNode& ast);
};

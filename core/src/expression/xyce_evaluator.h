#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "xyce_parser.h"
#include "xyce_value.h"

using BuiltinCallable = std::function<XyceValue(const std::vector<XyceValue>&)>;

struct EvaluationContext
{
    std::unordered_map<std::string, const XyceValue*> expressions;
    std::unordered_map<std::string, XyceValue> variables;
    std::unordered_map<std::string, const FunctionDefinitionNode*> functions;
    std::unordered_map<std::string, const XyceValue*> constants;
    std::vector<std::pair<size_t, size_t>> step_slices;
};

XyceValue evaluate_expression(const ExpressionNode& expression, std::unordered_map<std::string, XyceValue>& expressions, const std::function<std::optional<XyceValue>(const std::string&)>& loader, const std::unordered_map<std::string, FunctionDefinitionNode>& functions, const std::unordered_map<std::string, XyceValue>& constants, const std::vector<std::pair<size_t, size_t>>& step_slices);

XyceValue evaluate_expression(const ExpressionNode& expression, const std::unordered_map<std::string, XyceValue>& expressions = {}, const std::unordered_map<std::string, FunctionDefinitionNode>& functions = {}, const std::unordered_map<std::string, XyceValue>& constants = {}, const std::vector<std::pair<size_t, size_t>>& step_slices = {});

extern const std::unordered_map<std::string, double> NUMBER_SUFFIXES;
extern const std::unordered_map<std::string, BuiltinCallable> BUILTIN_FUNCTIONS;
extern const std::unordered_map<std::string, XyceValue> BUILTIN_CONSTANTS;

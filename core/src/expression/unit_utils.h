#pragma once

#include <string>
#include <unordered_map>

#include "xyce_parser.h"

// infer the propagated unit of an expression by walking the AST
std::string infer_unit(const ExpressionNode& node, const std::unordered_map<std::string, std::string>& unit_context);

// check whether the AST contains at least one StepSelectorNode
bool has_step_selector(const ExpressionNode& node);

// format an expression AST back to a canonical string with explicit grouping
std::string format_expression(const ExpressionNode& node);

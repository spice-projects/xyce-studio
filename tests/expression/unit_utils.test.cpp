#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "expression/unit_utils.h"
#include "expression/xyce_parser.h"

namespace
{
    auto make_identifier(const std::string& name) { return std::make_unique<IdentifierNode>(name); }

    auto make_number(const std::string& text) { return std::make_unique<NumberNode>(text); }

    struct UnknownNode final : ExpressionNode
    {
    };

    template <typename... Args>
    auto make_call(const std::string& name, Args&&... args) {
        std::vector<ExpressionPtr> argv;
        (argv.push_back(std::forward<Args>(args)), ...);
        return std::make_unique<FunctionCallNode>(name, std::move(argv));
    }

    auto make_unary(UnaryOperator op, ExpressionPtr arg) { return std::make_unique<UnaryOperationNode>(op, std::move(arg)); }

    auto make_binary(ExpressionPtr lhs, BinaryOperator op, ExpressionPtr rhs) { return std::make_unique<BinaryOperationNode>(std::move(lhs), op, std::move(rhs)); }

    auto make_ternary(ExpressionPtr cond, ExpressionPtr t, ExpressionPtr f) { return std::make_unique<TernaryOperationNode>(std::move(cond), std::move(t), std::move(f)); }

    auto make_step(ExpressionPtr base, size_t index) { return std::make_unique<StepSelectorNode>(std::move(base), index); }
} // namespace

// ========================================================================================
// format_expression
// ========================================================================================

TEST(FormatExpressionChecks, number_node) {
    // arrange / act
    auto result = format_expression(*make_number("3.14"));
    // assert
    ASSERT_EQ(result, "3.14");
}

TEST(FormatExpressionChecks, identifier_node) {
    // arrange / act
    auto result = format_expression(*make_identifier("v_out"));
    // assert
    ASSERT_EQ(result, "v_out");
}

TEST(FormatExpressionChecks, unary_pos) {
    // arrange / act
    auto result = format_expression(*make_unary(UnaryOperator::POS, make_identifier("x")));
    // assert
    ASSERT_EQ(result, "+x");
}

TEST(FormatExpressionChecks, unary_neg) {
    // arrange / act
    auto result = format_expression(*make_unary(UnaryOperator::NEG, make_identifier("x")));
    // assert
    ASSERT_EQ(result, "-x");
}

TEST(FormatExpressionChecks, unary_not) {
    // arrange / act
    auto result = format_expression(*make_unary(UnaryOperator::NOT, make_identifier("x")));
    // assert
    ASSERT_EQ(result, "~x");
}

TEST(FormatExpressionChecks, binary_add) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::ADD, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a+b)");
}

TEST(FormatExpressionChecks, binary_sub) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::SUB, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a-b)");
}

TEST(FormatExpressionChecks, binary_mul) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::MUL, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a*b)");
}

TEST(FormatExpressionChecks, binary_div) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::DIV, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a/b)");
}

TEST(FormatExpressionChecks, binary_mod) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::MOD, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a%b)");
}

TEST(FormatExpressionChecks, binary_pow) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::POW, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a**b)");
}

TEST(FormatExpressionChecks, binary_logical_and) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::LOGICAL_AND, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a&b)");
}

TEST(FormatExpressionChecks, binary_logical_or) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::LOGICAL_OR, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a|b)");
}

TEST(FormatExpressionChecks, binary_logical_xor) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::LOGICAL_XOR, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a^b)");
}

TEST(FormatExpressionChecks, binary_equal) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::EQUAL, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a==b)");
}

TEST(FormatExpressionChecks, binary_not_equal) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::NOT_EQUAL, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a!=b)");
}

TEST(FormatExpressionChecks, binary_less) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::LESS, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a<b)");
}

TEST(FormatExpressionChecks, binary_less_equal) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::LESS_EQUAL, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a<=b)");
}

TEST(FormatExpressionChecks, binary_greater) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::GREATER, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a>b)");
}

TEST(FormatExpressionChecks, binary_greater_equal) {
    // arrange / act
    auto result = format_expression(*make_binary(make_identifier("a"), BinaryOperator::GREATER_EQUAL, make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(a>=b)");
}

TEST(FormatExpressionChecks, function_call_no_args) {
    // arrange / act
    auto result = format_expression(*make_call("time"));
    // assert
    ASSERT_EQ(result, "time()");
}

TEST(FormatExpressionChecks, function_call_one_arg) {
    // arrange / act
    auto result = format_expression(*make_call("abs", make_identifier("x")));
    // assert
    ASSERT_EQ(result, "abs(x)");
}

TEST(FormatExpressionChecks, function_call_multi_args) {
    // arrange / act
    auto result = format_expression(*make_call("v", make_identifier("a"), make_identifier("b")));
    // assert
    ASSERT_EQ(result, "v(a,b)");
}

TEST(FormatExpressionChecks, function_call_nested_arg) {
    // arrange / act
    auto result = format_expression(*make_call("abs", make_binary(make_identifier("a"), BinaryOperator::ADD, make_identifier("b"))));
    // assert
    ASSERT_EQ(result, "abs((a+b))");
}

TEST(FormatExpressionChecks, ternary) {
    // arrange / act
    auto result = format_expression(*make_ternary(make_identifier("c"), make_identifier("a"), make_identifier("b")));
    // assert
    ASSERT_EQ(result, "(c?a:b)");
}

TEST(FormatExpressionChecks, ternary_nested) {
    // arrange
    auto inner = make_ternary(make_identifier("c1"), make_identifier("a1"), make_identifier("b1"));
    auto outer = make_ternary(std::move(inner), make_identifier("a2"), make_identifier("b2"));
    // act
    auto result = format_expression(*outer);
    // assert
    ASSERT_EQ(result, "((c1?a1:b1)?a2:b2)");
}

TEST(FormatExpressionChecks, step_selector) {
    // arrange / act
    auto result = format_expression(*make_step(make_identifier("v"), 3));
    // assert
    ASSERT_EQ(result, "v@3");
}

TEST(FormatExpressionChecks, step_selector_nested) {
    // arrange / act
    auto result = format_expression(*make_step(make_binary(make_identifier("a"), BinaryOperator::ADD, make_identifier("b")), 2));
    // assert
    ASSERT_EQ(result, "(a+b)@2");
}

TEST(FormatExpressionChecks, unknown_node_type_returns_empty_string) {
    // arrange
    auto node = std::make_unique<UnknownNode>();
    // act
    auto result = format_expression(*node);
    // assert
    ASSERT_EQ(result, "");
}

TEST(FormatExpressionChecks, parse_and_format_round_trip) {
    // arrange
    XyceParser parser;
    // act
    auto parsed = parser.parse_expression("(v(out)+i(r1))*3.14");
    auto result = format_expression(*parsed);
    // assert
    ASSERT_EQ(result, "((v(out)+i(r1))*3.14)");
}

// ========================================================================================
// has_step_selector
// ========================================================================================

TEST(HasStepSelectorChecks, number_node_returns_false) {
    // arrange / act
    auto result = has_step_selector(*make_number("1"));
    // assert
    ASSERT_FALSE(result);
}

TEST(HasStepSelectorChecks, identifier_node_returns_false) {
    // arrange / act
    auto result = has_step_selector(*make_identifier("v"));
    // assert
    ASSERT_FALSE(result);
}

TEST(HasStepSelectorChecks, direct_step_selector_returns_true) {
    // arrange / act
    auto result = has_step_selector(*make_step(make_identifier("v"), 0));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, unary_node_containing_step_selector_returns_true) {
    // arrange / act
    auto result = has_step_selector(*make_unary(UnaryOperator::NEG, make_step(make_identifier("v"), 0)));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, unary_node_without_step_selector_returns_false) {
    // arrange / act
    auto result = has_step_selector(*make_unary(UnaryOperator::NEG, make_identifier("v")));
    // assert
    ASSERT_FALSE(result);
}

TEST(HasStepSelectorChecks, binary_node_left_contains_step_selector_returns_true) {
    // arrange / act
    auto result = has_step_selector(*make_binary(make_step(make_identifier("v"), 0), BinaryOperator::ADD, make_identifier("i")));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, binary_node_right_contains_step_selector_returns_true) {
    // arrange / act
    auto result = has_step_selector(*make_binary(make_identifier("v"), BinaryOperator::ADD, make_step(make_identifier("i"), 1)));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, binary_node_both_contain_step_selector_returns_true) {
    // arrange / act
    auto result = has_step_selector(*make_binary(make_step(make_identifier("a"), 0), BinaryOperator::ADD, make_step(make_identifier("b"), 1)));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, binary_node_without_step_selector_returns_false) {
    // arrange / act
    auto result = has_step_selector(*make_binary(make_identifier("v"), BinaryOperator::ADD, make_identifier("i")));
    // assert
    ASSERT_FALSE(result);
}

TEST(HasStepSelectorChecks, ternary_condition_contains_step_selector) {
    // arrange / act
    auto result = has_step_selector(*make_ternary(make_step(make_identifier("c"), 0), make_identifier("a"), make_identifier("b")));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, ternary_if_true_contains_step_selector) {
    // arrange / act
    auto result = has_step_selector(*make_ternary(make_identifier("c"), make_step(make_identifier("a"), 0), make_identifier("b")));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, ternary_if_false_contains_step_selector) {
    // arrange / act
    auto result = has_step_selector(*make_ternary(make_identifier("c"), make_identifier("a"), make_step(make_identifier("b"), 0)));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, ternary_without_step_selector_returns_false) {
    // arrange / act
    auto result = has_step_selector(*make_ternary(make_identifier("c"), make_identifier("a"), make_identifier("b")));
    // assert
    ASSERT_FALSE(result);
}

TEST(HasStepSelectorChecks, function_call_contains_step_selector_in_arg) {
    // arrange / act
    auto result = has_step_selector(*make_call("abs", make_step(make_identifier("x"), 0)));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, function_call_multiple_args_one_contains_step_selector) {
    // arrange / act
    auto result = has_step_selector(*make_call("v", make_identifier("a"), make_step(make_identifier("b"), 1)));
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, function_call_without_step_selector_returns_false) {
    // arrange / act
    auto result = has_step_selector(*make_call("abs", make_identifier("x")));
    // assert
    ASSERT_FALSE(result);
}

TEST(HasStepSelectorChecks, deeply_nested_step_selector_returns_true) {
    // arrange / act
    auto expr = make_binary(make_identifier("v"), BinaryOperator::ADD, make_call("abs", make_step(make_identifier("x"), 2)));
    auto result = has_step_selector(*expr);
    // assert
    ASSERT_TRUE(result);
}

TEST(HasStepSelectorChecks, unknown_node_type_returns_false) {
    // arrange
    auto node = std::make_unique<UnknownNode>();
    // act
    auto result = has_step_selector(*node);
    // assert
    ASSERT_FALSE(result);
}

// ========================================================================================
// infer_unit
// ========================================================================================

TEST(InferUnitChecks, number_node_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_number("3.14"), {});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, identifier_from_context) {
    // arrange / act
    auto result = infer_unit(*make_identifier("v_out"), {{"v_out", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, identifier_from_context_case_mismatch) {
    // arrange / act
    auto result = infer_unit(*make_identifier("V_OUT"), {{"v_out", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, identifier_builtin_mho_is_siemens) {
    // arrange / act
    auto result = infer_unit(*make_identifier("mho"), {});
    // assert
    ASSERT_EQ(result, "S");
}

TEST(InferUnitChecks, identifier_builtin_s_is_seconds) {
    // arrange / act
    auto result = infer_unit(*make_identifier("s"), {});
    // assert
    ASSERT_EQ(result, "s");
}

TEST(InferUnitChecks, identifier_builtin_pi_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_identifier("pi"), {});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, identifier_builtin_case_insensitive) {
    // arrange / act
    auto result_mho = infer_unit(*make_identifier("MHO"), {});
    auto result_meg = infer_unit(*make_identifier("MEG"), {});
    auto result_mil = infer_unit(*make_identifier("MIL"), {});
    // assert
    ASSERT_EQ(result_mho, "S");
    ASSERT_EQ(result_meg, "");
    ASSERT_EQ(result_mil, "");
}

TEST(InferUnitChecks, identifier_unknown_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_identifier("xyzzy"), {});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, unary_pos_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_unary(UnaryOperator::POS, make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, unary_neg_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_unary(UnaryOperator::NEG, make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, unary_not_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_unary(UnaryOperator::NOT, make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, binary_add_matching_units) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v1"), BinaryOperator::ADD, make_identifier("v2")), {{"v1", "V"}, {"v2", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, binary_add_mismatched_units) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::ADD, make_identifier("i")), {{"v", "V"}, {"i", "A"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_sub_matching_units) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v1"), BinaryOperator::SUB, make_identifier("v2")), {{"v1", "V"}, {"v2", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, binary_sub_mismatched_units) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::SUB, make_identifier("i")), {{"v", "V"}, {"i", "A"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_mul_v_a_equals_w) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::MUL, make_identifier("i")), {{"v", "V"}, {"i", "A"}});
    // assert
    ASSERT_EQ(result, "W");
}

TEST(InferUnitChecks, binary_mul_a_v_equals_w) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("i"), BinaryOperator::MUL, make_identifier("v")), {{"v", "V"}, {"i", "A"}});
    // assert
    ASSERT_EQ(result, "W");
}

TEST(InferUnitChecks, binary_mul_s_v_equals_a) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("g"), BinaryOperator::MUL, make_identifier("v")), {{"g", "S"}, {"v", "V"}});
    // assert
    ASSERT_EQ(result, "A");
}

TEST(InferUnitChecks, binary_mul_v_s_equals_a) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::MUL, make_identifier("g")), {{"v", "V"}, {"g", "S"}});
    // assert
    ASSERT_EQ(result, "A");
}

TEST(InferUnitChecks, binary_mul_scalar_x_equals_x) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_number("2"), BinaryOperator::MUL, make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, binary_mul_x_scalar_equals_x) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::MUL, make_number("2")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, binary_div_x_x_equals_scalar) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::DIV, make_identifier("v2")), {{"v", "V"}, {"v2", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_div_v_a_equals_ohm) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::DIV, make_identifier("i")), {{"v", "V"}, {"i", "A"}});
    // assert
    ASSERT_EQ(result, "\u03A9");
}

TEST(InferUnitChecks, binary_div_a_v_equals_siemens) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("i"), BinaryOperator::DIV, make_identifier("v")), {{"v", "V"}, {"i", "A"}});
    // assert
    ASSERT_EQ(result, "S");
}

TEST(InferUnitChecks, binary_div_x_scalar_equals_x) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::DIV, make_number("2")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, binary_div_scalar_s_equals_ohm) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_number("1"), BinaryOperator::DIV, make_identifier("g")), {{"g", "S"}});
    // assert
    ASSERT_EQ(result, "\u03A9");
}

TEST(InferUnitChecks, binary_div_scalar_ohm_equals_siemens) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_number("1"), BinaryOperator::DIV, make_identifier("z")), {{"z", "\u03A9"}});
    // assert
    ASSERT_EQ(result, "S");
}

TEST(InferUnitChecks, binary_div_scalar_s_equals_hz) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_number("1"), BinaryOperator::DIV, make_identifier("t")), {{"t", "s"}});
    // assert
    ASSERT_EQ(result, "Hz");
}

TEST(InferUnitChecks, binary_div_scalar_hz_equals_s) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_number("1"), BinaryOperator::DIV, make_identifier("freq")), {{"freq", "Hz"}});
    // assert
    ASSERT_EQ(result, "s");
}

TEST(InferUnitChecks, binary_div_scalar_unknown_unit_is_empty) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_number("1"), BinaryOperator::DIV, make_identifier("x")), {{"x", "W"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_div_empty_left_empty_right_returns_empty) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("x"), BinaryOperator::DIV, make_identifier("y")), {{"x", ""}, {"y", ""}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_mod_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::MOD, make_identifier("v2")), {{"v", "V"}, {"v2", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_pow_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::POW, make_number("2")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_logical_and_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("a"), BinaryOperator::LOGICAL_AND, make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_logical_or_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("a"), BinaryOperator::LOGICAL_OR, make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_logical_xor_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("a"), BinaryOperator::LOGICAL_XOR, make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_equal_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("a"), BinaryOperator::EQUAL, make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_not_equal_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("a"), BinaryOperator::NOT_EQUAL, make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_less_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("a"), BinaryOperator::LESS, make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_less_equal_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("a"), BinaryOperator::LESS_EQUAL, make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_greater_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("a"), BinaryOperator::GREATER, make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, binary_greater_equal_is_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("a"), BinaryOperator::GREATER_EQUAL, make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, function_call_v_returns_volts) {
    // arrange / act
    auto result = infer_unit(*make_call("v", make_identifier("out")), {});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_v_case_insensitive) {
    // arrange / act
    auto result = infer_unit(*make_call("V", make_identifier("out")), {});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_v_uses_context_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("v", make_identifier("out")), {{"v(out)", "mV"}});
    // assert
    ASSERT_EQ(result, "mV");
}

TEST(InferUnitChecks, function_call_i_returns_amps) {
    // arrange / act
    auto result = infer_unit(*make_call("i", make_identifier("r1")), {});
    // assert
    ASSERT_EQ(result, "A");
}

TEST(InferUnitChecks, function_call_id_returns_amps) {
    // arrange / act
    auto result = infer_unit(*make_call("id", make_identifier("m1")), {});
    // assert
    ASSERT_EQ(result, "A");
}

TEST(InferUnitChecks, function_call_z_probe_in_context_returns_ohm) {
    // arrange / act
    auto result = infer_unit(*make_call("z11", make_number("1"), make_number("1")), {{"z11(1, 1)", "dB"}});
    // assert
    ASSERT_EQ(result, "dB");
}

TEST(InferUnitChecks, function_call_z_probe_not_in_context_returns_empty) {
    // arrange / act
    auto result = infer_unit(*make_call("z11", make_number("1"), make_number("1")), {});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, function_call_y_probe_in_context_returns_siemens) {
    // arrange / act
    auto result = infer_unit(*make_call("y21", make_number("2"), make_number("1")), {{"y21(2, 1)", "S"}});
    // assert
    ASSERT_EQ(result, "S");
}

TEST(InferUnitChecks, function_call_y_probe_not_in_context_returns_empty) {
    // arrange / act
    auto result = infer_unit(*make_call("y11", make_number("1"), make_number("1")), {});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, function_call_s_probe_in_context_uses_context) {
    // arrange / act
    auto result = infer_unit(*make_call("s11", make_number("1"), make_number("1")), {{"s11(1, 1)", "dB"}});
    // assert
    ASSERT_EQ(result, "dB");
}

TEST(InferUnitChecks, function_call_s_probe_not_in_context_returns_empty) {
    // arrange / act
    auto result = infer_unit(*make_call("s11", make_number("1"), make_number("1")), {});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, function_call_h_probe_in_context_uses_context) {
    // arrange / act
    auto result = infer_unit(*make_call("h11", make_number("1"), make_number("1")), {{"h11(1, 1)", ""}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, function_call_h_probe_not_in_context_returns_empty) {
    // arrange / act
    auto result = infer_unit(*make_call("h12", make_number("1"), make_number("2")), {});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, function_call_z_probe_with_empty_context_unit_defaults_to_ohm) {
    // arrange / act: stored unit empty falls through to the z->ohm branch
    auto result = infer_unit(*make_call("z11", make_number("1"), make_number("1")), {{"z11(1, 1)", ""}});
    // assert
    ASSERT_EQ(result, "\u03A9");
}

TEST(InferUnitChecks, function_call_y_probe_with_empty_context_unit_defaults_to_siemens) {
    // arrange / act: stored unit empty falls through to the y->siemens branch
    auto result = infer_unit(*make_call("y21", make_number("2"), make_number("1")), {{"y21(2, 1)", ""}});
    // assert
    ASSERT_EQ(result, "S");
}

TEST(InferUnitChecks, binary_mul_generic_non_special_fallthrough) {
    // arrange / act: V * W is not a special product, returns the left unit
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::MUL, make_identifier("w")), {{"v", "V"}, {"w", "W"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, binary_mul_omega_siemens_non_special_fallthrough) {
    // arrange / act: omega * siemens is not a special product, returns the left unit
    auto result = infer_unit(*make_binary(make_identifier("z"), BinaryOperator::MUL, make_identifier("g")), {{"z", "\u03A9"}, {"g", "S"}});
    // assert
    ASSERT_EQ(result, "\u03A9");
}

TEST(InferUnitChecks, binary_div_generic_fallthrough_returns_empty) {
    // arrange / act: V / W matches no special division rule
    auto result = infer_unit(*make_binary(make_identifier("v"), BinaryOperator::DIV, make_identifier("w")), {{"v", "V"}, {"w", "W"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, identifier_context_overrides_builtin_constant) {
    // arrange / act: context shadows the builtin seconds constant
    auto result = infer_unit(*make_identifier("s"), {{"s", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, builtin_constant_units_are_dimensionless) {
    // arrange / act: remaining CONSTANT_UNITS entries resolve to no unit
    auto result_e = infer_unit(*make_identifier("e"), {});
    auto result_f = infer_unit(*make_identifier("f"), {});
    auto result_g = infer_unit(*make_identifier("g"), {});
    auto result_j = infer_unit(*make_identifier("j"), {});
    auto result_k = infer_unit(*make_identifier("k"), {});
    auto result_m = infer_unit(*make_identifier("m"), {});
    auto result_n = infer_unit(*make_identifier("n"), {});
    auto result_p = infer_unit(*make_identifier("p"), {});
    auto result_t = infer_unit(*make_identifier("t"), {});
    auto result_u = infer_unit(*make_identifier("u"), {});
    // assert
    ASSERT_EQ(result_e, "");
    ASSERT_EQ(result_f, "");
    ASSERT_EQ(result_g, "");
    ASSERT_EQ(result_j, "");
    ASSERT_EQ(result_k, "");
    ASSERT_EQ(result_m, "");
    ASSERT_EQ(result_n, "");
    ASSERT_EQ(result_p, "");
    ASSERT_EQ(result_t, "");
    ASSERT_EQ(result_u, "");
}

TEST(InferUnitChecks, binary_mul_both_dimensionless_returns_empty) {
    // arrange / act
    auto result = infer_unit(*make_binary(make_identifier("x"), BinaryOperator::MUL, make_identifier("y")), {{"x", ""}, {"y", ""}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, probe_with_complex_arg_reconstructs_empty_key) {
    // arrange: non-identifier/number arg yields an empty arg name in the probe key
    auto result = infer_unit(*make_call("v", make_binary(make_identifier("a"), BinaryOperator::ADD, make_identifier("b"))), {});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(FormatExpressionChecks, nested_unary_formatting) {
    // arrange / act
    auto result = format_expression(*make_unary(UnaryOperator::NEG, make_unary(UnaryOperator::NOT, make_identifier("x"))));
    // assert
    ASSERT_EQ(result, "-~x");
}

TEST(FormatExpressionChecks, nested_step_with_ternary_formatting) {
    // arrange / act
    auto result = format_expression(*make_step(make_ternary(make_identifier("c"), make_identifier("a"), make_identifier("b")), 1));
    // assert
    ASSERT_EQ(result, "(c?a:b)@1");
}

TEST(InferUnitChecks, function_call_nullary_dimensionless) {
    // arrange / act
    auto result = infer_unit(*make_call("time"), {});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, function_call_abs_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("abs", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_real_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("real", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_imag_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("imag", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_mag_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("mag", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_conj_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("conj", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_uramp_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("uramp", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_round_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("round", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_floor_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("floor", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_ceil_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("ceil", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_int_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("int", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_sqr_preserves_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("sqr", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_preserving_case_insensitive) {
    // arrange / act
    auto result_abs = infer_unit(*make_call("ABS", make_identifier("v")), {{"v", "V"}});
    auto result_real = infer_unit(*make_call("ReAl", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result_abs, "V");
    ASSERT_EQ(result_real, "V");
}

TEST(InferUnitChecks, function_call_db_composes_argument_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("db", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "dBV");
}

TEST(InferUnitChecks, function_call_db_composes_power_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("db", make_identifier("w")), {{"w", "W"}});
    // assert
    ASSERT_EQ(result, "dBW");
}

TEST(InferUnitChecks, function_call_db_case_insensitive) {
    // arrange / act
    auto result = infer_unit(*make_call("DB", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "dBV");
}

TEST(InferUnitChecks, function_call_db_dimensionless_argument_returns_plain_dB) {
    // arrange / act: ratio expressions like gain or loss keep plain dB
    auto result = infer_unit(*make_call("db", make_identifier("gain")), {{"gain", ""}});
    // assert
    ASSERT_EQ(result, "dB");
}

TEST(InferUnitChecks, function_call_angle_returns_degrees) {
    // arrange / act
    auto result = infer_unit(*make_call("angle", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "\u00B0");
}

TEST(InferUnitChecks, function_call_ph_returns_degrees) {
    // arrange / act
    auto result = infer_unit(*make_call("ph", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "\u00B0");
}

TEST(InferUnitChecks, function_call_phase_returns_degrees) {
    // arrange / act
    auto result = infer_unit(*make_call("phase", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "\u00B0");
}

TEST(InferUnitChecks, function_call_unknown_unary_returns_empty) {
    // arrange / act
    auto result = infer_unit(*make_call("foo", make_identifier("v")), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, function_call_min_preserves_first_arg_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("min", make_identifier("a"), make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_max_preserves_first_arg_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("max", make_identifier("a"), make_identifier("b")), {{"a", "A"}, {"b", "A"}});
    // assert
    ASSERT_EQ(result, "A");
}

TEST(InferUnitChecks, function_call_limit_preserves_first_arg_unit) {
    // arrange / act
    auto result = infer_unit(*make_call("limit", make_identifier("v"), make_identifier("lo"), make_identifier("hi")), {{"v", "V"}, {"lo", "V"}, {"hi", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_limit_case_insensitive) {
    // arrange / act
    auto result = infer_unit(*make_call("LIMIT", make_identifier("v"), make_identifier("lo"), make_identifier("hi")), {{"v", "V"}, {"lo", "V"}, {"hi", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, function_call_unknown_multi_arg_returns_empty) {
    // arrange / act
    auto result = infer_unit(*make_call("foo", make_identifier("a"), make_identifier("b")), {{"a", "V"}, {"b", "V"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, function_call_probe_key_in_context_overrides_default) {
    // arrange / act
    auto result_v = infer_unit(*make_call("v", make_identifier("out")), {{"v(out)", "mV"}});
    auto result_i = infer_unit(*make_call("i", make_identifier("r1")), {{"i(r1)", "mA"}});
    auto result_id = infer_unit(*make_call("id", make_identifier("m1")), {{"id(m1)", "uA"}});
    // assert
    ASSERT_EQ(result_v, "mV");
    ASSERT_EQ(result_i, "mA");
    ASSERT_EQ(result_id, "uA");
}

TEST(InferUnitChecks, function_call_probe_key_in_context_with_empty_falls_through_to_default) {
    // arrange / act
    auto result = infer_unit(*make_call("v", make_identifier("out")), {{"v(out)", ""}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, ternary_matching_units) {
    // arrange / act
    auto result = infer_unit(*make_ternary(make_number("1"), make_identifier("v1"), make_identifier("v2")), {{"v1", "V"}, {"v2", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, ternary_mismatched_units) {
    // arrange / act
    auto result = infer_unit(*make_ternary(make_number("1"), make_identifier("v"), make_identifier("i")), {{"v", "V"}, {"i", "A"}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, ternary_both_dimensionless_returns_empty) {
    // arrange / act
    auto result = infer_unit(*make_ternary(make_number("1"), make_identifier("x"), make_identifier("y")), {{"x", ""}, {"y", ""}});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, step_selector_returns_base_unit) {
    // arrange / act
    auto result = infer_unit(*make_step(make_identifier("v"), 1), {{"v", "V"}});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, step_selector_nested_expression) {
    // arrange / act
    auto result = infer_unit(*make_step(make_binary(make_identifier("v"), BinaryOperator::DIV, make_identifier("i")), 0), {{"v", "V"}, {"i", "A"}});
    // assert
    ASSERT_EQ(result, "\u03A9");
}

TEST(InferUnitChecks, unknown_node_type_returns_empty) {
    // arrange
    auto node = std::make_unique<UnknownNode>();
    // act
    auto result = infer_unit(*node, {});
    // assert
    ASSERT_EQ(result, "");
}

TEST(InferUnitChecks, parse_and_infer_v_out) {
    // arrange
    XyceParser parser;
    // act
    auto expr = parser.parse_expression("v(out)");
    auto result = infer_unit(*expr, {});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, parse_and_infer_v_out_times_i_r1) {
    // arrange
    XyceParser parser;
    // act
    auto expr = parser.parse_expression("v(out)*i(r1)");
    auto result = infer_unit(*expr, {});
    // assert
    ASSERT_EQ(result, "W");
}

TEST(InferUnitChecks, parse_and_infer_v_out_div_i_r1) {
    // arrange
    XyceParser parser;
    // act
    auto expr = parser.parse_expression("v(out)/i(r1)");
    auto result = infer_unit(*expr, {});
    // assert
    ASSERT_EQ(result, "\u03A9");
}

TEST(InferUnitChecks, parse_and_infer_db_v_out) {
    // arrange
    XyceParser parser;
    // act
    auto expr = parser.parse_expression("db(v(out))");
    auto result = infer_unit(*expr, {});
    // assert
    ASSERT_EQ(result, "dBV");
}

TEST(InferUnitChecks, parse_and_infer_v_out_at_2) {
    // arrange
    XyceParser parser;
    // act
    auto expr = parser.parse_expression("v(out)@2");
    auto result = infer_unit(*expr, {});
    // assert
    ASSERT_EQ(result, "V");
}

TEST(InferUnitChecks, parse_and_infer_ternary_with_matching_units) {
    // arrange
    XyceParser parser;
    // act
    auto expr = parser.parse_expression("v(out) > 0 ? v(out) : 0");
    auto result = infer_unit(*expr, {});
    // assert
    ASSERT_EQ(result, "");
}

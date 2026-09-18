#include <cmath>
#include <complex>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "expression/expression.h"
#include "expression/expression_manager.h"

namespace
{
    Expression<double> make_real_expression(const std::string& name, const std::vector<double>& values, const std::string& unit = "V") {
        std::vector<double> data(values);
        std::vector<std::span<const double>> steps = {{data.data(), data.size()}};
        return {name, std::move(data), std::move(steps), unit};
    }

    Expression<std::complex<double>> make_complex_expression(const std::string& name, const std::vector<std::complex<double>>& values, const std::string& unit = "") {
        std::vector<std::complex<double>> data(values);
        std::vector<std::span<const std::complex<double>>> steps = {{data.data(), data.size()}};
        return {name, std::move(data), std::move(steps), unit};
    }

    std::vector<double> extract_data(AnyExpression& expr) {
        return std::visit(
            [](auto&& e) -> std::vector<double> {
                auto span = e.data();
                using T = typename std::remove_reference_t<decltype(span)>::value_type;
                if constexpr (std::is_same_v<T, double>)
                    return {span.begin(), span.end()};
                return {};
            },
            expr);
    }

    std::vector<std::complex<double>> extract_complex_data(AnyExpression& expr) {
        return std::visit(
            [](auto&& e) -> std::vector<std::complex<double>> {
                auto span = e.data();
                using T = typename std::remove_reference_t<decltype(span)>::value_type;
                if constexpr (std::is_same_v<T, std::complex<double>>)
                    return {span.begin(), span.end()};
                return {};
            },
            expr);
    }

    std::string extract_name(const AnyExpression& expr) {
        return std::visit([](auto&& e) { return e.name(); }, expr);
    }

    std::string extract_unit(const AnyExpression& expr) {
        return std::visit([](auto&& e) { return e.unit(); }, expr);
    }

    std::string extract_source(const AnyExpression& expr) {
        return std::visit([](auto&& e) { return e.source(); }, expr);
    }
} // namespace

static_assert(!std::is_copy_constructible_v<ExpressionManager>);
static_assert(!std::is_copy_assignable_v<ExpressionManager>);
static_assert(std::is_nothrow_move_constructible_v<ExpressionManager>);
static_assert(std::is_nothrow_move_assignable_v<ExpressionManager>);

TEST(ExpressionManagerChecks, constructor_throws_on_empty_step_slices) {
    // arrange
    std::vector<AnyExpression> expressions;
    std::vector<std::pair<size_t, size_t>> slices;
    // assert
    ASSERT_THROW((ExpressionManager{expressions, slices}), std::invalid_argument);
}

TEST(ExpressionManagerChecks, abscissa_returns_first_real_expression) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("time", {0.0, 1.0, 2.0}, "s"));
    expressions.emplace_back(make_real_expression("V(out)", {1.0, 2.0, 3.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}};
    ExpressionManager manager(expressions, slices);
    // act
    auto& abscissa = manager.abscissa();
    // assert
    ASSERT_EQ(abscissa.name(), "time");
    ASSERT_EQ(abscissa.unit(), "s");
    ASSERT_EQ(abscissa.data().size(), 3);
    ASSERT_DOUBLE_EQ(abscissa.data()[2], 2.0);
}

TEST(ExpressionManagerChecks, evaluate_imaginary_unit_builds_complex_expression) {
    // arrange
    std::vector<AnyExpression> expressions;
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}, {2, 4}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("j");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_complex_data(*result);
    ASSERT_EQ(data.size(), 4);
    for (const auto& point : data) {
        EXPECT_DOUBLE_EQ(point.real(), 0.0);
        EXPECT_DOUBLE_EQ(point.imag(), 1.0);
    }
    ASSERT_EQ(extract_source(*result), "expression manager");
}

TEST(ExpressionManagerChecks, evaluate_complex_vector_expression_preserves_complex_data) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_complex_expression("c", {{1.0, 1.0}, {2.0, 0.0}}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("c*1");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_complex_data(*result);
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[0].real(), 1.0);
    EXPECT_DOUBLE_EQ(data[0].imag(), 1.0);
    EXPECT_DOUBLE_EQ(data[1].real(), 2.0);
    EXPECT_DOUBLE_EQ(data[1].imag(), 0.0);
    ASSERT_EQ(extract_source(*result), "expression manager");
}

TEST(ExpressionManagerChecks, rematerialize_tiles_single_step_real_to_all_steps) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {1.0, 2.0, 3.0, 4.0, 5.0, 6.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}, {3, 6}};
    ExpressionManager manager(expressions, slices);
    // act: @1 selects the first 3-point step, tiled across both steps
    const auto result = manager.evaluate("v(a)@1");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_data(*result);
    ASSERT_EQ(data.size(), 6);
    ASSERT_DOUBLE_EQ(data[0], 1.0);
    ASSERT_DOUBLE_EQ(data[1], 2.0);
    ASSERT_DOUBLE_EQ(data[2], 3.0);
    ASSERT_DOUBLE_EQ(data[3], 1.0);
    ASSERT_DOUBLE_EQ(data[4], 2.0);
    ASSERT_DOUBLE_EQ(data[5], 3.0);
}

TEST(ExpressionManagerChecks, rematerialize_scalar_with_selector_returns_early) {
    // arrange
    std::vector<AnyExpression> expressions;
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}, {3, 6}};
    ExpressionManager manager(expressions, slices);
    // act: scalar values never need tiling even with a step selector
    const auto result = manager.evaluate("pi@1");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_data(*result);
    ASSERT_EQ(data.size(), 6);
    for (const auto& point : data)
        ASSERT_DOUBLE_EQ(point, std::acos(-1.0));
}

TEST(ExpressionManagerChecks, rematerialize_vector_without_selector_untiled) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {1.0, 2.0, 3.0, 4.0, 5.0, 6.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}, {3, 6}};
    ExpressionManager manager(expressions, slices);
    // act: no step selector means no tiling
    const auto result = manager.evaluate("v(a)");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_data(*result);
    ASSERT_EQ(data.size(), 6);
}

TEST(ExpressionManagerChecks, rematerialize_full_size_vector_returns_early) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {1.0, 2.0, 3.0, 4.0, 5.0, 6.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}, {3, 6}};
    ExpressionManager manager(expressions, slices);
    // act: vector already covers all steps; selector present but no tiling needed
    const auto result = manager.evaluate("v(a) + pi@1");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_data(*result);
    ASSERT_EQ(data.size(), 6);
    ASSERT_DOUBLE_EQ(data[0], 1.0 + std::acos(-1.0));
    ASSERT_DOUBLE_EQ(data[5], 6.0 + std::acos(-1.0));
}

TEST(ExpressionManagerChecks, rematerialize_size_mismatch_returns_untiled) {
    // arrange: vector length matches neither a single step nor all steps
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {1.0, 2.0, 3.0, 4.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}, {3, 6}};
    ExpressionManager manager(expressions, slices);
    // act: 4-point result cannot be tiled to the 3-point step length
    const auto result = manager.evaluate("v(a) + pi@1");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_data(*result);
    ASSERT_EQ(data.size(), 4);
}

TEST(ExpressionManagerChecks, rematerialize_tiles_complex_single_step_to_all_steps) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_complex_expression("c", {{1.0, 1.0}, {2.0, 0.0}, {3.0, 3.0}, {4.0, 0.0}, {5.0, 0.0}, {6.0, 0.0}}));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}, {3, 6}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("c@1");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_complex_data(*result);
    ASSERT_EQ(data.size(), 6);
    EXPECT_DOUBLE_EQ(data[0].real(), 1.0);
    EXPECT_DOUBLE_EQ(data[0].imag(), 1.0);
    EXPECT_DOUBLE_EQ(data[3].real(), 1.0);
    EXPECT_DOUBLE_EQ(data[3].imag(), 1.0);
    EXPECT_DOUBLE_EQ(data[5].real(), 3.0);
    EXPECT_DOUBLE_EQ(data[5].imag(), 3.0);
}

TEST(ExpressionManagerChecks, infer_unit_returns_empty_on_parse_failure) {
    // arrange
    std::vector<AnyExpression> expressions;
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.infer_unit("1 +");
    // assert
    ASSERT_EQ(result, "");
}

TEST(ExpressionManagerChecks, move_construct_and_move_assign_smoke) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {1.0, 2.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act: move construction preserves state
    ExpressionManager moved(std::move(manager));
    const auto first = moved.evaluate("v(a)");
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(extract_name(*first), "v(a)");
    // act: move assignment preserves state
    ExpressionManager assigned;
    assigned = std::move(moved);
    const auto second = assigned.evaluate("v(a)");
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(extract_unit(*second), "V");
}

TEST(ExpressionManagerChecks, default_constructor_initializes_empty_state) {
    // arrange / act
    ExpressionManager manager;
    const auto result = manager.evaluate("v(out)");
    // assert
    ASSERT_TRUE(manager.expressions().empty());
    ASSERT_TRUE(manager.expression_names().empty());
    ASSERT_TRUE(manager.step_slices().empty());
    ASSERT_EQ(result, nullptr);
}

TEST(ExpressionManagerChecks, constructor_stores_expressions_and_step_slices) {
    // arrange / act
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("time", {0.0, 1.0, 2.0}, "s"));
    expressions.emplace_back(make_real_expression("V(out)", {1.0, 2.0, 3.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}, {3, 6}};
    ExpressionManager manager(expressions, slices);
    // assert
    ASSERT_EQ(manager.expressions().size(), 2);
    ASSERT_EQ(std::get<Expression<double>>(*manager.expressions()[0]).name(), "time");
    ASSERT_EQ(std::get<Expression<double>>(*manager.expressions()[1]).name(), "V(out)");
    ASSERT_EQ(manager.step_slices().size(), 2);
    ASSERT_EQ(manager.step_slices()[0], (std::pair<size_t, size_t>{0, 3}));
    ASSERT_EQ(manager.step_slices()[1], (std::pair<size_t, size_t>{3, 6}));
}

TEST(ExpressionManagerChecks, expression_names_returns_names_in_insertion_order) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("time", {0.0, 1.0}, "s"));
    expressions.emplace_back(make_real_expression("V(out)", {1.0, 2.0}, "V"));
    expressions.emplace_back(make_real_expression("I(R1)", {0.1, 0.2}, "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto names = manager.expression_names();
    // assert
    ASSERT_EQ(names, std::vector<std::string>({"time", "V(out)", "I(R1)"}));
}

TEST(ExpressionManagerChecks, step_slices_property_returns_stored_slices) {
    // arrange / act
    std::vector<std::pair<size_t, size_t>> slices = {{0, 5}, {5, 10}};
    std::vector<AnyExpression> expressions;
    ExpressionManager manager(expressions, slices);
    // assert
    ASSERT_EQ(manager.step_slices().size(), 2);
    ASSERT_EQ(manager.step_slices()[0], (std::pair<size_t, size_t>{0, 5}));
    ASSERT_EQ(manager.step_slices()[1], (std::pair<size_t, size_t>{5, 10}));
}

TEST(ExpressionManagerChecks, evaluate_finds_expression_by_expression_name_case_insensitively) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("V(Out)", {1.0, 2.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("v(out)");
    // assert
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(std::get<Expression<double>>(*result).name(), "V(Out)");
    ASSERT_EQ(std::get<Expression<double>>(*result).unit(), "V");
}

TEST(ExpressionManagerChecks, evaluate_uses_name_override_for_lookup_when_provided) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("I(R1)", {0.1, 0.2}, "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("not used", "i(r1)");
    // assert
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(std::get<Expression<double>>(*result).name(), "I(R1)");
    ASSERT_EQ(std::get<Expression<double>>(*result).unit(), "A");
}

TEST(ExpressionManagerChecks, evaluate_returns_nullopt_when_lookup_key_is_missing) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("V(in)", {1.0, 2.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("V(out)");
    // assert
    ASSERT_EQ(result, nullptr);
}

TEST(ExpressionManagerChecks, evaluate_with_name_override_creates_new_expression) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("V(out)", {1.0, 2.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("V(out)", "missing");
    // assert
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(extract_name(*result), "missing");
    ASSERT_EQ(extract_unit(*result), "V");
}

TEST(ExpressionManagerChecks, evaluate_returns_last_expression_when_casefolded_keys_collide) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("V(OUT)", {1.0, 1.0}, "V"));
    expressions.emplace_back(make_real_expression("v(out)", {2.0, 2.0}, "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("V(out)");
    // assert
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(std::get<Expression<double>>(*result).name(), "v(out)");
    ASSERT_EQ(std::get<Expression<double>>(*result).unit(), "A");
}

TEST(ExpressionManagerChecks, evaluate_arithmetic_expression_computes_data) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {2.0, 4.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("v(a)*2");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_data(*result);
    ASSERT_EQ(data.size(), 2);
    ASSERT_DOUBLE_EQ(data[0], 4.0);
    ASSERT_DOUBLE_EQ(data[1], 8.0);
}

TEST(ExpressionManagerChecks, evaluate_uses_provided_name) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {1.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 1}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("v(a)", "my_result");
    // assert
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(extract_name(*result), "my_result");
}

TEST(ExpressionManagerChecks, evaluate_without_name_keeps_the_expression_text_as_name) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {2.0}, "V"));
    expressions.emplace_back(make_real_expression("i(r1)", {0.5}, "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 1}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("v(a)*i(r1)");
    // assert
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(extract_name(*result), "v(a)*i(r1)");
}

TEST(ExpressionManagerChecks, evaluate_infers_unit_volts_times_amps) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {2.0}, "V"));
    expressions.emplace_back(make_real_expression("i(r1)", {0.5}, "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 1}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("v(a)*i(r1)");
    // assert
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(extract_unit(*result), "W");
}

TEST(ExpressionManagerChecks, evaluate_invalid_expression_returns_none) {
    // arrange / act
    ExpressionManager manager;
    const auto result = manager.evaluate("1 +");
    // assert
    ASSERT_EQ(result, nullptr);
}

TEST(ExpressionManagerChecks, evaluate_caches_result_for_subsequent_calls) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {1.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 1}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto first = manager.evaluate("v(a)*2");
    const auto second = manager.evaluate("v(a)*2");
    // assert
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first, second);
}

TEST(ExpressionManagerChecks, evaluate_named_result_cached_by_name) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {1.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 1}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto first = manager.evaluate("v(a)", "my_signal");
    const auto second = manager.evaluate("v(a)", "my_signal");
    // assert
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first, second);
}

TEST(ExpressionManagerChecks, evaluate_source_is_expression_manager) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("v(a)", {1.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 1}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("v(a)*2");
    // assert
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(extract_source(*result), "expression manager");
}

TEST(ExpressionManagerChecks, evaluate_numeric_literal_is_dimensionless) {
    // arrange
    auto expressions = std::vector<AnyExpression>{};
    auto slices = std::vector<std::pair<size_t, size_t>>{{0, 3}};
    auto manager = ExpressionManager(expressions, slices);
    // act
    const auto result = manager.evaluate("3.14");
    // assert
    ASSERT_EQ(extract_unit(*result), "");
    const auto data = extract_data(*result);
    ASSERT_EQ(data.size(), 3);
    ASSERT_DOUBLE_EQ(data[0], 3.14);
    ASSERT_DOUBLE_EQ(data[1], 3.14);
    ASSERT_DOUBLE_EQ(data[2], 3.14);
}

TEST(ExpressionManagerChecks, evaluate_node_name_with_plus_sign) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("V(/POWER_SUPPLY/HB+)", {1.0, 2.0, 3.0}, "V"));
    expressions.emplace_back(make_real_expression("V(HGND)", {0.1, 0.2, 0.3}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("V(/POWER_SUPPLY/HB+)-V(HGND)");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_data(*result);
    ASSERT_EQ(data.size(), 3);
    ASSERT_DOUBLE_EQ(data[0], 0.9);
    ASSERT_DOUBLE_EQ(data[1], 1.8);
    ASSERT_DOUBLE_EQ(data[2], 2.7);
    ASSERT_EQ(extract_unit(*result), "V");
}

TEST(ExpressionManagerChecks, evaluate_node_name_with_colon) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("I(XL201:L1)", {0.5, 0.6, 0.7}, "A"));
    expressions.emplace_back(make_real_expression("I(C207)", {0.1, 0.2, 0.3}, "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.evaluate("I(XL201:L1)-I(C207)");
    // assert
    ASSERT_NE(result, nullptr);
    const auto data = extract_data(*result);
    ASSERT_EQ(data.size(), 3);
    ASSERT_DOUBLE_EQ(data[0], 0.4);
    ASSERT_DOUBLE_EQ(data[1], 0.4);
    ASSERT_DOUBLE_EQ(data[2], 0.4);
    ASSERT_EQ(extract_unit(*result), "A");
}

TEST(ExpressionManagerChecks, infer_unit_returns_cached_unit_when_expression_exists) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("V(out)", {1.0}, "mV"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 1}};
    ExpressionManager manager(expressions, slices);
    // act
    const auto result = manager.infer_unit("V(out)");
    // assert
    ASSERT_EQ(result, "mV");
}

TEST(ExpressionManagerChecks, infer_unit_returns_empty_for_numeric_literal) {
    // arrange
    ExpressionManager manager;
    // act
    const auto result = manager.infer_unit("3.14");
    // assert
    ASSERT_EQ(result, "");
}

TEST(ExpressionManagerChecks, infer_unit_voltage_probe) {
    // arrange
    ExpressionManager manager;
    // act
    const auto result = manager.infer_unit("V(out)");
    // assert
    ASSERT_EQ(result, "V");
}

TEST(ExpressionManagerChecks, infer_unit_current_probe) {
    // arrange
    ExpressionManager manager;
    // act
    const auto result = manager.infer_unit("I(R1)");
    // assert
    ASSERT_EQ(result, "A");
}

TEST(ExpressionManagerChecks, compound_evaluate_keeps_requested_name_and_current_type) {
    // arrange
    const std::string compound = "I(XU301:X1:X1:E1)-I(XU301:X1:X1:C3)";
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("time", {0.0, 1.0, 2.0}, "s"));
    expressions.emplace_back(make_real_expression("I(XU301:X1:X1:E1)", {1.0, 2.0, 3.0}, "A"));
    expressions.emplace_back(make_real_expression("I(XU301:X1:X1:C3)", {3.0, 2.0, 1.0}, "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}};
    ExpressionManager manager(expressions, slices);
    // act
    auto* expression = manager.evaluate(compound, compound);
    // assert
    ASSERT_NE(expression, nullptr);
    ASSERT_EQ(extract_name(*expression), compound);
    ASSERT_EQ(extract_unit(*expression), "A");
}

TEST(ExpressionManagerChecks, compound_reevaluation_without_name_returns_same_expression) {
    // arrange
    const std::string compound = "I(XU301:X1:X1:E1)-I(XU301:X1:X1:C3)";
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("time", {0.0, 1.0, 2.0}, "s"));
    expressions.emplace_back(make_real_expression("I(XU301:X1:X1:E1)", {1.0, 2.0, 3.0}, "A"));
    expressions.emplace_back(make_real_expression("I(XU301:X1:X1:C3)", {3.0, 2.0, 1.0}, "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}};
    ExpressionManager manager(expressions, slices);
    auto* created = manager.evaluate(compound, compound);
    ASSERT_NE(created, nullptr);
    // act
    auto* resolved = manager.evaluate(compound);
    // assert
    ASSERT_EQ(resolved, created);
    ASSERT_EQ(extract_name(*resolved), compound);
}

TEST(ExpressionManagerChecks, compound_reevaluation_after_context_loss_keeps_requested_text_as_name) {
    // arrange: a fresh manager like after a simulation re-run, the chart
    // re-resolves the plotted series name against it
    const std::string compound = "I(XU301:X1:X1:E1)-I(XU301:X1:X1:C3)";
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("time", {0.0, 1.0, 2.0}, "s"));
    expressions.emplace_back(make_real_expression("I(XU301:X1:X1:E1)", {1.0, 2.0, 3.0}, "A"));
    expressions.emplace_back(make_real_expression("I(XU301:X1:X1:C3)", {3.0, 2.0, 1.0}, "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}};
    ExpressionManager manager(expressions, slices);
    // act
    auto* expression = manager.evaluate(compound);
    // assert
    ASSERT_NE(expression, nullptr);
    ASSERT_EQ(extract_name(*expression), compound);
}

TEST(ExpressionManagerChecks, evaluated_voltage_difference_gets_voltage_type) {
    // arrange
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_real_expression("time", {0.0, 1.0}, "s"));
    expressions.emplace_back(make_real_expression("V(1)", {1.0, 2.0}, "V"));
    expressions.emplace_back(make_real_expression("V(2)", {2.0, 1.0}, "V"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 2}};
    ExpressionManager manager(expressions, slices);
    // act
    auto* expression = manager.evaluate("V(1)-V(2)", "V(1)-V(2)");
    // assert
    ASSERT_NE(expression, nullptr);
    ASSERT_EQ(std::get<Expression<double>>(*expression).unit(), "V");
}

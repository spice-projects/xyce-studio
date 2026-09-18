#include <complex>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "expression/expression.h"

template <typename T>
View<T> make_view(const std::vector<T>& buf, const size_t size, const size_t stride, const size_t offset = 0) {
    return View<T>(const_cast<T*>(buf.data()) + offset, size, stride);
}

static_assert(!std::is_default_constructible_v<Expression<double>>);
static_assert(!std::is_copy_constructible_v<Expression<double>>);
static_assert(!std::is_copy_assignable_v<Expression<double>>);

TEST(ExpressionChecks, constructor_stores_metadata_for_span_constructor) {
    // arrange
    std::vector<double> data = {1, 2, 3};
    std::vector<std::span<const double>> steps = {{data.data(), data.size()}};
    // act
    const Expression<double> expr("net_voltage", std::move(data), std::move(steps), "V", "R1");
    // assert
    ASSERT_EQ(expr.name(), "net_voltage");
    ASSERT_EQ(expr.unit(), "V");
    ASSERT_EQ(expr.source(), "R1");
}

TEST(ExpressionChecks, constructor_defaults_source_to_empty) {
    // arrange
    std::vector<double> data = {1, 2, 3};
    std::vector<std::span<const double>> steps = {{data.data(), data.size()}};
    // act
    const Expression<double> expr("net_voltage", std::move(data), std::move(steps), "V");
    // assert
    ASSERT_TRUE(expr.source().empty());
}

TEST(ExpressionChecks, step_count_returns_correct_count_for_view_steps) {
    // arrange
    const std::vector<double> buffer = {1, 10, 2, 20, 3, 30};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 2));
    steps.emplace_back(make_view(buffer, 3, 2, 1));
    const Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto step_count = expr.step_count();
    // assert
    ASSERT_EQ(step_count, 2);
}

TEST(ExpressionChecks, step_count_returns_correct_count_for_span_steps) {
    // arrange
    std::vector<std::complex<double>> data = {1, 2, 3, 4, 5, 6};
    std::vector<std::span<const std::complex<double>>> steps = {{data.data(), 2}, {data.data() + 2, 2}, {data.data() + 4, 2}};
    const Expression<std::complex<double>> expr("I1", std::move(data), std::move(steps), "A");
    // act
    const auto step_count = expr.step_count();
    // assert
    ASSERT_EQ(step_count, 3);
}

TEST(ExpressionChecks, data_and_step_data_initialize_and_return_values_for_view_steps) {
    // arrange
    const std::vector<double> buffer = {1, 10, 2, 20, 3, 30};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 2));
    steps.emplace_back(make_view(buffer, 3, 2, 1));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    const auto step = expr.step_data(1);
    const auto slices = expr.step_indices();
    // assert
    ASSERT_EQ(data.size(), 6);
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[1], 2);
    ASSERT_EQ(data[2], 3);
    ASSERT_EQ(data[3], 10);
    ASSERT_EQ(data[4], 20);
    ASSERT_EQ(data[5], 30);
    ASSERT_EQ(step.size(), 3);
    ASSERT_EQ(step[0], 10);
    ASSERT_EQ(step[1], 20);
    ASSERT_EQ(step[2], 30);
    ASSERT_EQ(slices.size(), 2);
    ASSERT_EQ(slices[0], (std::pair<size_t, size_t>{0, 3}));
    ASSERT_EQ(slices[1], (std::pair<size_t, size_t>{3, 6}));
}

TEST(ExpressionChecks, data_and_step_data_return_values_for_span_steps) {
    // arrange
    std::vector<std::complex<double>> data_buffer = {{1, 1}, {2, 2}, {3, 3}};
    std::vector<std::span<const std::complex<double>>> steps = {{data_buffer.data(), 1}, {data_buffer.data() + 1, 2}};
    Expression<std::complex<double>> expr("I1", std::move(data_buffer), std::move(steps), "A");
    // act
    const auto data = expr.data();
    const auto step = expr.step_data(1);
    const auto slices = expr.step_indices();
    // assert
    ASSERT_EQ(data.size(), 3);
    ASSERT_EQ(data[0], std::complex<double>(1, 1));
    ASSERT_EQ(data[2], std::complex<double>(3, 3));
    ASSERT_EQ(step.size(), 2);
    ASSERT_EQ(step[0], std::complex<double>(2, 2));
    ASSERT_EQ(step[1], std::complex<double>(3, 3));
    ASSERT_EQ(slices.size(), 2);
    ASSERT_EQ(slices[0], (std::pair<size_t, size_t>{0, 1}));
    ASSERT_EQ(slices[1], (std::pair<size_t, size_t>{1, 3}));
}

TEST(ExpressionChecks, step_data_throws_out_of_range_for_invalid_index) {
    // arrange
    std::vector<double> data_buffer = {1, 2};
    std::vector<std::span<const double>> steps = {{data_buffer.data(), 2}};
    Expression<double> expr("V1", std::move(data_buffer), std::move(steps), "V");
    // act
    const auto read_step = [&expr]() { (void)expr.step_data(1); };
    // assert
    ASSERT_THROW(read_step(), std::out_of_range);
}

TEST(ExpressionChecks, transform_updates_metadata_and_values_for_real_expression) {
    // arrange
    std::vector<double> data_buffer = {1, 2, 3, 4};
    std::vector<std::span<const double>> steps = {{data_buffer.data(), 1}, {data_buffer.data() + 1, 3}};
    Expression<double> expr("V1", std::move(data_buffer), std::move(steps), "V", "R1");
    std::string transformed_name = "V1_scaled";
    std::string transformed_unit = "mV";
    // act
    expr.transform([](double v) { return v * 1000.0; });
    const auto data = expr.data();
    const auto step = expr.step_data(1);
    // assert
    ASSERT_EQ(data.size(), 4);
    ASSERT_EQ(data[0], 1000.0);
    ASSERT_EQ(data[1], 2000.0);
    ASSERT_EQ(data[2], 3000.0);
    ASSERT_EQ(data[3], 4000.0);
    ASSERT_EQ(step.size(), 3);
    ASSERT_EQ(step[0], 2000.0);
    ASSERT_EQ(step[1], 3000.0);
    ASSERT_EQ(step[2], 4000.0);
}

TEST(ExpressionChecks, constructor_from_data_and_step_slices_builds_steps) {
    // arrange
    std::vector<double> data = {1, 2, 3, 4, 5, 6};
    const std::vector<std::pair<size_t, size_t>> slices = {{0, 2}, {2, 6}};
    // act
    Expression<double> expr("V1", std::move(data), slices, "V", "R1");
    // assert
    ASSERT_EQ(expr.name(), "V1");
    ASSERT_EQ(expr.unit(), "V");
    ASSERT_EQ(expr.source(), "R1");
    ASSERT_EQ(expr.step_count(), 2);
    ASSERT_EQ(expr.data().size(), 6);
    ASSERT_EQ(expr.data()[0], 1);
    ASSERT_EQ(expr.data()[5], 6);
    ASSERT_EQ(expr.step_data(0).size(), 2);
    ASSERT_EQ(expr.step_data(1).size(), 4);
    ASSERT_EQ(expr.step_data(1)[0], 3);
    ASSERT_EQ(expr.step_indices(), slices);
}

TEST(ExpressionChecks, constructor_from_view_copies_external_strided_data) {
    // arrange
    const std::vector<double> buffer = {1, 10, 2, 20, 3, 30};
    View<double> view(buffer.data(), 3, 2);
    const std::vector<std::pair<size_t, size_t>> slices = {{0, 2}, {2, 3}};
    // act
    Expression<double> expr("V1", std::move(view), slices, "V", "R1");
    // assert
    ASSERT_EQ(expr.name(), "V1");
    ASSERT_EQ(expr.unit(), "V");
    ASSERT_EQ(expr.source(), "R1");
    ASSERT_EQ(expr.step_count(), 2);
    const auto data = expr.data();
    ASSERT_EQ(data.size(), 3);
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[1], 2);
    ASSERT_EQ(data[2], 3);
    ASSERT_EQ(expr.step_data(0).size(), 2);
    ASSERT_EQ(expr.step_data(1).size(), 1);
    ASSERT_EQ(expr.step_data(1)[0], 3);
    ASSERT_EQ(expr.step_indices(), slices);
}

TEST(ExpressionChecks, constructor_from_owning_view_moves_owned_data) {
    // arrange
    std::vector<double> owned = {10, 20, 30, 40};
    View<double> view(std::move(owned));
    const std::vector<std::pair<size_t, size_t>> slices = {{0, 2}, {2, 4}};
    // act
    Expression<double> expr("V1", std::move(view), slices, "V");
    // assert
    ASSERT_EQ(expr.step_count(), 2);
    const auto data = expr.data();
    ASSERT_EQ(data.size(), 4);
    ASSERT_EQ(data[0], 10);
    ASSERT_EQ(data[3], 40);
    ASSERT_EQ(expr.step_data(0).size(), 2);
    ASSERT_EQ(expr.step_data(0)[0], 10);
    ASSERT_EQ(expr.step_data(1).size(), 2);
    ASSERT_EQ(expr.step_data(1)[0], 30);
    ASSERT_EQ(expr.step_indices(), (std::vector<std::pair<size_t, size_t>>{{0, 2}, {2, 4}}));
}

TEST(ExpressionChecks, view_constructor_defaults_source) {
    // arrange
    const std::vector<double> buffer = {1, 2};
    std::vector<View<double>> steps;
    steps.emplace_back(make_view(buffer, 2, 1));
    // act
    const Expression<double> expr("V1", std::move(steps), "V");
    // assert
    ASSERT_TRUE(expr.source().empty());
}

TEST(ExpressionChecks, move_constructor_transfers_metadata_and_span_steps) {
    // arrange
    std::vector<double> data = {1, 2, 3, 4};
    std::vector<std::span<const double>> steps = {{data.data(), 2}, {data.data() + 2, 2}};
    Expression<double> source("V1", std::move(data), std::move(steps), "V", "R1");
    // act
    Expression<double> dest(std::move(source));
    // assert
    ASSERT_EQ(dest.name(), "V1");
    ASSERT_EQ(dest.unit(), "V");
    ASSERT_EQ(dest.source(), "R1");
    ASSERT_EQ(dest.step_count(), 2);
    ASSERT_EQ(dest.data().size(), 4);
    ASSERT_EQ(dest.data()[0], 1);
    ASSERT_EQ(dest.step_data(1)[0], 3);
}

TEST(ExpressionChecks, move_constructor_preserves_uninitialized_view_steps) {
    // arrange
    const std::vector<double> buffer = {1, 2, 3, 4, 5, 6};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 1));
    steps.emplace_back(make_view(buffer, 3, 1, 3));
    Expression<double> source("V1", std::move(steps), "V");
    // act
    Expression<double> dest(std::move(source));
    const auto data = dest.data();
    // assert
    ASSERT_EQ(dest.step_count(), 2);
    ASSERT_EQ(data.size(), 6);
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[3], 4);
    ASSERT_EQ(data[5], 6);
    ASSERT_EQ(dest.step_data(1).size(), 3);
}

TEST(ExpressionChecks, move_assignment_transfers_metadata_and_data) {
    // arrange
    std::vector<double> data = {1, 2, 3, 4};
    std::vector<std::span<const double>> steps = {{data.data(), 2}, {data.data() + 2, 2}};
    Expression<double> source("V1", std::move(data), std::move(steps), "V", "R1");
    std::vector<double> other_data = {9};
    std::vector<std::span<const double>> other_steps = {{other_data.data(), 1}};
    Expression<double> dest("other", std::move(other_data), std::move(other_steps), "A");
    // act
    dest = std::move(source);
    // assert
    ASSERT_EQ(dest.name(), "V1");
    ASSERT_EQ(dest.unit(), "V");
    ASSERT_EQ(dest.source(), "R1");
    ASSERT_EQ(dest.step_count(), 2);
    ASSERT_EQ(dest.data().size(), 4);
    ASSERT_EQ(dest.data()[0], 1);
    ASSERT_EQ(dest.step_data(1)[0], 3);
}

TEST(ExpressionChecks, transform_initializes_and_transforms_view_step_data) {
    // arrange
    const std::vector<double> buffer = {1, 10, 2, 20, 3, 30};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 2));
    steps.emplace_back(make_view(buffer, 3, 2, 1));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    expr.transform([](double v) { return v * 10.0; });
    const auto data = expr.data();
    const auto step = expr.step_data(1);
    // assert
    ASSERT_EQ(data.size(), 6);
    ASSERT_EQ(data[0], 10.0);
    ASSERT_EQ(data[5], 300.0);
    ASSERT_EQ(step.size(), 3);
    ASSERT_EQ(step[0], 100.0);
    ASSERT_EQ(step[1], 200.0);
    ASSERT_EQ(step[2], 300.0);
}

TEST(ExpressionChecks, transform_scales_complex_view_step_data) {
    // arrange
    const std::vector<std::complex<double>> buffer = {{1, 0}, {2, 0}};
    std::vector<View<std::complex<double>>> steps;
    steps.emplace_back(make_view(buffer, 2, 1));
    Expression<std::complex<double>> expr("I1", std::move(steps), "A");
    // act
    expr.transform([](std::complex<double> v) { return v * 2.0; });
    const auto data = expr.data();
    // assert
    ASSERT_EQ(data.size(), 2);
    ASSERT_EQ(data[0], std::complex<double>(2, 0));
    ASSERT_EQ(data[1], std::complex<double>(4, 0));
}

TEST(ExpressionChecks, empty_view_steps_yield_zero_steps_and_empty_data) {
    // arrange
    std::vector<View<double>> steps;
    // act
    Expression<double> expr("empty", std::move(steps), "V");
    // assert
    ASSERT_EQ(expr.step_count(), 0);
    ASSERT_TRUE(expr.step_indices().empty());
    ASSERT_TRUE(expr.data().empty());
}

TEST(ExpressionChecks, initialize_handles_empty_strided_view_step) {
    // arrange: a strided step with zero elements must not sweep any tile window
    // (the clamped span formula would underflow for count == 0 without a guard)
    const std::vector<double> buffer = {1, 2};
    std::vector<View<double>> steps;
    steps.emplace_back(make_view(buffer, 0, 2));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    // assert: the step survives as a zero-length span contributing no data
    ASSERT_EQ(expr.step_count(), 1);
    ASSERT_TRUE(data.empty());
    ASSERT_EQ(expr.step_indices(), (std::vector<std::pair<size_t, size_t>>{{0, 0}}));
    ASSERT_TRUE(expr.step_data(0).empty());
}

TEST(ExpressionChecks, step_data_throws_when_expression_has_no_steps) {
    // arrange
    std::vector<View<double>> steps;
    Expression<double> expr("empty", std::move(steps), "V");
    // act
    const auto read_step = [&expr]() { (void)expr.step_data(0); };
    // assert
    ASSERT_THROW(read_step(), std::out_of_range);
}

TEST(ExpressionChecks, initialize_concatenates_contiguous_view_steps) {
    // arrange
    const std::vector<double> buffer = {1, 2, 3, 4, 5, 6};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 1));
    steps.emplace_back(make_view(buffer, 3, 1, 3));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    // assert
    ASSERT_EQ(data.size(), 6);
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[1], 2);
    ASSERT_EQ(data[3], 4);
    ASSERT_EQ(data[5], 6);
    ASSERT_EQ(expr.step_data(0).size(), 3);
    ASSERT_EQ(expr.step_data(1).size(), 3);
}

TEST(ExpressionChecks, initialize_extracts_large_stride_view_step) {
    // arrange
    constexpr size_t stride = 8192;
    std::vector<double> buffer(stride * 2, 0.0);
    buffer[0] = 1.0;
    buffer[stride] = 2.0;
    std::vector<View<double>> steps;
    steps.emplace_back(make_view(buffer, 2, stride));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    // assert
    ASSERT_EQ(data.size(), 2);
    ASSERT_EQ(data[0], 1.0);
    ASSERT_EQ(data[1], 2.0);
}

TEST(ExpressionChecks, initialize_handles_strided_step_spanning_multiple_tiles) {
    // arrange
    constexpr size_t count = 4097;
    constexpr size_t stride = 2;
    std::vector<double> buffer(count * stride);
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer[i] = static_cast<double>(i);
    std::vector<View<double>> steps;
    steps.emplace_back(make_view(buffer, count, stride));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    // assert
    ASSERT_EQ(data.size(), count);
    ASSERT_EQ(data[0], 0.0);
    ASSERT_EQ(data[count - 2], 8190.0);
    ASSERT_EQ(data[count - 1], 8192.0);
}

TEST(ExpressionChecks, any_expression_variant_holds_real_expression) {
    // arrange
    std::vector<double> data = {0.0, 1.0};
    std::vector<std::span<const double>> steps = {{data.data(), data.size()}};
    Expression<double> expr("time", std::move(data), std::move(steps), "s");
    // act
    AnyExpression any = std::move(expr);
    // assert
    ASSERT_TRUE(std::holds_alternative<Expression<double>>(any));
}

TEST(ExpressionChecks, any_expression_variant_holds_complex_expression) {
    // arrange
    std::vector<std::complex<double>> data = {{1, 0}, {0, 1}};
    std::vector<std::span<const std::complex<double>>> steps = {{data.data(), data.size()}};
    Expression<std::complex<double>> expr("I1", std::move(data), std::move(steps), "A");
    // act
    AnyExpression any = std::move(expr);
    // assert
    ASSERT_TRUE(std::holds_alternative<Expression<std::complex<double>>>(any));
}

TEST(ExpressionChecks, step_data_as_first_accessor_initializes_from_strided_source) {
    // arrange
    const std::vector<double> buffer = {1, 10, 2, 20, 3, 30};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 2));
    steps.emplace_back(make_view(buffer, 3, 2, 1));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto step = expr.step_data(1);
    const auto slices = expr.step_indices();
    // assert
    ASSERT_EQ(step.size(), 3);
    ASSERT_EQ(step[0], 10);
    ASSERT_EQ(step[1], 20);
    ASSERT_EQ(step[2], 30);
    ASSERT_EQ(slices.size(), 2);
    ASSERT_EQ(slices[0], (std::pair<size_t, size_t>{0, 3}));
    ASSERT_EQ(slices[1], (std::pair<size_t, size_t>{3, 6}));
}

TEST(ExpressionChecks, step_indices_single_step_expression) {
    // arrange
    const std::vector<double> buffer = {1, 2, 3};
    std::vector<View<double>> steps;
    steps.emplace_back(make_view(buffer, 3, 1));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto slices = expr.step_indices();
    // assert
    ASSERT_EQ(slices, (std::vector<std::pair<size_t, size_t>>{{0, 3}}));
}

TEST(ExpressionChecks, step_indices_after_initialize_uses_recomputed_offsets) {
    // arrange
    const std::vector<double> buffer = {1, 10, 2, 20, 3, 30};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 2));
    steps.emplace_back(make_view(buffer, 3, 2, 1));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    (void)expr.data();
    const auto slices = expr.step_indices();
    // assert
    ASSERT_EQ(slices, (std::vector<std::pair<size_t, size_t>>{{0, 3}, {3, 6}}));
}

TEST(ExpressionChecks, step_indices_after_move) {
    // arrange
    const std::vector<double> buffer = {1, 2, 3, 4, 5, 6};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 1));
    steps.emplace_back(make_view(buffer, 3, 1, 3));
    Expression<double> source("V1", std::move(steps), "V");
    // act
    Expression<double> dest(std::move(source));
    const auto slices = dest.step_indices();
    // assert
    ASSERT_EQ(slices, (std::vector<std::pair<size_t, size_t>>{{0, 3}, {3, 6}}));
}

TEST(ExpressionChecks, mixed_contiguous_and_strided_steps_concatenate_in_order) {
    // arrange
    const std::vector<double> buffer = {1, 2, 3, 10, 99, 20, 99, 30};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 1));
    steps.emplace_back(make_view(buffer, 3, 2, 3));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    // assert
    ASSERT_EQ(data.size(), 6);
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[1], 2);
    ASSERT_EQ(data[2], 3);
    ASSERT_EQ(data[3], 10);
    ASSERT_EQ(data[4], 20);
    ASSERT_EQ(data[5], 30);
    ASSERT_EQ(expr.step_data(1)[0], 10);
    ASSERT_EQ(expr.step_indices(), (std::vector<std::pair<size_t, size_t>>{{0, 3}, {3, 6}}));
}

TEST(ExpressionChecks, mixed_strided_and_contiguous_steps_concatenate_in_order) {
    // arrange
    const std::vector<double> buffer = {1, 99, 2, 99, 3, 10, 20, 30};
    std::vector<View<double>> steps;
    steps.reserve(2);
    steps.emplace_back(make_view(buffer, 3, 2));
    steps.emplace_back(make_view(buffer, 3, 1, 5));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    // assert
    ASSERT_EQ(data.size(), 6);
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[1], 2);
    ASSERT_EQ(data[2], 3);
    ASSERT_EQ(data[3], 10);
    ASSERT_EQ(data[4], 20);
    ASSERT_EQ(data[5], 30);
    ASSERT_EQ(expr.step_data(1)[0], 10);
    ASSERT_EQ(expr.step_indices(), (std::vector<std::pair<size_t, size_t>>{{0, 3}, {3, 6}}));
}

TEST(ExpressionChecks, initialize_strided_tile_ending_on_tile_boundary) {
    // arrange
    constexpr size_t count = 4096;
    constexpr size_t stride = 2;
    std::vector<double> buffer(count * stride);
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer[i] = static_cast<double>(i);
    std::vector<View<double>> steps;
    steps.emplace_back(make_view(buffer, count, stride));
    Expression<double> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    // assert
    ASSERT_EQ(data.size(), count);
    ASSERT_EQ(data[0], 0.0);
    ASSERT_EQ(data[count - 1], static_cast<double>((count - 1) * stride));
}

TEST(ExpressionChecks, constructor_from_data_and_empty_step_slices_yields_zero_steps) {
    // arrange
    std::vector<double> data = {1, 2, 3};
    // act
    const std::vector<std::pair<size_t, size_t>> empty_slices{};
    Expression<double> expr("V1", std::move(data), empty_slices, "V");
    // assert: steps are empty but the cached data vector remains accessible
    ASSERT_EQ(expr.step_count(), 0);
    ASSERT_TRUE(expr.step_indices().empty());
    ASSERT_EQ(expr.data().size(), 3);
}

TEST(ExpressionChecks, constructor_from_data_and_gap_slices_pins_step_indices_divergence) {
    // arrange
    std::vector<double> data = {1, 2, 3, 4, 5, 6, 7};
    const std::vector<std::pair<size_t, size_t>> slices = {{0, 2}, {5, 7}};
    // act
    Expression<double> expr("V1", std::move(data), slices, "V");
    // assert: step_indices() recomputes cumulative offsets from step sizes, ignoring gaps
    ASSERT_EQ(expr.step_count(), 2);
    const auto indices = expr.step_indices();
    ASSERT_EQ(indices.size(), 2);
    ASSERT_EQ(indices[0], (std::pair<size_t, size_t>{0, 2}));
    ASSERT_EQ(indices[1], (std::pair<size_t, size_t>{2, 4}));
    const auto step = expr.step_data(1);
    ASSERT_EQ(step.size(), 2);
    ASSERT_EQ(step[0], 6);
    ASSERT_EQ(step[1], 7);
}

TEST(ExpressionChecks, constructor_from_contiguous_external_view_copies_data) {
    // arrange
    const std::vector<double> buffer = {1, 2, 3, 4, 5, 6};
    View<double> view(buffer.data(), 6, 1);
    const std::vector<std::pair<size_t, size_t>> slices = {{0, 3}, {3, 6}};
    // act
    Expression<double> expr("V1", std::move(view), slices, "V");
    // assert
    ASSERT_EQ(expr.step_count(), 2);
    const auto data = expr.data();
    ASSERT_EQ(data.size(), 6);
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[5], 6);
    ASSERT_EQ(expr.step_data(1)[0], 4);
}

TEST(ExpressionChecks, constructor_from_view_with_empty_slices) {
    // arrange
    const std::vector<double> buffer = {1, 2, 3, 4};
    View<double> view(buffer.data(), 4, 1);
    const std::vector<std::pair<size_t, size_t>> empty_slices{};
    // act
    Expression<double> expr("V1", std::move(view), empty_slices, "V");
    // assert
    ASSERT_EQ(expr.step_count(), 0);
    ASSERT_TRUE(expr.step_indices().empty());
}

TEST(ExpressionChecks, constructor_from_owning_view_with_empty_view) {
    // arrange
    std::vector<double> owned = {10, 20, 30, 40};
    View<double> view(std::move(owned));
    view = View<double>(std::vector<double>{1, 2});
    // act
    const std::vector<std::pair<size_t, size_t>> slices = {{0, 1}, {1, 2}};
    Expression<double> expr("V1", std::move(view), slices, "V");
    // assert
    ASSERT_EQ(expr.step_count(), 2);
    const auto data = expr.data();
    ASSERT_EQ(data.size(), 2);
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[1], 2);
}

TEST(ExpressionChecks, constructor_from_owning_view_with_gap_slices) {
    // arrange
    View<double> view(std::vector<double>{10, 20, 30, 40});
    const std::vector<std::pair<size_t, size_t>> slices = {{0, 1}, {3, 4}};
    // act
    Expression<double> expr("V1", std::move(view), slices, "V");
    // assert
    ASSERT_EQ(expr.step_count(), 2);
    const auto data = expr.data();
    ASSERT_EQ(data.size(), 4);
    ASSERT_EQ(data[0], 10);
    ASSERT_EQ(data[3], 40);
    ASSERT_EQ(expr.step_data(0)[0], 10);
    ASSERT_EQ(expr.step_data(1)[0], 40);
    const auto indices = expr.step_indices();
    ASSERT_EQ(indices[0], (std::pair<size_t, size_t>{0, 1}));
    ASSERT_EQ(indices[1], (std::pair<size_t, size_t>{1, 2}));
}

TEST(ExpressionChecks, any_expression_get_exercises_real_expression_accessors) {
    // arrange
    std::vector<double> data = {0.0, 1.0, 2.0, 3.0};
    std::vector<std::span<const double>> steps = {{data.data(), 2}, {data.data() + 2, 2}};
    Expression<double> expr("time", std::move(data), std::move(steps), "s");
    // act
    AnyExpression any = std::move(expr);
    auto& held = std::get<Expression<double>>(any);
    const auto step_count = held.step_count();
    const auto data_span = held.data();
    held.transform([](double v) { return v * 2.0; });
    // assert
    ASSERT_EQ(step_count, 2);
    ASSERT_EQ(data_span.size(), 4);
    ASSERT_EQ(held.data()[0], 0.0);
    ASSERT_EQ(held.data()[1], 2.0);
    ASSERT_EQ(held.data()[2], 4.0);
    ASSERT_EQ(held.data()[3], 6.0);
}

TEST(ExpressionChecks, any_expression_get_exercises_complex_expression_accessors) {
    // arrange
    std::vector<std::complex<double>> data = {{1, 0}, {2, 0}};
    std::vector<std::span<const std::complex<double>>> steps = {{data.data(), 2}};
    Expression<std::complex<double>> expr("I1", std::move(data), std::move(steps), "A");
    // act
    AnyExpression any = std::move(expr);
    auto& held = std::get<Expression<std::complex<double>>>(any);
    const auto step_count = held.step_count();
    const auto step = held.step_data(0);
    // assert
    ASSERT_EQ(step_count, 1);
    ASSERT_EQ(step.size(), 2);
    ASSERT_EQ(held.data()[1], std::complex<double>(2, 0));
}

TEST(ExpressionChecks, complex_strided_initialize_with_stride_two) {
    // arrange
    const std::vector<std::complex<double>> buffer = {{1, 1}, {10, 10}, {2, 2}, {20, 20}, {3, 3}, {30, 30}};
    std::vector<View<std::complex<double>>> steps;
    steps.emplace_back(make_view(buffer, 3, 2));
    Expression<std::complex<double>> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    // assert
    ASSERT_EQ(data.size(), 3);
    ASSERT_EQ(data[0], std::complex<double>(1, 1));
    ASSERT_EQ(data[1], std::complex<double>(2, 2));
    ASSERT_EQ(data[2], std::complex<double>(3, 3));
}

TEST(ExpressionChecks, complex_strided_initialize_with_large_stride) {
    // arrange
    constexpr size_t stride = 8192;
    std::vector<std::complex<double>> buffer(stride * 2, std::complex<double>(0, 0));
    buffer[0] = std::complex<double>(1, 1);
    buffer[stride] = std::complex<double>(2, 2);
    std::vector<View<std::complex<double>>> steps;
    steps.emplace_back(make_view(buffer, 2, stride));
    Expression<std::complex<double>> expr("V1", std::move(steps), "V");
    // act
    const auto data = expr.data();
    // assert
    ASSERT_EQ(data.size(), 2);
    ASSERT_EQ(data[0], std::complex<double>(1, 1));
    ASSERT_EQ(data[1], std::complex<double>(2, 2));
}

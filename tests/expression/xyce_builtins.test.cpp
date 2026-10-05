#include <cmath>
#include <complex>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "expression/xyce_evaluator.h"

namespace
{
    // keep scalar values explicit in the assertions
    XyceValue expression_value(double value) { return value; }

    // keep real vector values explicit in the assertions
    XyceValue expression_value(std::vector<double>&& values) { return std::make_shared<View<double>>(std::move(values)); }

    // keep complex values explicit in the assertions
    XyceValue expression_value(const std::complex<double>& value) { return value; }

    // keep complex vector values explicit in the assertions
    XyceValue expression_value(std::vector<std::complex<double>>&& values) { return std::make_shared<View<std::complex<double>>>(std::move(values)); }

    // read the first scalar value from a variant for compact checks
    template <typename T>
    T scalar(const XyceValue& value) {
        return scalar_value<T>(value);
    }

    // normalize any value to a real vector for compact checks
    std::vector<double> as_real_vector(const XyceValue& value) {
        auto view = to_real_vector(value);
        return std::vector<double>(view->begin(), view->end());
    }

    // normalize any value to a complex vector for compact checks
    std::vector<std::complex<double>> as_complex_vector(const XyceValue& value) {
        auto view = to_complex_vector(value);
        return std::vector<std::complex<double>>(view->begin(), view->end());
    }
} // namespace

TEST(XyceBuiltinsChecks, constants_match_expected_values) {
    // arrange
    const auto& constants = BUILTIN_CONSTANTS;
    // act
    const auto pi = scalar<double>(constants.at("pi"));
    // assert
    ASSERT_NEAR(pi, std::acos(-1.0), 1e-12);
    ASSERT_NEAR(scalar<double>(constants.at("e")), std::exp(1.0), 1e-12);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("meg")), 1e6);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("k")), 1e3);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("m")), 1e-3);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("u")), 1e-6);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("n")), 1e-9);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("p")), 1e-12);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("f")), 1e-15);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("g")), 1e9);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("t")), 1e12);
    ASSERT_NEAR(scalar<double>(constants.at("mil")), 25.4e-6, 1e-20);
    ASSERT_EQ(std::get<std::complex<double>>(constants.at("j")), std::complex<double>(0.0, 1.0));
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("mho")), 1.0);
    ASSERT_DOUBLE_EQ(scalar<double>(constants.at("s")), 1.0);
}

TEST(XyceBuiltinsChecks, core_builtin_functions_work) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto log_value = scalar<double>(functions.at("log")({expression_value(100.0)}));
    // assert
    ASSERT_DOUBLE_EQ(log_value, 2.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("log10")({expression_value(1000.0)})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("ln")({expression_value(std::exp(1.0))})), 1.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("abs")({expression_value(-3.0)})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sqrt")({expression_value(9.0)})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("db")({expression_value(10.0)})), 20.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("real")({expression_value(std::complex<double>(3.0, 4.0))})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("imag")({expression_value(std::complex<double>(3.0, 4.0))})), 4.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("phase")({expression_value(std::complex<double>(0.0, 1.0))})), 90.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sin")({expression_value(0.0)})), 0.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("cos")({expression_value(0.0)})), 1.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("tan")({expression_value(0.0)})), 0.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("atan2")({expression_value(1.0), expression_value(1.0)})), std::atan2(1.0, 1.0));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sgn")({expression_value(-3.0)})), -1.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sign")({expression_value(-3.0), expression_value(2.0)})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("uramp")({expression_value(-5.0)})), 0.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("stp")({expression_value(1.0)})), 1.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("round")({expression_value(2.7)})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("nint")({expression_value(1.5)})), 2.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("floor")({expression_value(2.9)})), 2.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("ceil")({expression_value(2.1)})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("int")({expression_value(-2.9)})), -2.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("pow")({expression_value(2.0), expression_value(10.0)})), 1024.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("pwr")({expression_value(-2.0), expression_value(3.0)})), -8.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("pwrs")({expression_value(-2.0), expression_value(3.0)})), -8.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("fmod")({expression_value(10.0), expression_value(3.0)})), 1.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("min")({expression_value(3.0), expression_value(1.0), expression_value(2.0)})), 1.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("max")({expression_value(3.0), expression_value(1.0), expression_value(2.0)})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("limit")({expression_value(5.0), expression_value(1.0), expression_value(10.0)})), 5.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("if")({expression_value(1.0), expression_value(10.0), expression_value(20.0)})), 10.0);
}

TEST(XyceBuiltinsChecks, number_suffixes_match_expected_multipliers) {
    // arrange
    const auto& suffixes = NUMBER_SUFFIXES;
    // act
    const auto tera = suffixes.at("T");
    // assert
    ASSERT_DOUBLE_EQ(tera, 1e12);
    ASSERT_DOUBLE_EQ(suffixes.at("G"), 1e9);
    ASSERT_DOUBLE_EQ(suffixes.at("MEG"), 1e6);
    ASSERT_DOUBLE_EQ(suffixes.at("K"), 1e3);
    ASSERT_DOUBLE_EQ(suffixes.at("M"), 1e-3);
    ASSERT_DOUBLE_EQ(suffixes.at("U"), 1e-6);
    ASSERT_DOUBLE_EQ(suffixes.at("N"), 1e-9);
    ASSERT_DOUBLE_EQ(suffixes.at("P"), 1e-12);
    ASSERT_DOUBLE_EQ(suffixes.at("F"), 1e-15);
    ASSERT_DOUBLE_EQ(suffixes.at("MIL"), 25.4e-6);
}

TEST(XyceBuiltinsChecks, unregistered_builtin_functions_work) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto asin_value = scalar<double>(functions.at("asin")({expression_value(1.0)}));
    // assert
    ASSERT_NEAR(asin_value, std::acos(-1.0) / 2.0, 1e-12);
    ASSERT_NEAR(scalar<double>(functions.at("acos")({expression_value(0.5)})), std::acos(0.5), 1e-12);
    ASSERT_NEAR(scalar<double>(functions.at("atan")({expression_value(1.0)})), std::atan(1.0), 1e-12);
    ASSERT_NEAR(scalar<double>(functions.at("arctan")({expression_value(1.0)})), std::atan(1.0), 1e-12);
    ASSERT_NEAR(scalar<double>(functions.at("asinh")({expression_value(1.0)})), std::asinh(1.0), 1e-12);
    ASSERT_NEAR(scalar<double>(functions.at("acosh")({expression_value(2.0)})), std::acosh(2.0), 1e-12);
    ASSERT_NEAR(scalar<double>(functions.at("atanh")({expression_value(0.5)})), std::atanh(0.5), 1e-12);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sinh")({expression_value(0.0)})), 0.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("cosh")({expression_value(0.0)})), 1.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("tanh")({expression_value(0.0)})), 0.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("exp")({expression_value(1.0)})), std::exp(1.0));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sqr")({expression_value(3.0)})), 9.0);
    ASSERT_EQ(std::get<std::complex<double>>(functions.at("conj")({expression_value(3.0)})), std::complex<double>(3.0, 0.0));
}

TEST(XyceBuiltinsChecks, builtin_aliases_equivalent_to_primary_names) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    const auto complex_arg = std::complex<double>(3.0, 4.0);
    // act
    const auto img_value = scalar<double>(functions.at("img")({expression_value(complex_arg)}));
    // assert
    ASSERT_DOUBLE_EQ(img_value, scalar<double>(functions.at("imag")({expression_value(complex_arg)})));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("m")({expression_value(complex_arg)})), scalar<double>(functions.at("abs")({expression_value(complex_arg)})));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("mag")({expression_value(complex_arg)})), scalar<double>(functions.at("abs")({expression_value(complex_arg)})));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("ph")({expression_value(complex_arg)})), scalar<double>(functions.at("phase")({expression_value(complex_arg)})));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("r")({expression_value(complex_arg)})), scalar<double>(functions.at("real")({expression_value(complex_arg)})));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("re")({expression_value(complex_arg)})), scalar<double>(functions.at("real")({expression_value(complex_arg)})));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("arctan")({expression_value(1.0)})), scalar<double>(functions.at("atan")({expression_value(1.0)})));
}

TEST(XyceBuiltinsChecks, complex_operations_preserve_complex_scalars) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto sqrt_negative = std::get<std::complex<double>>(functions.at("sqrt")({expression_value(-1.0)}));
    const auto sqrt_complex = std::get<std::complex<double>>(functions.at("sqrt")({expression_value(std::complex<double>(1.0, 1.0))}));
    const auto ln_negative = std::get<std::complex<double>>(functions.at("ln")({expression_value(-1.0)}));
    const auto expected_sqrt_complex = std::sqrt(std::complex<double>(1.0, 1.0));
    // assert
    ASSERT_NEAR(sqrt_negative.real(), 0.0, 1e-12);
    ASSERT_NEAR(sqrt_negative.imag(), 1.0, 1e-12);
    ASSERT_NEAR(sqrt_complex.real(), expected_sqrt_complex.real(), 1e-12);
    ASSERT_NEAR(sqrt_complex.imag(), expected_sqrt_complex.imag(), 1e-12);
    ASSERT_NEAR(ln_negative.real(), 0.0, 1e-12);
    ASSERT_NEAR(ln_negative.imag(), std::acos(-1.0), 1e-12);
}

TEST(XyceBuiltinsChecks, builtin_branch_edges_match_scenarios) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto sgn_positive = scalar<double>(functions.at("sgn")({expression_value(5.0)}));
    // assert
    ASSERT_DOUBLE_EQ(sgn_positive, 1.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sgn")({expression_value(0.0)})), 0.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sign")({expression_value(3.0), expression_value(-2.0)})), -3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sign")({expression_value(3.0), expression_value(0.0)})), 0.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("sign")({expression_value(-3.0), expression_value(2.0)})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("stp")({expression_value(0.0)})), 0.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("stp")({expression_value(-1.0)})), 0.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("uramp")({expression_value(3.0)})), 3.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("db")({expression_value(-10.0)})), 20.0);
    ASSERT_TRUE(std::isinf(scalar<double>(functions.at("db")({expression_value(0.0)}))));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("if")({expression_value(0.0), expression_value(10.0), expression_value(20.0)})), 20.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("min")({expression_value(5.0)})), 5.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("max")({expression_value(5.0)})), 5.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("pwrs")({expression_value(2.0), expression_value(3.0)})), 8.0);
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("pwr")({expression_value(2.0), expression_value(3.0)})), 8.0);
    ASSERT_TRUE(std::isnan(scalar<double>(functions.at("fmod")({expression_value(10.0), expression_value(0.0)}))));
    ASSERT_EQ(std::get<std::complex<double>>(functions.at("conj")({expression_value(std::complex<double>(3.0, 4.0))})), std::complex<double>(3.0, -4.0));
    ASSERT_DOUBLE_EQ(scalar<double>(functions.at("abs")({expression_value(std::complex<double>(3.0, 4.0))})), 5.0);
    ASSERT_NEAR(scalar<double>(functions.at("atan2")({expression_value(0.0), expression_value(1.0)})), 0.0, 1e-12);
    ASSERT_NEAR(scalar<double>(functions.at("atan2")({expression_value(-1.0), expression_value(-1.0)})), std::atan2(-1.0, -1.0), 1e-12);
    ASSERT_NEAR(scalar<double>(functions.at("atan2")({expression_value(0.0), expression_value(-1.0)})), std::atan2(0.0, -1.0), 1e-12);
}

TEST(XyceBuiltinsChecks, builtin_function_errors_match_scenarios) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto invalid_abs_args = std::vector<XyceValue>{expression_value(1.0), expression_value(2.0)};
    // assert
    ASSERT_THROW(functions.at("abs")(invalid_abs_args), std::invalid_argument);
    ASSERT_THROW(functions.at("sign")({expression_value(1.0)}), std::invalid_argument);
    ASSERT_THROW(functions.at("fmod")({expression_value(1.0)}), std::invalid_argument);
    ASSERT_THROW(functions.at("min")({}), std::invalid_argument);
    ASSERT_THROW(functions.at("max")({}), std::invalid_argument);
    ASSERT_THROW(functions.at("ddt")({expression_value(1.0)}), std::logic_error);
    ASSERT_THROW(functions.at("sdt")({expression_value(1.0)}), std::logic_error);
}

TEST(XyceBuiltinsChecks, real_only_builtins_reject_complex_arguments) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto complex_scalar = expression_value(std::complex<double>(3.0, 4.0));
    const auto complex_vector = expression_value(std::vector<std::complex<double>>{{3.0, 4.0}});
    // assert
    ASSERT_THROW(functions.at("tan")({complex_scalar}), std::invalid_argument);
    ASSERT_THROW(functions.at("tan")({complex_vector}), std::invalid_argument);
    ASSERT_THROW(functions.at("exp")({complex_scalar}), std::invalid_argument);
    ASSERT_THROW(functions.at("pwr")({complex_scalar, expression_value(2.0)}), std::invalid_argument);
    ASSERT_THROW(functions.at("fmod")({expression_value(1.0), complex_scalar}), std::invalid_argument);
    ASSERT_THROW(functions.at("sign")({complex_scalar, expression_value(1.0)}), std::invalid_argument);
    ASSERT_THROW(functions.at("if")({complex_scalar, expression_value(1.0), expression_value(2.0)}), std::invalid_argument);
    ASSERT_NO_THROW(functions.at("db")({complex_scalar}));
    ASSERT_NO_THROW(functions.at("abs")({complex_scalar}));
    ASSERT_NO_THROW(functions.at("phase")({complex_scalar}));
}

TEST(XyceBuiltinsChecks, builtin_abs_applies_elementwise_to_vectors) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto real_result = functions.at("abs")({expression_value(std::vector<double>{-3.0, 4.0})});
    const auto complex_result = functions.at("abs")({expression_value(std::vector<std::complex<double>>{{3.0, 4.0}, {5.0, 12.0}})});
    // assert
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<double>>>(real_result));
    ASSERT_EQ(as_real_vector(real_result), (std::vector<double>{3.0, 4.0}));
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<double>>>(complex_result));
    ASSERT_EQ(as_real_vector(complex_result), (std::vector<double>{5.0, 13.0}));
}

TEST(XyceBuiltinsChecks, builtin_real_imag_phase_apply_to_vectors) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    const auto complex_vec = std::vector<std::complex<double>>{{1.0, 2.0}, {3.0, 4.0}};
    // act
    const auto real_result = functions.at("real")({expression_value(std::vector<std::complex<double>>(complex_vec))});
    const auto real_pass = functions.at("real")({expression_value(std::vector<double>{5.0, 6.0})});
    const auto imag_complex = functions.at("imag")({expression_value(std::vector<std::complex<double>>(complex_vec))});
    const auto imag_real = functions.at("imag")({expression_value(std::vector<double>{1.0, 2.0})});
    const auto phase_real = functions.at("phase")({expression_value(std::vector<double>{1.0, -1.0, 0.0})});
    const auto phase_complex = functions.at("phase")({expression_value(std::vector<std::complex<double>>{{1.0, 1.0}, {-1.0, 0.0}, {0.0, 0.0}})});
    // assert
    ASSERT_EQ(as_real_vector(real_result), (std::vector<double>{1.0, 3.0}));
    ASSERT_EQ(as_real_vector(real_pass), (std::vector<double>{5.0, 6.0}));
    ASSERT_EQ(as_real_vector(imag_complex), (std::vector<double>{2.0, 4.0}));
    ASSERT_EQ(as_real_vector(imag_real), (std::vector<double>{0.0, 0.0}));
    ASSERT_EQ(as_real_vector(phase_real), (std::vector<double>{0.0, 180.0, 0.0}));
    const auto phase_values = as_real_vector(phase_complex);
    ASSERT_EQ(phase_values.size(), 3U);
    ASSERT_NEAR(phase_values[0], 45.0, 1e-12);
    ASSERT_NEAR(phase_values[1], 180.0, 1e-12);
    ASSERT_NEAR(phase_values[2], 0.0, 1e-12);
}

TEST(XyceBuiltinsChecks, builtin_sqrt_ln_map_across_vectors) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto sqrt_real = functions.at("sqrt")({expression_value(std::vector<double>{1.0, 4.0, 9.0})});
    const auto sqrt_negative = functions.at("sqrt")({expression_value(std::vector<double>{-1.0, -4.0})});
    const auto sqrt_mixed = functions.at("sqrt")({expression_value(std::vector<std::complex<double>>{{-1.0, 0.0}, {1.0, 0.0}})});
    const auto sqrt_all_real = functions.at("sqrt")({expression_value(std::vector<std::complex<double>>{{1.0, 0.0}, {4.0, 0.0}})});
    const auto ln_real = functions.at("ln")({expression_value(std::vector<double>{1.0, std::exp(1.0)})});
    const auto ln_complex = functions.at("ln")({expression_value(std::vector<std::complex<double>>{{1.0, 1.0}})});
    const auto expected_ln = std::log(std::complex<double>(1.0, 1.0));
    // assert
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<double>>>(sqrt_real));
    ASSERT_EQ(as_real_vector(sqrt_real), (std::vector<double>{1.0, 2.0, 3.0}));
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<std::complex<double>>>>(sqrt_negative));
    const auto sqrt_negative_values = as_complex_vector(sqrt_negative);
    ASSERT_NEAR(sqrt_negative_values[0].real(), 0.0, 1e-12);
    ASSERT_NEAR(sqrt_negative_values[0].imag(), 1.0, 1e-12);
    ASSERT_NEAR(sqrt_negative_values[1].real(), 0.0, 1e-12);
    ASSERT_NEAR(sqrt_negative_values[1].imag(), 2.0, 1e-12);
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<std::complex<double>>>>(sqrt_mixed));
    const auto sqrt_mixed_values = as_complex_vector(sqrt_mixed);
    ASSERT_NEAR(sqrt_mixed_values[0].real(), 0.0, 1e-12);
    ASSERT_NEAR(sqrt_mixed_values[0].imag(), 1.0, 1e-12);
    ASSERT_NEAR(sqrt_mixed_values[1].real(), 1.0, 1e-12);
    ASSERT_NEAR(sqrt_mixed_values[1].imag(), 0.0, 1e-12);
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<double>>>(sqrt_all_real));
    const auto ln_real_values = as_real_vector(ln_real);
    ASSERT_EQ(ln_real_values.size(), 2U);
    ASSERT_NEAR(ln_real_values[0], 0.0, 1e-12);
    ASSERT_NEAR(ln_real_values[1], 1.0, 1e-12);
    const auto ln_complex_values = as_complex_vector(ln_complex);
    ASSERT_EQ(ln_complex_values.size(), 1U);
    ASSERT_NEAR(ln_complex_values[0].real(), expected_ln.real(), 1e-12);
    ASSERT_NEAR(ln_complex_values[0].imag(), expected_ln.imag(), 1e-12);
}

TEST(XyceBuiltinsChecks, builtin_db_tan_map_across_vectors) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto db_real = functions.at("db")({expression_value(std::vector<double>{10.0, 100.0})});
    const auto db_complex = functions.at("db")({expression_value(std::vector<std::complex<double>>{{3.0, 4.0}})});
    const auto db_complex_scalar = functions.at("db")({expression_value(std::complex<double>(3.0, 4.0))});
    const auto tan_real = functions.at("tan")({expression_value(std::vector<double>{0.0, 0.0})});
    // assert
    ASSERT_EQ(as_real_vector(db_real), (std::vector<double>{20.0, 40.0}));
    const auto db_complex_values = as_real_vector(db_complex);
    ASSERT_EQ(db_complex_values.size(), 1U);
    ASSERT_NEAR(db_complex_values[0], 20.0 * std::log10(5.0), 1e-12);
    ASSERT_NEAR(scalar<double>(db_complex_scalar), 20.0 * std::log10(5.0), 1e-12);
    ASSERT_EQ(as_real_vector(tan_real), (std::vector<double>{0.0, 0.0}));
}

TEST(XyceBuiltinsChecks, builtin_if_broadcasts_over_vectors) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto vector_condition = functions.at("if")({expression_value(std::vector<double>{1.0, 0.0, 1.0}), expression_value(std::vector<double>{10.0, 20.0, 30.0}), expression_value(std::vector<double>{40.0, 50.0, 60.0})});
    const auto scalar_branches = functions.at("if")({expression_value(std::vector<double>{1.0, 0.0}), expression_value(10.0), expression_value(20.0)});
    const auto scalar_condition = functions.at("if")({expression_value(0.0), expression_value(std::vector<double>{10.0, 20.0}), expression_value(std::vector<double>{30.0, 40.0})});
    // assert
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<double>>>(vector_condition));
    ASSERT_EQ(as_real_vector(vector_condition), (std::vector<double>{10.0, 50.0, 30.0}));
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<double>>>(scalar_branches));
    ASSERT_EQ(as_real_vector(scalar_branches), (std::vector<double>{10.0, 20.0}));
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<double>>>(scalar_condition));
    ASSERT_EQ(as_real_vector(scalar_condition), (std::vector<double>{30.0}));
}

TEST(XyceBuiltinsChecks, builtin_if_preserves_complex_branches) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // act
    const auto vector_condition = functions.at("if")({expression_value(std::vector<double>{1.0, 0.0}), expression_value(std::vector<std::complex<double>>{{1.0, 2.0}, {3.0, 4.0}}), expression_value(std::vector<std::complex<double>>{{5.0, 6.0}, {7.0, 8.0}})});
    const auto mixed_branch = functions.at("if")({expression_value(std::vector<double>{0.0, 1.0}), expression_value(std::vector<double>{10.0, 20.0}), expression_value(std::vector<std::complex<double>>{{1.0, 2.0}, {3.0, 4.0}})});
    // assert
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<std::complex<double>>>>(vector_condition));
    ASSERT_EQ(as_complex_vector(vector_condition), (std::vector<std::complex<double>>{{1.0, 2.0}, {7.0, 8.0}}));
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<View<std::complex<double>>>>(mixed_branch));
    ASSERT_EQ(as_complex_vector(mixed_branch), (std::vector<std::complex<double>>{{1.0, 2.0}, {20.0, 0.0}}));
}

TEST(XyceBuiltinsChecks, builtin_arity_mismatches_throw) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // assert
    ASSERT_THROW(functions.at("sqrt")({expression_value(1.0), expression_value(2.0)}), std::invalid_argument);
    ASSERT_THROW(functions.at("limit")({expression_value(1.0), expression_value(2.0)}), std::invalid_argument);
    ASSERT_THROW(functions.at("pow")({expression_value(1.0)}), std::invalid_argument);
}

TEST(XyceBuiltinsChecks, all_builtins_reject_empty_argument_lists) {
    // arrange
    const auto& functions = BUILTIN_FUNCTIONS;
    // assert
    for (const auto& [name, function] : functions) {
        if (name == "ddt" || name == "sdt")
            continue;
        EXPECT_THROW(function({}), std::invalid_argument) << "Function '" << name << "' should reject empty arguments";
    }
}

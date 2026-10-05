#pragma once

#include <complex>
#include <string>
#include <variant>

#include "../core/view.h"
#include "expression.h"

using XyceValue = std::variant<double, std::complex<double>, std::shared_ptr<View<double>>, std::shared_ptr<View<std::complex<double>>>>;

XyceValue from_expression(AnyExpression& expression);

bool is_scalar(const XyceValue& value);

bool is_vector(const XyceValue& value);

bool is_real(const XyceValue& value);

bool is_complex(const XyceValue& value);

void require_real_argument(const std::string& name, const XyceValue& value);

template <typename T>
T scalar_value(const XyceValue& value) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702) // unreachable code — false positive in second if constexpr's else
#endif
    // processor
    auto l = []<typename T0>(T0& arg [[maybe_unused]]) -> T {
        // actual parameter type
        using TX = std::decay_t<T0>;
        // real scalar
        if constexpr (std::is_same_v<T, double>) {
            // scalar double
            if constexpr (std::is_same_v<TX, double>) {
                return arg;
            }
            else if constexpr (std::is_same_v<TX, std::complex<double>>) {
                return arg.real();
            }
            else if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
                return arg->operator[](0);
            }
            else if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                return arg->operator[](0).real();
            }
            else {
                // not possible value type
                throw std::invalid_argument("unsupported type");
            }
        }
        // complex scalar
        if constexpr (std::is_same_v<T, std::complex<double>>) {
            // complex scalar
            if constexpr (std::is_same_v<TX, double>) {
                return T(arg, 0.0);
            }
            // scalar complex
            else if constexpr (std::is_same_v<TX, std::complex<double>>) {
                return arg;
            }
            // vector<double>
            else if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
                return T(arg->operator[](0), 0.0);
            }
            else if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                return arg->operator[](0);
            }
            else {
                // not possible value type
                throw std::invalid_argument("unsupported type");
            }
        }
        else {
            // not possible value type
            throw std::invalid_argument("unsupported type");
        }
    };
    // exit
    return std::visit(l, value);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

std::shared_ptr<View<double>> to_real_vector(const XyceValue& value);

std::shared_ptr<View<std::complex<double>>> to_complex_vector(const XyceValue& value);

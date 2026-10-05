#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <vector>

#ifdef _MSC_VER
#pragma warning(disable : 4702)
#endif

#include "../core/view.h"
#include "xyce_evaluator.h"
#include "xyce_value.h"

namespace
{
    // fail when a builtin receives the wrong arity
    void expect_arity(const std::string& name, const std::vector<XyceValue>& args, const size_t count) {
        if (args.size() != count) {
            throw std::invalid_argument("Function '" + name + "' expects " + std::to_string(count) + " arguments, got " + std::to_string(args.size()));
        }
    }

    // fail when a builtin receives too few arguments
    void expect_min_arity(const std::string& name, const std::vector<XyceValue>& args, const size_t count) {
        if (args.size() < count) {
            throw std::invalid_argument("Function '" + name + "' expects at least " + std::to_string(count) + " arguments, got " + std::to_string(args.size()));
        }
    }

    // collapse a complex scalar to a real when possible
    XyceValue make_scalar(const std::complex<double>& value) {
        // convert to real if imaginary part is very small
        if (std::abs(value.imag()) < 1e-15) {
            // real part only
            return value.real();
        }
        return value;
    }

    // collapse a complex vector to a real vector when possible
    XyceValue make_vector(std::shared_ptr<View<std::complex<double>>> values) {
        // execution flag
        bool all_real = true;
        // lop values
        for (const auto& value : *values) {
            // check we can eliminate the imaginary part
            if (std::abs(value.imag()) >= 1e-15) {
                // reset flag and exit loop
                all_real = false;
                break;
            }
        }
        // we can convert vector
        if (all_real) {
            // vector
            std::vector<double> out;
            // allocate space
            out.reserve(values->size());
            // loop view, append real values
            for (const auto& v : *values)
                out.emplace_back(v.real());
            // exit
            return std::make_shared<View<double>>(std::move(out));
        }
        return {values};
    }

    // apply a unary real function across a scalar or vector
    XyceValue map_unary_real(const std::string& name, const XyceValue& value, const std::function<double(double)>& fn) {
        // a real function has no complex value to apply to
        require_real_argument(name, value);
        // normalize the argument to a real vector, promoting a scalar
        const auto operand = to_real_vector(value);
        // apply function, returns vector or scalar
        if (is_scalar(value))
            return fn(operand->operator[](0));
        // output vector
        std::vector<double> out;
        // allocate space
        out.reserve(operand->size());
        // append mapped values
        std::ranges::transform(operand->begin(), operand->end(), std::back_inserter(out), fn);
        // exit
        return std::make_shared<View<double>>(std::move(out));
    }

    // extract a real-only builtin argument, failing on a complex value
    double real_argument(const std::string& name, const XyceValue& value) {
        // reject a complex argument, its imaginary part would be discarded below
        require_real_argument(name, value);
        // exit
        return scalar_value<double>(value);
    }

    // apply a unary complex function across a scalar or vector
    XyceValue map_unary_complex(const XyceValue& value, const std::function<std::complex<double>(std::complex<double>)>& fn) {
        // processor
        auto l = [&fn]<typename T0>(T0& arg) -> XyceValue {
            // actual parameter type
            using TX = std::decay_t<T0>;
            // double
            if constexpr (std::is_same_v<TX, double>) {
                // apply fn
                return make_scalar(fn(std::complex<double>(arg, 0.0)));
            }
            // complex
            if constexpr (std::is_same_v<TX, std::complex<double>>) {
                // apply fn
                return make_scalar(fn(arg));
            }
            // View<double>
            if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
                // vector
                std::vector<std::complex<double>> out;
                // allocate space
                out.reserve(arg->size());
                // append mapped values
                std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [&fn](auto v) { return fn(std::complex<double>(v, 0.0)); });
                // exit
                return make_vector(std::make_shared<View<std::complex<double>>>(std::move(out)));
            }
            // View<complex>
            if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                // vector
                std::vector<std::complex<double>> out;
                // allocate space
                out.reserve(arg->size());
                // append mapped values
                std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), fn);
                // exit
                return make_vector(std::make_shared<View<std::complex<double>>>(std::move(out)));
            }
            // not possible value type
            throw std::invalid_argument("unsupported type");
        };
        // exit
        return std::visit(l, value);
    }

    // absolute value builtin
    XyceValue builtin_abs(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("abs", args, 1);
        // processor
        auto l = []<typename T0>(T0& arg) -> XyceValue {
            // actual parameter type
            using TX = std::decay_t<T0>;
            // View<double>
            if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
                // vector
                std::vector<double> out;
                // allocate space
                out.reserve(arg->size());
                // append absolute values
                std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto v) { return std::abs(v); });
                // exit
                return std::make_shared<View<double>>(std::move(out));
            }
            // vector<complex>
            else if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                // vector
                std::vector<double> out;
                // allocate space
                out.reserve(arg->size());
                // append absolute values
                std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto v) { return std::abs(v); });
                // exit
                return std::make_shared<View<double>>(std::move(out));
            }
            // scalar
            else
                return std::abs(arg);
        };
        // exit
        return std::visit(l, args[0]);
    }

    // square root builtin
    XyceValue builtin_sqrt(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("sqrt", args, 1);
        // apply function, returns vector or scalar
        return map_unary_complex(args[0], [](const std::complex<double> value) { return std::sqrt(value); });
    }

    // base-10 logarithm builtin
    XyceValue builtin_log10(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("log10", args, 1);
        // apply function, returns vector or scalar
        return map_unary_complex(args[0], [](const std::complex<double> value) { return std::log10(value); });
    }

    // Xyce log maps to base-10 log
    XyceValue builtin_log(const std::vector<XyceValue>& args) { return builtin_log10(args); }

    // natural logarithm builtin
    XyceValue builtin_ln(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("ln", args, 1);
        // apply function, returns vector or scalar
        return map_unary_complex(args[0], [](const std::complex<double> value) { return std::log(value); });
    }

    // dB conversion builtin
    XyceValue builtin_db(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("db", args, 1);
        // apply function to the argument magnitude, returns vector or scalar
        return map_unary_complex(args[0], [](const std::complex<double> value) { return 20.0 * std::log10(std::abs(value)); });
    }

    // real part builtin
    XyceValue builtin_real(const std::vector<XyceValue>& args) {
        // one real argument
        expect_arity("real", args, 1);
        // processor
        auto l = []<typename T0>(T0& arg) -> XyceValue {
            // actual parameter type
            using TX = std::decay_t<T0>;
            // vector<complex>
            if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                // vector
                std::vector<double> out;
                // allocate space
                out.reserve(arg->size());
                // append real values
                std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto v) { return v.real(); });
                // exit
                return std::make_shared<View<double>>(std::move(out));
            }
            // vector<double>
            if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>)
                return arg;
            // scalar
            return scalar_value<double>(arg);
        };
        // exit
        return std::visit(l, args[0]);
    }

    // imaginary part builtin
    XyceValue builtin_imag(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("imag", args, 1);
        // processor
        auto l = []<typename T0>(T0& arg) -> XyceValue {
            // actual parameter type
            using TX = std::decay_t<T0>;
            // View<complex>
            if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                // vector
                std::vector<double> out;
                // allocate space
                out.reserve(arg->size());
                // append imag values
                std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto v) { return v.imag(); });
                // exit
                return std::make_shared<View<double>>(std::move(out));
            }
            // View<double>
            if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
                // create vector with zeros
                auto v = std::vector<double>(arg->size(), 0.0);
                // create view (owning the vector)
                return std::make_shared<View<double>>(std::move(v));
            }
            // scalar
            if constexpr (std::is_same_v<TX, std::complex<double>>) {
                return arg.imag();
            }
            return 0.0;
        };
        // exit
        return std::visit(l, args[0]);
    }

    // phase angle builtin
    XyceValue builtin_angle(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("angle", args, 1);
        // processor
        auto l = []<typename T0>(T0& arg) -> XyceValue {
            // actual parameter type
            using TX = std::decay_t<T0>;
            // View<complex>
            if constexpr (std::is_same_v<TX, std::shared_ptr<View<std::complex<double>>>>) {
                // vector
                std::vector<double> out;
                // allocate space
                out.reserve(arg->size());
                // append angle values
                std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto v) { return std::arg(v) * 180.0 / std::numbers::pi; });
                // exit
                return std::make_shared<View<double>>(std::move(out));
            }
            // View<double>
            else if constexpr (std::is_same_v<TX, std::shared_ptr<View<double>>>) {
                // vector
                std::vector<double> out;
                // allocate space
                out.reserve(arg->size());
                // append angle values
                std::ranges::transform(arg->begin(), arg->end(), std::back_inserter(out), [](auto v) { return v >= 0.0 ? 0 : 180.0; });
                // exit
                return std::make_shared<View<double>>(std::move(out));
            }
            // complex
            else if constexpr (std::is_same_v<TX, std::complex<double>>) {
                return std::arg(arg) * 180.0 / std::numbers::pi;
            }
            // double
            else {
                return arg >= 0.0 ? 0 : 180.0;
            }
        };
        // exit
        return std::visit(l, args[0]);
    }

    // sine builtin
    XyceValue builtin_sin(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("sin", args, 1);
        // apply function, returns vector or scalar
        return map_unary_complex(args[0], [](const std::complex<double> value) { return std::sin(value); });
    }

    // cosine builtin
    XyceValue builtin_cos(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("cos", args, 1);
        // apply function, returns vector or scalar
        return map_unary_complex(args[0], [](const std::complex<double> value) { return std::cos(value); });
    }

    // tangent builtin
    XyceValue builtin_tan(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("tan", args, 1);
        // apply function, returns vector or scalar
        return map_unary_real("tan", args[0], [](const double value) { return std::tan(value); });
    }

    // arcsine builtin
    XyceValue builtin_asin(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("asin", args, 1);
        // apply function, returns vector or scalar
        return std::asin(real_argument("asin", args[0]));
    }

    // arccosine builtin
    XyceValue builtin_acos(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("acos", args, 1);
        // apply function, returns vector or scalar
        return std::acos(real_argument("acos", args[0]));
    }

    // arctangent builtin
    XyceValue builtin_atan(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("atan", args, 1);
        // apply function, returns vector or scalar
        return std::atan(real_argument("atan", args[0]));
    }

    // arctangent alias
    XyceValue builtin_arctan(const std::vector<XyceValue>& args) { return builtin_atan(args); }

    // atan2 builtin
    XyceValue builtin_atan2(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("atan2", args, 2);
        // apply function, returns vector or scalar
        return std::atan2(real_argument("atan2", args[0]), real_argument("atan2", args[1]));
    }

    // hyperbolic sine builtin
    XyceValue builtin_sinh(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("sinh", args, 1);
        // apply function, returns vector or scalar
        return std::sinh(real_argument("sinh", args[0]));
    }

    // hyperbolic cosine builtin
    XyceValue builtin_cosh(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("cosh", args, 1);
        // apply function, returns vector or scalar
        return std::cosh(real_argument("cosh", args[0]));
    }

    // hyperbolic tangent builtin
    XyceValue builtin_tanh(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("tanh", args, 1);
        // apply function, returns vector or scalar
        return std::tanh(real_argument("tanh", args[0]));
    }

    // inverse hyperbolic sine builtin
    XyceValue builtin_asinh(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("asinh", args, 1);
        // apply function, returns vector or scalar
        return std::asinh(real_argument("asinh", args[0]));
    }

    // inverse hyperbolic cosine builtin
    XyceValue builtin_acosh(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("acosh", args, 1);
        // apply function, returns vector or scalar
        return std::acosh(real_argument("acosh", args[0]));
    }

    // inverse hyperbolic tangent builtin
    XyceValue builtin_atanh(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("atanh", args, 1);
        // apply function, returns vector or scalar
        return std::atanh(real_argument("atanh", args[0]));
    }

    // exponential builtin
    XyceValue builtin_exp(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("exp", args, 1);
        // apply function, returns vector or scalar
        return std::exp(real_argument("exp", args[0]));
    }

    // complex conjugate builtin
    XyceValue builtin_conj(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("conj", args, 1);
        // processor
        auto l = []<typename T0>(T0& arg) -> XyceValue {
            // actual parameter type
            using TX = std::decay_t<T0>;
            // complex
            if constexpr (std::is_same_v<TX, std::complex<double>>) {
                return std::conj(arg);
            }
            return std::conj(std::complex<double>(scalar_value<double>(arg), 0.0));
        };
        // exit
        return std::visit(l, args[0]);
    }

    // square builtin
    XyceValue builtin_sqr(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("sqr", args, 1);
        // scalar argument
        const auto x = scalar_value<double>(args[0]);
        // multiply values
        return x * x;
    }

    // signum builtin
    XyceValue builtin_sgn(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("sgn", args, 1);
        // scalar argument
        const auto x = real_argument("sgn", args[0]);
        // sign
        return (x > 0.0) - (x < 0.0);
    }

    // sign-with-magnitude builtin
    XyceValue builtin_sign(const std::vector<XyceValue>& args) {
        // two values argument
        expect_arity("sign", args, 2);
        // scalar arguments
        const auto x = std::abs(real_argument("sign", args[0]));
        const auto y = real_argument("sign", args[1]);
        // sgn(y) is zero for a zero second argument, so the magnitude collapses
        if (y == 0.0)
            return 0.0;
        // return sgn(y) * |x|
        return y < 0.0 ? -x : x;
    }

    // positive ramp builtin
    XyceValue builtin_uramp(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("uramp", args, 1);
        // max(arg, 0)
        return std::max(real_argument("uramp", args[0]), 0.0);
    }

    // step builtin
    XyceValue builtin_stp(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("stp", args, 1);
        // return 1.0 if arg > 0, else 0.0
        return real_argument("stp", args[0]) > 0.0 ? 1.0 : 0.0;
    }

    // round builtin
    XyceValue builtin_round(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("round", args, 1);
        // round
        return std::round(real_argument("round", args[0]));
    }

    // nearest-integer builtin
    XyceValue builtin_nint(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("nint", args, 1);
        // round
        return std::round(real_argument("nint", args[0]));
    }

    // floor builtin
    XyceValue builtin_floor(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("floor", args, 1);
        // floor
        return std::floor(real_argument("floor", args[0]));
    }

    // ceiling builtin
    XyceValue builtin_ceil(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("ceil", args, 1);
        // ceiling
        return std::ceil(real_argument("ceil", args[0]));
    }

    // truncate builtin
    XyceValue builtin_int(const std::vector<XyceValue>& args) {
        // one value argument
        expect_arity("int", args, 1);
        // truncate
        return std::trunc(real_argument("int", args[0]));
    }

    // power builtin
    XyceValue builtin_pow(const std::vector<XyceValue>& args) {
        // two value arguments
        expect_arity("pow", args, 2);
        // arg0 ^ arg1
        return std::pow(real_argument("pow", args[0]), real_argument("pow", args[1]));
    }

    // power builtin
    XyceValue builtin_pwr(const std::vector<XyceValue>& args) {
        // two value arguments
        expect_arity("pwr", args, 2);
        // arg0 ^ arg1, documented as an alternate spelling of pow without a magnitude
        return std::pow(real_argument("pwr", args[0]), real_argument("pwr", args[1]));
    }

    // signed-power builtin
    XyceValue builtin_pwrs(const std::vector<XyceValue>& args) {
        // two value arguments
        expect_arity("pwrs", args, 2);
        // scalar arguments
        const auto x = real_argument("pwrs", args[0]);
        const auto y = real_argument("pwrs", args[1]);
        // return sign(x) * abs(x)^y
        return (x < 0.0 ? -1.0 : 1.0) * std::pow(std::abs(x), y);
    }

    // floating remainder builtin
    XyceValue builtin_fmod(const std::vector<XyceValue>& args) {
        // two value arguments
        expect_arity("fmod", args, 2);
        // floating remainder
        return std::fmod(real_argument("fmod", args[0]), real_argument("fmod", args[1]));
    }

    // minimum builtin
    XyceValue builtin_min(const std::vector<XyceValue>& args) {
        // at least one value argument
        expect_min_arity("min", args, 1);
        // first argument is the initial minimum
        auto result = real_argument("min", args[0]);
        // loop other arguments, updating the minimum
        for (size_t i = 1; i < args.size(); ++i)
            result = std::min(result, real_argument("min", args[i]));
        // exit
        return result;
    }

    // maximum builtin
    XyceValue builtin_max(const std::vector<XyceValue>& args) {
        // at least one value argument
        expect_min_arity("max", args, 1);
        // first argument is the initial maximum
        auto result = real_argument("max", args[0]);
        // loop other arguments, updating the maximum
        for (size_t i = 1; i < args.size(); ++i)
            result = std::max(result, real_argument("max", args[i]));
        // exit
        return result;
    }

    // clamp builtin
    XyceValue builtin_limit(const std::vector<XyceValue>& args) {
        // three value arguments
        expect_arity("limit", args, 3);
        // clamp the first argument between the second and third arguments
        return std::clamp(real_argument("limit", args[0]), real_argument("limit", args[1]), real_argument("limit", args[2]));
    }

    // conditional builtin
    XyceValue builtin_if(const std::vector<XyceValue>& args) {
        // three value arguments
        expect_arity("if", args, 3);
        // the condition selects a branch, it carries no complex part of its own
        require_real_argument("if", args[0]);
        // if all arguments are scalars, return a scalar
        if (!is_vector(args[0]) && !is_vector(args[1]) && !is_vector(args[2]))
            return scalar_value<double>(args[0]) != 0.0 ? args[1] : args[2];
        // a complex branch keeps its imaginary part, so all operands become complex
        if (is_complex(args[1]) || is_complex(args[2])) {
            // convert all arguments to complex vectors for broadcasting
            const auto condition = to_complex_vector(args[0]);
            const auto if_true = to_complex_vector(args[1]);
            const auto if_false = to_complex_vector(args[2]);
            // output vector
            std::vector<std::complex<double>> out;
            // allocate space
            out.reserve(condition->size());
            // loop over condition, broadcasting if necessary
            for (size_t index = 0; index < condition->size(); ++index) {
                // true and false values
                const auto t = if_true->size() == 1 ? if_true->operator[](0) : if_true->operator[](index);
                const auto f = if_false->size() == 1 ? if_false->operator[](0) : if_false->operator[](index);
                // append value based on condition
                out.push_back(condition->operator[](index).real() != 0.0 ? t : f);
            }
            // exit
            return std::make_shared<View<std::complex<double>>>(std::move(out));
        }
        // convert all arguments to real vectors for broadcasting
        const auto condition = to_real_vector(args[0]);
        const auto if_true = to_real_vector(args[1]);
        const auto if_false = to_real_vector(args[2]);
        // output vector
        std::vector<double> out;
        // allocate space
        out.reserve(condition->size());
        // loop over condition, broadcasting if necessary
        for (size_t index = 0; index < condition->size(); ++index) {
            // true and false values
            const auto t = if_true->size() == 1 ? if_true->operator[](0) : if_true->operator[](index);
            const auto f = if_false->size() == 1 ? if_false->operator[](0) : if_false->operator[](index);
            // append value based on condition
            out.push_back(condition->operator[](index) != 0.0 ? t : f);
        }
        return std::make_shared<View<double>>(std::move(out));
    }

    // derivative builtin placeholder
    XyceValue builtin_ddt(const std::vector<XyceValue>&) { throw std::logic_error("DDT() requires time-domain simulation context and cannot be evaluated in post-processing"); }

    // integral builtin placeholder
    XyceValue builtin_sdt(const std::vector<XyceValue>&) { throw std::logic_error("SDT() requires time-domain simulation context and cannot be evaluated in post-processing"); }
} // namespace

const std::unordered_map<std::string, double> NUMBER_SUFFIXES{
    {"T", 1e12}, {"G", 1e9}, {"MEG", 1e6}, {"K", 1e3}, {"M", 1e-3}, {"U", 1e-6}, {"N", 1e-9}, {"P", 1e-12}, {"F", 1e-15}, {"MIL", 25.4e-6},
};

const std::unordered_map<std::string, XyceValue> BUILTIN_CONSTANTS{
    {"e", std::exp(1.0)}, {"f", 1e-15}, {"g", 1e9}, {"j", std::complex<double>(0.0, 1.0)}, {"k", 1e3}, {"m", 1e-3}, {"meg", 1e6}, {"mho", 1.0}, {"mil", 25.4e-6}, {"n", 1e-9}, {"p", 1e-12}, {"pi", std::acos(-1.0)}, {"s", 1.0}, {"t", 1e12}, {"u", 1e-6},
};

const std::unordered_map<std::string, BuiltinCallable> BUILTIN_FUNCTIONS{
    {"abs", builtin_abs}, {"acos", builtin_acos}, {"acosh", builtin_acosh}, {"arctan", builtin_arctan}, {"asin", builtin_asin}, {"asinh", builtin_asinh}, {"atan", builtin_atan}, {"atanh", builtin_atanh}, {"atan2", builtin_atan2}, {"ceil", builtin_ceil}, {"conj", builtin_conj}, {"cos", builtin_cos}, {"cosh", builtin_cosh}, {"db", builtin_db}, {"ddt", builtin_ddt}, {"exp", builtin_exp}, {"floor", builtin_floor}, {"fmod", builtin_fmod}, {"if", builtin_if}, {"img", builtin_imag}, {"imag", builtin_imag}, {"int", builtin_int}, {"limit", builtin_limit}, {"ln", builtin_ln}, {"log", builtin_log}, {"log10", builtin_log10}, {"m", builtin_abs}, {"mag", builtin_abs}, {"max", builtin_max}, {"min", builtin_min}, {"nint", builtin_nint}, {"ph", builtin_angle}, {"phase", builtin_angle}, {"pow", builtin_pow}, {"pwr", builtin_pwr}, {"pwrs", builtin_pwrs}, {"r", builtin_real}, {"re", builtin_real}, {"real", builtin_real}, {"round", builtin_round}, {"sdt", builtin_sdt}, {"sgn", builtin_sgn}, {"sign", builtin_sign}, {"sin", builtin_sin}, {"sinh", builtin_sinh}, {"sqr", builtin_sqr}, {"sqrt", builtin_sqrt}, {"stp", builtin_stp}, {"tan", builtin_tan}, {"tanh", builtin_tanh}, {"uramp", builtin_uramp},
};

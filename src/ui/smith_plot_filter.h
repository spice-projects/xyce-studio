#pragma once

#include <string>

#include "../expression/expression.h"

// whether the expression may plot on a smith chart: only the complex diagonal
// parameter entries (sii, yii, zii) carry a gamma-plane value; any other
// expression — real values or off-diagonal parameters — is rejected
[[nodiscard]] inline bool is_smith_plot_expression(const AnyExpression& expression) {
    // only complex expressions carry a gamma-plane position
    const auto* complex_expression = std::get_if<Expression<std::complex<double>>>(&expression);
    if (complex_expression == nullptr)
        return false;
    // the name must be a parameter entry, prefix s/y/z followed by the port
    // indices; the diagonal entries repeat the same index twice
    const std::string& name = complex_expression->name();
    if (name.size() < 3 || (name[0] != 'S' && name[0] != 'Y' && name[0] != 'Z'))
        return false;
    // split the digits after the prefix into two halves; the halves must be
    // identical (s11, s22, ...)
    const std::string digits = name.substr(1);
    if (digits.size() % 2 != 0)
        return false;
    // any character that is not a digit rejects the name
    for (const char digit : digits) {
        if (digit < '0' || digit > '9')
            return false;
    }
    // identical halves make the entry diagonal
    const size_t half = digits.size() / 2;
    return digits.compare(0, half, digits, half) == 0;
}

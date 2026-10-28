#ifndef STEP_INFORMATION_H
#define STEP_INFORMATION_H

#include <string>
#include <utility>
#include <vector>

// step information class
class StepInformation
{
public:
    StepInformation() = delete;

    StepInformation(std::vector<std::string> keys, std::vector<std::vector<double>> values, std::vector<std::pair<double, double>> abscissa_value_ranges);

    [[nodiscard]] const std::vector<std::string>& keys() const;

    [[nodiscard]] const std::vector<std::vector<double>>& values() const;

    [[nodiscard]] size_t length() const;

    [[nodiscard]] double abscissa_left_value() const;

    [[nodiscard]] double abscissa_right_value() const;

    [[nodiscard]] bool is_abscissa_ascending() const;

    [[nodiscard]] double step_abscissa_left_value(size_t step_index) const;

    [[nodiscard]] double step_abscissa_right_value(size_t step_index) const;

private:
    std::vector<std::string> m_keys;
    std::vector<std::vector<double>> m_values;
    std::vector<std::pair<double, double>> m_abscissa_value_ranges;
    size_t m_step_count;
    bool m_is_abscissa_ascending;
    double m_abscissa_left_value;
    double m_abscissa_right_value;
};

#endif

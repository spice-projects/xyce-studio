#include <algorithm>

#include "step_information.h"

StepInformation::StepInformation(std::vector<std::string> keys, std::vector<std::vector<double>> values, std::vector<std::pair<double, double>> abscissa_value_ranges) :
    m_keys(std::move(keys)), m_values(std::move(values)), m_abscissa_value_ranges(std::move(abscissa_value_ranges)), m_step_count(m_abscissa_value_ranges.size()) {
    // check step count
    if (m_step_count > 0) {
        // determine direction
        m_is_abscissa_ascending = m_abscissa_value_ranges[0].first <= m_abscissa_value_ranges[0].second;
        if (m_is_abscissa_ascending) {
            // initialize left and right values
            double left = m_abscissa_value_ranges[0].first;
            double right = m_abscissa_value_ranges[0].second;
            // loop steps
            for (const auto& [first, second] : m_abscissa_value_ranges) {
                // compute minimum
                left = std::min(left, first);
                // compute maximum
                right = std::max(right, second);
            }
            // assign left & right values
            m_abscissa_left_value = left;
            m_abscissa_right_value = right;
        }
        else {
            // initialize left and right values for descending
            double left = m_abscissa_value_ranges[0].first;
            double right = m_abscissa_value_ranges[0].second;
            // loop steps
            for (const auto& [first, second] : m_abscissa_value_ranges) {
                // compute maximum
                left = std::max(left, first);
                // compute minimum
                right = std::min(right, second);
            }
            // assign left & right values
            m_abscissa_left_value = left;
            m_abscissa_right_value = right;
        }
    }
    else {
        // asc
        m_is_abscissa_ascending = true;
        // default to zero
        m_abscissa_left_value = 0.0;
        m_abscissa_right_value = 0.0;
    }
}

const std::vector<std::string>& StepInformation::keys() const {
    // return keys
    return m_keys;
}

const std::vector<std::vector<double>>& StepInformation::values() const {
    // return values
    return m_values;
}

size_t StepInformation::length() const {
    // return count
    return m_step_count;
}

double StepInformation::abscissa_left_value() const {
    // return left
    return m_abscissa_left_value;
}

double StepInformation::abscissa_right_value() const {
    // return right
    return m_abscissa_right_value;
}

bool StepInformation::is_abscissa_ascending() const {
    // return ascending
    return m_is_abscissa_ascending;
}

double StepInformation::step_abscissa_left_value(size_t step_index) const {
    // return left value for index
    return m_abscissa_value_ranges.at(step_index).first;
}

double StepInformation::step_abscissa_right_value(size_t step_index) const {
    // return right value for index
    return m_abscissa_value_ranges.at(step_index).second;
}

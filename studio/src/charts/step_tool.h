#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace studio
{
    // one parameter combination the step tool lists
    struct StepToolRow
    {
        // the step index the chart selects
        std::size_t index = 0;

        // the formatted parameter values, empty when the dataset carries none
        std::vector<std::string> values;

        // whether the chart keeps this step active
        bool selected = false;
    };

    // the steps and keys the step tool opens with
    struct StepToolState
    {
        // the chart the dialog edits
        std::size_t chart_index = 0;

        // the dataset that owns the chart, which the accept checks before applying
        int dataset_id = 0;

        // the sweep parameter names, empty when the dataset carries none
        std::vector<std::string> keys;

        // one row per parameter combination
        std::vector<StepToolRow> rows;
    };
} // namespace studio

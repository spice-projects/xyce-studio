#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace studio
{
    // one expression the add/remove plots dialog offers
    struct PlotExpression
    {
        // the expression name a chart plots
        std::string name;

        // the unit the expression carries, empty when it has none
        std::string unit;

        // whether the chart already plots the expression
        bool selected = false;
    };

    // the expressions and rules the add/remove plots dialog opens with
    struct PlotSelectionState
    {
        // the chart the dialog edits
        std::size_t chart_index = 0;

        // whether the chart plots the fixed smith plane
        bool smith = false;

        // whether the chart accepts a custom expression
        bool allow_custom = true;

        // the expressions the dialog lists
        std::vector<PlotExpression> expressions;
    };
} // namespace studio

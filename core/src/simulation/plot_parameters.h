#pragma once

#include <string>
#include <vector>

// parse every .PLOT directive into one chart group per directive, each group holding the expressions plotted in one chart; the directives are an Xyce Studio extension to the netlist language declaring the default charts of a run
[[nodiscard]] std::vector<std::vector<std::string>> plot_chart_groups(const std::vector<std::string>& plot_directives);

// collect the .PRINT output variables the plotted expressions need, a bare output variable contributes itself and a compound expression contributes the solution variables it reads
[[nodiscard]] std::vector<std::string> plot_output_variables(const std::vector<std::vector<std::string>>& chart_groups);

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "simulation/plot_parameters.h"

TEST(PlotDirectiveChecks, parses_one_chart_group_per_directive) {
    // arrange / act
    const auto groups = plot_chart_groups({".PLOT V(N1) V(N2,N3) abs(I(R1))", ".PLOT I(R3)"});
    // assert — the first directive declares a chart with three series, the second one with a single series
    ASSERT_EQ(groups.size(), 2);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"V(N1)", "V(N2,N3)", "abs(I(R1))"}));
    ASSERT_EQ(groups[1], (std::vector<std::string>{"I(R3)"}));
}

TEST(PlotDirectiveChecks, tolerates_the_hspice_plot_type_token) {
    // arrange / act
    const auto groups = plot_chart_groups({".PLOT TRAN V(1) V(2)"});
    // assert — the leading analysis type is dropped because a run produces a single analysis output
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"V(1)", "V(2)"}));
}

TEST(PlotDirectiveChecks, keeps_an_expression_that_is_named_like_a_plot_type) {
    // arrange / act — the type token is only dropped as the first token of the line
    const auto groups = plot_chart_groups({".PLOT V(1) TRAN"});
    // assert
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"V(1)", "TRAN"}));
}

TEST(PlotDirectiveChecks, tolerates_the_comma_separated_spelling) {
    // arrange / act
    const auto groups = plot_chart_groups({".PLOT V(N1), V(N2,N3), abs(I(R1))"});
    // assert — a top-level comma separates the series while the comma of the voltage difference stays inside its argument list
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"V(N1)", "V(N2,N3)", "abs(I(R1))"}));
}

TEST(PlotDirectiveChecks, keeps_a_braced_expression_together_and_drops_the_braces) {
    // arrange / act
    const auto groups = plot_chart_groups({".PLOT {V(1) * 2} V(2)"});
    // assert — the braces group the expression for the tokenizer and are dropped so the expression evaluates on its own
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"V(1) * 2", "V(2)"}));
}

TEST(PlotDirectiveChecks, keeps_a_comma_inside_a_braced_expression) {
    // arrange / act
    const auto groups = plot_chart_groups({".PLOT {V(1), V(2)} V(3)"});
    // assert — a comma nested in the braces belongs to the expression, the braces are dropped so the expression evaluates on its own
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"V(1), V(2)", "V(3)"}));
}

TEST(PlotDirectiveChecks, normalizes_the_power_alias_to_the_p_probe) {
    // arrange / act
    const auto groups = plot_chart_groups({".PLOT W(R1) I(R2)"});
    // assert — the .PRINT parser rewrites W( to P( and the chart expression follows so the name matches the produced column
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"P(R1)", "I(R2)"}));
}

TEST(PlotDirectiveChecks, skips_a_directive_without_a_single_expression) {
    // arrange / act
    const auto groups = plot_chart_groups({".PLOT", ".PLOT   ", ".PLOT {}", ".PLOT V(1)"});
    // assert — a directive without expressions declares no chart, an empty brace pair counts as no expression
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"V(1)"}));
}

TEST(PlotDirectiveChecks, declares_a_single_name_expression_as_a_series) {
    // arrange / act
    const auto groups = plot_chart_groups({".PLOT x"});
    // assert — a name without an argument list is declared as written and resolves to nothing once the charts are built
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"x"}));
}

TEST(PlotDirectiveChecks, skips_a_line_that_is_not_a_plot_directive) {
    // arrange / act
    const auto groups = plot_chart_groups({".PRINT TRAN V(1)", "R1 1 0 100", ""});
    // assert
    ASSERT_TRUE(groups.empty());
}

TEST(PlotDirectiveChecks, tolerates_a_lowercase_plot_directive) {
    // arrange / act
    const auto groups = plot_chart_groups({".plot v(1) i(r2)"});
    // assert
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(groups[0], (std::vector<std::string>{"v(1)", "i(r2)"}));
}

TEST(PlotDirectiveChecks, collects_the_quantities_of_a_bare_output_variable) {
    // arrange / act
    const auto variables = plot_output_variables({{"V(N1)", "I(R1)"}});
    // assert — a bare output variable contributes itself
    ASSERT_EQ(variables, (std::vector<std::string>{"V(N1)", "I(R1)"}));
}

TEST(PlotDirectiveChecks, keeps_a_wildcard_output_variable) {
    // arrange / act
    const auto variables = plot_output_variables({{"V(*)", "I(R1)"}});
    // assert
    ASSERT_EQ(variables, (std::vector<std::string>{"V(*)", "I(R1)"}));
}

TEST(PlotDirectiveChecks, keeps_a_device_parameter_probe) {
    // arrange / act
    const auto variables = plot_output_variables({{"R1:res"}});
    // assert
    ASSERT_EQ(variables, (std::vector<std::string>{"R1:res"}));
}

TEST(PlotDirectiveChecks, keeps_a_lead_current_probe) {
    // arrange / act
    const auto variables = plot_output_variables({{"IC(Q1)", "IB(Q2)"}});
    // assert
    ASSERT_EQ(variables, (std::vector<std::string>{"IC(Q1)", "IB(Q2)"}));
}

TEST(PlotDirectiveChecks, collects_nothing_from_a_network_parameter_name) {
    // arrange / act — an s-parameter entry is not a .PRINT output variable, it only exists in a touchstone dataset
    const auto variables = plot_output_variables({{"S11", "db(S(2,1))"}});
    // assert
    ASSERT_TRUE(variables.empty());
}

TEST(PlotDirectiveChecks, collects_the_solution_variables_of_a_compound_expression) {
    // arrange / act
    const auto variables = plot_output_variables({{"abs(I(R1))", "db(V(out))", "V(N2,N3)*2"}});
    // assert — the function calls themselves are evaluated in the viewer, only their solution variables must be produced
    ASSERT_EQ(variables, (std::vector<std::string>{"I(R1)", "V(out)", "V(N2,N3)"}));
}

TEST(PlotDirectiveChecks, collects_the_solution_variables_of_a_nested_expression) {
    // arrange / act
    const auto variables = plot_output_variables({{"V(a)/I(R1) + V(b)*P(R2)"}});
    // assert
    ASSERT_EQ(variables, (std::vector<std::string>{"V(a)", "I(R1)", "V(b)", "P(R2)"}));
}

TEST(PlotDirectiveChecks, collects_the_solution_variables_of_a_negated_expression) {
    // arrange / act
    const auto variables = plot_output_variables({{"-V(out)"}});
    // assert
    ASSERT_EQ(variables, (std::vector<std::string>{"V(out)"}));
}

TEST(PlotDirectiveChecks, normalizes_a_voltage_difference_written_with_spaces) {
    // arrange / act — the space inside the argument list is not a series separator
    const auto variables = plot_output_variables({{"V(N2, N3)"}});
    // assert — the rendered quantity carries no whitespace so the produced column name matches
    ASSERT_EQ(variables, (std::vector<std::string>{"V(N2,N3)"}));
}

TEST(PlotDirectiveChecks, collects_the_solution_variables_of_a_frequency_domain_call) {
    // arrange / act — the frequency-domain variants take a full expression, so their argument is not a bare node name
    const auto variables = plot_output_variables({{"VM(V(1) + V(2))"}});
    // assert — the viewer evaluates the magnitude itself, so the voltages it reads are produced
    ASSERT_EQ(variables, (std::vector<std::string>{"V(1)", "V(2)"}));
}

TEST(PlotDirectiveChecks, collects_the_solution_variables_of_a_conditional_expression) {
    // arrange / act
    const auto variables = plot_output_variables({{"V(out) > 0 ? I(R1) : 0"}});
    // assert — every branch of the conditional contributes its quantities
    ASSERT_EQ(variables, (std::vector<std::string>{"V(out)", "I(R1)"}));
}

TEST(PlotDirectiveChecks, skips_the_leaves_of_a_per_step_selector) {
    // arrange / act — a .PRINT line cannot select a single step of a quantity
    const auto variables = plot_output_variables({{"I(R1)@2", "V(out)"}});
    // assert
    ASSERT_EQ(variables, (std::vector<std::string>{"V(out)"}));
}

TEST(PlotDirectiveChecks, collects_the_solution_variables_of_a_nested_user_function_call) {
    // arrange / act — a netlist .FUNC cannot be evaluated in the viewer but its solution variables are still collected
    const auto variables = plot_output_variables({{"powerTestFunc(I(V1))"}});
    // assert
    ASSERT_EQ(variables, (std::vector<std::string>{"I(V1)"}));
}

TEST(PlotDirectiveChecks, collects_nothing_from_an_unparsable_expression) {
    // arrange / act
    const auto variables = plot_output_variables({{"V(1))", "V(out)"}});
    // assert — the broken expression contributes no quantities while the valid one still does
    ASSERT_EQ(variables, (std::vector<std::string>{"V(out)"}));
}

TEST(PlotDirectiveChecks, collects_nothing_from_a_constant_expression) {
    // arrange / act
    const auto variables = plot_output_variables({{"2*3", "time"}});
    // assert — constants and the abscissa are not output variables
    ASSERT_TRUE(variables.empty());
}

TEST(PlotDirectiveChecks, keeps_nothing_from_a_conditional_expression_that_reads_no_solution_variable) {
    // arrange / act — a conditional whose branches are plain values must not be mistaken for a device parameter probe
    const auto variables = plot_output_variables({{"1>0?2:3"}});
    // assert
    ASSERT_TRUE(variables.empty());
}

TEST(PlotDirectiveChecks, keeps_nothing_from_a_malformed_output_variable) {
    // arrange / act — half-written probes contribute no quantity instead of a broken .PRINT line
    const auto variables = plot_output_variables({{":res", "R1:", "(a)", "V(1"}});
    // assert
    ASSERT_TRUE(variables.empty());
}

TEST(PlotDirectiveChecks, collects_each_quantity_once_across_the_chart_groups) {
    // arrange / act
    const auto variables = plot_output_variables({{"V(1)", "abs(I(R1))"}, {"I(R1)", "V(2)"}});
    // assert — a quantity repeated by several expressions is collected once, in first-seen order
    ASSERT_EQ(variables, (std::vector<std::string>{"V(1)", "I(R1)", "V(2)"}));
}

TEST(PlotDirectiveChecks, compares_the_collected_quantities_case_insensitively) {
    // arrange / act
    const auto variables = plot_output_variables({{"V(out)", "v(OUT)"}});
    // assert — the expression manager looks the names up in lowercase, so a differently spelled duplicate is the same quantity
    ASSERT_EQ(variables, (std::vector<std::string>{"V(out)"}));
}

TEST(PlotDirectiveChecks, collects_the_quantities_of_every_directive_of_a_netlist) {
    // arrange / act
    const auto groups = plot_chart_groups({".PLOT V(N1) abs(I(R1))", ".PLOT I(R3)"});
    const auto variables = plot_output_variables(groups);
    // assert
    ASSERT_EQ(groups.size(), 2);
    ASSERT_EQ(variables, (std::vector<std::string>{"V(N1)", "I(R1)", "I(R3)"}));
}

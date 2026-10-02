#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../core/util.h"
#include "../expression/xyce_parser.h"
#include "plot_parameters.h"
#include "print_parameters.h"

namespace
{

    // analysis and print type names accepted as the leading token of a .PLOT directive; HSPICE spells the type first and Xyce Studio ignores it because a run produces a single analysis output
    const std::vector<std::string_view> PLOT_TYPE_TOKENS = {
        "AC", "AC_IC", "DC", "FFT", "FOUR", "HB", "HB_FD", "HB_IC", "HB_TD", "HOMOTOPY", "LIN", "NOISE", "OP", "PCE", "SENS", "TRAN", "TRANADJOINT",
    };

    // Xyce .PRINT output variable names (RG 2.1.31) that read solution variables, the lead current notation adds a lead letter to I (e.g. IC(Q1)) so I itself is never matched exactly
    const std::vector<std::string_view> OUTPUT_VARIABLE_NAMES = {
        "V", "VDB", "VI", "VM", "VP", "VR", "I", "N", "P", "W", "DNI", "DNO",
    };

    // check whether a probe name reads a solution variable, the lead current notation (I followed by a lead letter) is accepted as well
    bool is_output_variable_name(const std::string& name) {
        // uppercase the probe name for the comparison
        const auto upper_name = to_upper(name);
        // a documented output variable name reads solution variables
        if (std::ranges::find(OUTPUT_VARIABLE_NAMES.begin(), OUTPUT_VARIABLE_NAMES.end(), upper_name) != OUTPUT_VARIABLE_NAMES.end())
            return true;
        // the lead current notation spells the lead letter right after I (e.g. IC(Q1), IB(Q2))
        return upper_name.size() >= 2 && upper_name.starts_with('I');
    }

    // replace the top-level commas of a directive with spaces so the comma-separated spelling of a .PLOT line tokenizes like the whitespace-separated .PRINT spelling; a comma inside an argument list or a braced expression belongs to the expression
    std::string separate_top_level_commas(std::string_view text) {
        // mutable copy the separators are replaced in
        std::string separated(text);
        // argument list nesting depth
        int paren_depth = 0;
        // braced expression nesting depth
        int brace_depth = 0;
        // process every character
        for (auto& ch : separated) {
            // track the argument list depth
            if (ch == '(') {
                ++paren_depth;
                continue;
            }
            if (ch == ')') {
                --paren_depth;
                continue;
            }
            // track the braced expression depth
            if (ch == '{') {
                ++brace_depth;
                continue;
            }
            if (ch == '}') {
                --brace_depth;
                continue;
            }
            // replace a top-level comma with the whitespace separator of a .PRINT statement
            if (ch == ',' && paren_depth == 0 && brace_depth == 0)
                ch = ' ';
        }
        return separated;
    }

    // strip one pair of outer braces from an expression; .PRINT uses the braces to group an expression but a chart expression is evaluated on its own
    std::string strip_expression_braces(const std::string& expression) {
        // only a fully braced expression loses its braces
        if (expression.size() < 2 || expression.front() != '{' || expression.back() != '}')
            return expression;
        return expression.substr(1, expression.size() - 2);
    }

    // check whether an expression is a bare .PRINT output variable such as V(out), I(R1), P(R1) or R1:res, wildcards such as V(*) included
    bool is_bare_output_variable(const std::string& expression) {
        // a device or model parameter probe is spelled <device>:<parameter> and carries no argument list; a conditional expression also holds a colon but never a question mark-free single colon pair of plain names
        if (const auto colon = expression.find(':'); colon != std::string::npos) {
            // a conditional expression is not a parameter probe
            if (expression.find('?') != std::string::npos)
                return false;
            // both sides must be non-empty and free of whitespace, argument lists and further separators
            const auto device = expression.substr(0, colon);
            const auto parameter = expression.substr(colon + 1);
            return !device.empty() && !parameter.empty() && device.find_first_of("() \t:?") == std::string::npos && parameter.find_first_of("() \t:?") == std::string::npos;
        }
        // locate the argument list opener
        const auto open = expression.find('(');
        // every other bare output variable carries a parenthesized argument list
        if (open == std::string::npos || open == 0 || expression.back() != ')')
            return false;
        // the argument list must not nest another list and must not carry whitespace
        const auto arguments = expression.substr(open + 1, expression.size() - open - 2);
        if (arguments.find_first_of("() \t") != std::string::npos)
            return false;
        // a solution-variable probe is spelled V(node), I(device), P(device) or N(variable)
        return is_output_variable_name(expression.substr(0, open));
    }

    // render a parsed probe leaf back to the .PRINT spelling, e.g. V(N2,N3) or I(R1)
    std::string render_probe_leaf(const FunctionCallNode& node) {
        // start with the probe name as written
        std::string rendered = node.name;
        // open the argument list
        rendered += '(';
        // append every node name argument
        for (size_t i = 0; i < node.args.size(); ++i) {
            // separate the arguments with a comma
            if (i > 0)
                rendered += ',';
            // append the node name
            rendered += static_cast<const IdentifierNode&>(*node.args[i]).name;
        }
        // close the argument list
        rendered += ')';
        return rendered;
    }

    // collect the solution-variable leaves of a parsed expression, a probe leaf contributes itself and every other node is traversed; a per-step selector is skipped because a .PRINT line cannot select a single step
    void collect_probe_leaves(const ExpressionNode& node, std::vector<std::string>& leaves) {
        // a probe call reading only node names is a leaf
        if (const auto* function = dynamic_cast<const FunctionCallNode*>(&node); function != nullptr) {
            // check that the call reads a solution variable
            const bool is_probe = is_output_variable_name(function->name);
            // check that every argument is a plain node name
            const bool names_only = !function->args.empty() && std::ranges::all_of(function->args, [](const ExpressionPtr& argument) { return dynamic_cast<const IdentifierNode*>(argument.get()) != nullptr; });
            // render the leaf when the call is a probe over node names
            if (is_probe && names_only) {
                leaves.push_back(render_probe_leaf(*function));
                return;
            }
        }
        // a per-step selector reads one step of a quantity that cannot be printed
        if (dynamic_cast<const StepSelectorNode*>(&node) != nullptr)
            return;
        // traverse the children of every other node
        if (const auto* function = dynamic_cast<const FunctionCallNode*>(&node); function != nullptr) {
            for (const auto& argument : function->args)
                collect_probe_leaves(*argument, leaves);
        }
        else if (const auto* unary = dynamic_cast<const UnaryOperationNode*>(&node); unary != nullptr) {
            collect_probe_leaves(*unary->operand, leaves);
        }
        else if (const auto* binary = dynamic_cast<const BinaryOperationNode*>(&node); binary != nullptr) {
            collect_probe_leaves(*binary->left, leaves);
            collect_probe_leaves(*binary->right, leaves);
        }
        else if (const auto* ternary = dynamic_cast<const TernaryOperationNode*>(&node); ternary != nullptr) {
            collect_probe_leaves(*ternary->condition, leaves);
            collect_probe_leaves(*ternary->if_true, leaves);
            collect_probe_leaves(*ternary->if_false, leaves);
        }
    }

    // collect the solution variables a single chart expression reads; an unparsable expression contributes nothing and its series is skipped when the charts are built
    std::vector<std::string> expression_leaves(const std::string& expression) {
        // the quantities this expression needs
        std::vector<std::string> leaves;
        // a bare output variable is its own quantity, wildcards such as V(*) included
        if (is_bare_output_variable(expression)) {
            leaves.push_back(expression);
            return leaves;
        }
        // parse the compound expression and collect its solution-variable leaves
        try {
            XyceParser parser;
            collect_probe_leaves(*parser.parse_expression(expression), leaves);
        }
        catch (const std::exception&) {
            // an expression the evaluator cannot parse contributes no quantities
        }
        return leaves;
    }

} // anonymous namespace

std::vector<std::vector<std::string>> plot_chart_groups(const std::vector<std::string>& plot_directives) {
    // one chart group per directive
    std::vector<std::vector<std::string>> chart_groups;
    // process every directive in netlist order
    for (const auto& directive : plot_directives) {
        // tokenize the directive the .PRINT way, braces and quotes keep an expression together
        const auto tokens = tokenize_print_statement(separate_top_level_commas(directive));
        // skip a line that is not a .PLOT directive
        if (tokens.empty() || to_upper(tokens[0]) != ".PLOT")
            continue;
        // the first token after the directive name may be an HSPICE plot type, which the run ignores
        auto expression = tokens.begin() + 1;
        if (expression != tokens.end() && std::ranges::find(PLOT_TYPE_TOKENS, to_upper(*expression)) != PLOT_TYPE_TOKENS.end())
            ++expression;
        // the expressions plotted in the chart of this directive
        std::vector<std::string> group;
        // normalize every remaining token into one chart expression
        for (auto token = expression; token != tokens.end(); ++token) {
            // drop the .PRINT braces so the expression can be evaluated on its own
            const auto normalized = strip_expression_braces(trim(*token));
            // skip a token that holds no expression
            if (normalized.empty())
                continue;
            // normalize the W( power alias to P( the way the .PRINT parser does
            if (to_upper(normalized).starts_with("W("))
                group.push_back("P(" + normalized.substr(2));
            else
                group.push_back(normalized);
        }
        // a directive without a single expression declares no chart
        if (group.empty())
            continue;
        // append the chart group of this directive
        chart_groups.push_back(std::move(group));
    }
    return chart_groups;
}

std::vector<std::string> plot_output_variables(const std::vector<std::vector<std::string>>& chart_groups) {
    // collected output variables in first-seen order
    std::vector<std::string> variables;
    // names already collected, compared in lowercase because the expression manager looks the names up in lowercase
    std::set<std::string> collected;
    // process every chart group
    for (const auto& group : chart_groups) {
        // process every expression of the group
        for (const auto& expression : group) {
            // append every quantity of the expression that was not collected yet
            for (const auto& leaf : expression_leaves(expression)) {
                if (collected.insert(to_lower(leaf)).second)
                    variables.push_back(leaf);
            }
        }
    }
    return variables;
}

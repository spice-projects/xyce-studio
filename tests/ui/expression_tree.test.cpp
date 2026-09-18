#include <gtest/gtest.h>

#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "ui/expression_tree.h"

TEST(ExpressionTreeChecks, classifies_flat_voltage_probe) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("V(5)");
    // assert
    ASSERT_EQ(kind, GroupKind::Flat);
}

TEST(ExpressionTreeChecks, classifies_flat_named_net) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("V(NET-_R1-Pad1)");
    // assert
    ASSERT_EQ(kind, GroupKind::Flat);
}

TEST(ExpressionTreeChecks, classifies_flat_device_current) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("I(R1)");
    // assert
    ASSERT_EQ(kind, GroupKind::Flat);
}

TEST(ExpressionTreeChecks, classifies_subcircuit_probe) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("V(XU1:23)");
    // assert
    ASSERT_EQ(kind, GroupKind::Subcircuit);
}

TEST(ExpressionTreeChecks, classifies_nested_subcircuit_probe) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("V(XU1:XU2:5)");
    // assert
    ASSERT_EQ(kind, GroupKind::Subcircuit);
}

TEST(ExpressionTreeChecks, classifies_subcircuit_device_current) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("I(XU1:R1)");
    // assert
    ASSERT_EQ(kind, GroupKind::Subcircuit);
}

TEST(ExpressionTreeChecks, classifies_sheet_hierarchy_probe) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("V(/SHEET1/net1)");
    // assert
    ASSERT_EQ(kind, GroupKind::Sheet);
}

TEST(ExpressionTreeChecks, classifies_nested_sheet_hierarchy_probe) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("V(/SHEET1/SHEET2/net1)");
    // assert
    ASSERT_EQ(kind, GroupKind::Sheet);
}

TEST(ExpressionTreeChecks, classifies_single_segment_slash_as_flat) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("V(/net1)");
    // assert
    ASSERT_EQ(kind, GroupKind::Flat);
}

TEST(ExpressionTreeChecks, classifies_differential_probe_by_first_node) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("v(XU1:1,XU1:2)");
    // assert
    ASSERT_EQ(kind, GroupKind::Subcircuit);
}

TEST(ExpressionTreeChecks, classifies_compound_expression_as_flat) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("V(1)*abs(I(R1))");
    // assert
    ASSERT_EQ(kind, GroupKind::Flat);
}

TEST(ExpressionTreeChecks, classifies_scaled_subcircuit_probe_as_flat) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("2*I(XU1:R1)");
    // assert
    ASSERT_EQ(kind, GroupKind::Flat);
}

TEST(ExpressionTreeChecks, classifies_parenthesized_probe_as_flat) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("(I(XU1:R1))");
    // assert
    ASSERT_EQ(kind, GroupKind::Flat);
}

TEST(ExpressionTreeChecks, classifies_subtracted_probes_as_flat) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("I(XU301:X1:X1:E1)-I(XU301:X1:X1:C3)");
    // assert
    ASSERT_EQ(kind, GroupKind::Flat);
}

TEST(ExpressionTreeChecks, classifies_plain_name_as_flat) {
    // arrange / act
    const auto kind = ExpressionTree::kind_of("onoise_total");
    // assert
    ASSERT_EQ(kind, GroupKind::Flat);
}

TEST(ExpressionTreeChecks, hierarchy_of_nested_subcircuit_probe) {
    // arrange / act
    const auto segments = ExpressionTree::hierarchy_of("V(XU1:XU2:5)");
    // assert
    ASSERT_EQ(segments.size(), 2);
    ASSERT_EQ(segments[0], "XU1");
    ASSERT_EQ(segments[1], "XU2");
}

TEST(ExpressionTreeChecks, hierarchy_of_flat_probe_is_empty) {
    // arrange / act
    const auto segments = ExpressionTree::hierarchy_of("V(5)");
    // assert
    ASSERT_TRUE(segments.empty());
}

TEST(ExpressionTreeChecks, hierarchy_of_sheet_probe_segments) {
    // arrange / act
    const auto segments = ExpressionTree::hierarchy_of("V(/SHEET1/SHEET2/net1)");
    // assert
    ASSERT_EQ(segments.size(), 2);
    ASSERT_EQ(segments[0], "SHEET1");
    ASSERT_EQ(segments[1], "SHEET2");
}

TEST(ExpressionTreeChecks, scope_cards_replace_subcircuit_leaves) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(XU1:23)", "Voltage"}, {"V(XU1:45)", "Voltage"}});
    // act
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 2);
    ASSERT_FALSE(cards[0].is_scope);
    ASSERT_EQ(cards[0].label, "V(1)");
    ASSERT_EQ(cards[0].type, "Voltage");
    ASSERT_TRUE(cards[1].is_scope);
    ASSERT_EQ(cards[1].label, "XU1");
    ASSERT_EQ(cards[1].kind, "subcircuit");
    ASSERT_EQ(cards[1].count, 2);
}

TEST(ExpressionTreeChecks, scope_card_activates_with_scope_relative_labels) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}, {"V(XU1:45)", "Voltage"}});
    // act
    tree.activate(0);
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 2);
    ASSERT_FALSE(cards[0].is_scope);
    ASSERT_EQ(cards[0].label, "V(23)");
    ASSERT_EQ(cards[0].full_name, "V(XU1:23)");
    ASSERT_EQ(cards[1].label, "V(45)");
    ASSERT_EQ(cards[1].full_name, "V(XU1:45)");
}

TEST(ExpressionTreeChecks, deep_scope_leaves_shorten_one_level_at_a_time) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(X1:X2:node)", "Voltage"}});
    // act
    tree.activate(0);
    const auto first_level = tree.cards();
    tree.activate(0);
    const auto& second_level = tree.cards();
    // assert
    ASSERT_EQ(first_level.size(), 1);
    ASSERT_TRUE(first_level[0].is_scope);
    ASSERT_EQ(second_level.size(), 1);
    ASSERT_FALSE(second_level[0].is_scope);
    ASSERT_EQ(second_level[0].label, "V(node)");
    ASSERT_EQ(second_level[0].full_name, "V(X1:X2:node)");
}

TEST(ExpressionTreeChecks, nested_scope_cards_nest_by_path) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}, {"V(XU1:XU2:5)", "Voltage"}});
    // act
    tree.activate(0);
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 2);
    ASSERT_FALSE(cards[0].is_scope);
    ASSERT_EQ(cards[0].label, "V(23)");
    ASSERT_TRUE(cards[1].is_scope);
    ASSERT_EQ(cards[1].label, "XU2");
    ASSERT_EQ(cards[1].count, 1);
}

TEST(ExpressionTreeChecks, breadcrumb_tracks_browsed_scope) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:XU2:5)", "Voltage"}});
    // act
    tree.activate(0);
    const auto& breadcrumb = tree.breadcrumb();
    // assert
    ASSERT_EQ(breadcrumb.size(), 2);
    ASSERT_EQ(breadcrumb[0].label, "All");
    ASSERT_EQ(breadcrumb[0].level, 0);
    ASSERT_EQ(breadcrumb[1].label, "XU1");
    ASSERT_EQ(breadcrumb[1].level, 1);
}

TEST(ExpressionTreeChecks, breadcrumb_root_resets_to_root_scope) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}, {"V(1)", "Voltage"}});
    tree.activate(0);
    // act
    tree.open_breadcrumb(0);
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 2);
    ASSERT_TRUE(cards[1].is_scope);
}

TEST(ExpressionTreeChecks, breadcrumb_nested_level_pops_to_parent_scope) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:XU2:5)", "Voltage"}});
    tree.activate(0);
    tree.activate(0);
    const auto deepest = tree.breadcrumb();
    ASSERT_EQ(deepest.size(), 3);
    // act
    tree.open_breadcrumb(1);
    const auto& breadcrumb = tree.breadcrumb();
    // assert
    ASSERT_EQ(breadcrumb.size(), 2);
    ASSERT_EQ(breadcrumb[1].label, "XU1");
}

TEST(ExpressionTreeChecks, sheet_scope_cards_use_sheet_kind) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(/SHEET1/net1)", "Voltage"}});
    // act
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 1);
    ASSERT_TRUE(cards[0].is_scope);
    ASSERT_EQ(cards[0].label, "SHEET1");
    ASSERT_EQ(cards[0].kind, "sheet");
}

TEST(ExpressionTreeChecks, activate_toggles_expression_selection) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(2)", "Voltage"}});
    // act
    tree.activate(1);
    // assert
    const auto names = tree.selected_names();
    ASSERT_EQ(names.size(), 1);
    ASSERT_EQ(names[0], "V(2)");
}

TEST(ExpressionTreeChecks, selection_persists_across_scope_navigation) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}, {"V(XU1:45)", "Voltage"}});
    // act
    tree.activate(0);
    tree.activate(0);
    tree.open_breadcrumb(0);
    const auto names = tree.selected_names();
    // assert
    ASSERT_EQ(names.size(), 1);
    ASSERT_EQ(names[0], "V(XU1:23)");
}

TEST(ExpressionTreeChecks, selected_names_preserve_tree_order) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(2)", "Voltage"}, {"V(3)", "Voltage"}});
    // act
    tree.activate(2);
    tree.activate(0);
    const auto names = tree.selected_names();
    // assert
    ASSERT_EQ(names.size(), 2);
    ASSERT_EQ(names[0], "V(1)");
    ASSERT_EQ(names[1], "V(3)");
}

TEST(ExpressionTreeChecks, set_selected_refreshes_visible_cards) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(2)", "Voltage"}});
    // act
    tree.set_selected("V(2)", true);
    // assert
    const auto& cards = tree.cards();
    ASSERT_EQ(cards.size(), 2);
    ASSERT_FALSE(cards[0].selected);
    ASSERT_TRUE(cards[1].selected);
}

TEST(ExpressionTreeChecks, set_selected_missing_leaf_is_ignored) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}});
    // act
    tree.set_selected("V(99)", true);
    // assert
    const auto& cards = tree.cards();
    ASSERT_EQ(cards.size(), 1);
    ASSERT_FALSE(cards[0].selected);
}

TEST(ExpressionTreeChecks, set_selected_flags_existing_leaf) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(2)", "Voltage"}});
    // act
    tree.set_selected("V(2)", true);
    // assert
    const auto names = tree.selected_names();
    ASSERT_EQ(names.size(), 1);
    ASSERT_EQ(names[0], "V(2)");
}

TEST(ExpressionTreeChecks, contains_finds_leaf_by_name) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(XU1:23)", "Voltage"}});
    // act / assert
    ASSERT_TRUE(tree.contains("V(1)"));
    ASSERT_TRUE(tree.contains("V(XU1:23)"));
    ASSERT_FALSE(tree.contains("V(99)"));
}

TEST(ExpressionTreeChecks, add_leaf_appends_to_scope) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}});
    // act
    tree.add_leaf("V(XU1:45)", "Voltage");
    tree.activate(0);
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 2);
    ASSERT_EQ(cards[0].label, "V(23)");
    ASSERT_EQ(cards[1].label, "V(45)");
}

TEST(ExpressionTreeChecks, filter_flattens_matching_expressions) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(XU1:23)", "Voltage"}, {"V(XU1:45)", "Voltage"}, {"V(XU2:1)", "Voltage"}});
    // act
    tree.set_filter("XU1");
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 2);
    ASSERT_FALSE(cards[0].is_scope);
    ASSERT_EQ(cards[0].label, "V(XU1:23)");
    ASSERT_EQ(cards[1].label, "V(XU1:45)");
}

TEST(ExpressionTreeChecks, filter_is_case_insensitive) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}, {"V(2)", "Voltage"}});
    // act
    tree.set_filter("xu1");
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 1);
    ASSERT_EQ(cards[0].label, "V(XU1:23)");
}

TEST(ExpressionTreeChecks, filter_preserves_raw_file_order) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU2:1)", "Voltage"}, {"V(1)", "Voltage"}, {"V(XU1:23)", "Voltage"}});
    // act
    tree.set_filter("V(");
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 3);
    ASSERT_EQ(cards[0].label, "V(XU2:1)");
    ASSERT_EQ(cards[1].label, "V(1)");
    ASSERT_EQ(cards[2].label, "V(XU1:23)");
}

TEST(ExpressionTreeChecks, clearing_filter_restores_scoped_cards) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}, {"V(1)", "Voltage"}});
    // act
    tree.set_filter("XU1");
    tree.set_filter("");
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 2);
    ASSERT_TRUE(cards[1].is_scope);
}

TEST(ExpressionTreeChecks, filtering_suspends_breadcrumb) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}});
    tree.activate(0);
    // act
    tree.set_filter("V(XU1:23)");
    const auto& filtered = tree.breadcrumb();
    // assert
    ASSERT_TRUE(filtered.empty());
}

TEST(ExpressionTreeChecks, show_selected_flattens_selected_expressions) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(XU1:23)", "Voltage"}, {"V(XU1:45)", "Voltage"}});
    tree.activate(0);
    tree.set_selected("V(XU1:45)", true);
    // act
    tree.set_show_selected(true);
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 2);
    ASSERT_FALSE(cards[0].is_scope);
    ASSERT_EQ(cards[0].label, "V(1)");
    ASSERT_TRUE(cards[1].selected);
    ASSERT_EQ(cards[1].label, "V(XU1:45)");
}

TEST(ExpressionTreeChecks, selected_view_keeps_breadcrumb) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}});
    tree.activate(0);
    // act
    tree.set_show_selected(true);
    tree.set_selected("V(XU1:23)", true);
    const auto& breadcrumb = tree.breadcrumb();
    // assert
    ASSERT_EQ(breadcrumb.size(), 2);
    ASSERT_EQ(breadcrumb[1].label, "XU1");
}

TEST(ExpressionTreeChecks, toggling_show_selected_returns_to_scope) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(XU1:23)", "Voltage"}});
    tree.set_selected("V(1)", true);
    tree.set_show_selected(true);
    // act
    tree.set_show_selected(false);
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 2);
    ASSERT_TRUE(cards[1].is_scope);
}

TEST(ExpressionTreeChecks, show_selected_defaults_to_false) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}});
    // act / assert
    ASSERT_FALSE(tree.show_selected());
}

TEST(ExpressionTreeChecks, open_breadcrumb_exits_selected_view) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}});
    tree.activate(0);
    tree.set_selected("V(XU1:23)", true);
    tree.set_show_selected(true);
    // act
    tree.open_breadcrumb(0);
    const auto& cards = tree.cards();
    // assert
    ASSERT_FALSE(tree.show_selected());
    ASSERT_EQ(cards.size(), 1);
    ASSERT_TRUE(cards[0].is_scope);
}

TEST(ExpressionTreeChecks, filter_takes_precedence_over_selected_view) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(2)", "Voltage"}});
    tree.set_selected("V(1)", true);
    tree.set_show_selected(true);
    // act
    tree.set_filter("V(2)");
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 1);
    ASSERT_EQ(cards[0].label, "V(2)");
    ASSERT_FALSE(cards[0].selected);
}

TEST(ExpressionTreeChecks, rebuild_resets_selected_view) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}});
    tree.set_selected("V(1)", true);
    tree.set_show_selected(true);
    // act
    tree.rebuild({{"V(1)", "Voltage"}});
    // assert
    ASSERT_FALSE(tree.show_selected());
    ASSERT_TRUE(tree.selected_names().empty());
}

TEST(ExpressionTreeChecks, deselecting_in_selected_view_removes_the_card) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(2)", "Voltage"}});
    tree.set_selected("V(1)", true);
    tree.set_selected("V(2)", true);
    tree.set_show_selected(true);
    // act
    tree.activate(0);
    const auto& cards = tree.cards();
    // assert
    ASSERT_EQ(cards.size(), 1);
    ASSERT_EQ(cards[0].label, "V(2)");
}

TEST(ExpressionTreeChecks, activate_out_of_range_is_ignored) {
    // arrange
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}});
    // act
    tree.activate(99);
    // assert
    const auto& cards = tree.cards();
    ASSERT_EQ(cards.size(), 1);
}

namespace
{
    // real expression with owning data, mirroring the raw file parser output
    Expression<double> make_probe(const std::string& name, const std::string& unit) {
        std::vector<double> data = {1.0, 2.0, 3.0};
        return {name, std::move(data), std::vector<std::pair<size_t, size_t>>{{0, 3}}, unit};
    }
} // namespace

TEST(ExpressionTreeChecks, reopen_dialog_flow_marks_plotted_compound_selected) {
    // arrange: manager with raw probes; the user creates the compound through
    // the expression builder, plots it, and reopens the dialog
    const std::string compound = "I(XU302:X1:X1:E1)-I(XU302:X1:X1:D3)";
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(make_probe("time", "s"));
    expressions.emplace_back(make_probe("I(XU302:X1:X1:E1)", "A"));
    expressions.emplace_back(make_probe("I(XU302:X1:X1:D3)", "A"));
    std::vector<std::pair<size_t, size_t>> slices = {{0, 3}};
    ExpressionManager manager(expressions, slices);
    // builder path: the evaluated expression keeps the requested text
    AnyExpression* plotted = manager.evaluate(compound, compound);
    ASSERT_NE(plotted, nullptr);
    // act: reopen flow — rebuild the tree from all expressions, then mark the
    // plotted expressions selected exactly like the dialog view does
    ExpressionTree tree;
    std::vector<std::pair<std::string, std::string>> items;
    for (AnyExpression* expression : manager.expressions()) {
        const auto name = std::visit([](const auto& e) { return e.name(); }, *expression);
        const auto type = std::visit([](const auto& e) { return e.unit(); }, *expression);
        items.emplace_back(name, type);
    }
    tree.rebuild(items);
    // chart side: the plotted series resolves back through the manager
    AnyExpression* resolved = manager.evaluate(compound);
    ASSERT_NE(resolved, nullptr);
    const auto resolved_name = std::visit([](const auto& e) { return e.name(); }, *resolved);
    tree.set_selected(resolved_name, true);
    // assert: the compound card at the root is selected with the current unit
    const auto& cards = tree.cards();
    bool found = false;
    for (const ExpressionCard& card : cards) {
        if (card.full_name == compound) {
            found = true;
            ASSERT_FALSE(card.is_scope);
            ASSERT_TRUE(card.selected);
            ASSERT_EQ(card.type, "A");
        }
    }
    ASSERT_TRUE(found);
}

TEST(ExpressionTreeChecks, distinguishes_subcircuit_and_sheet_with_same_label) {
    // arrange: subcircuit XU1 and sheet XU1 share the label but have different kinds
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:23)", "Voltage"}, {"V(/XU1/net1)", "Voltage"}});
    // act: root should have two separate scope cards (one subcircuit, one sheet)
    const auto& root_cards = tree.cards();
    ASSERT_EQ(root_cards.size(), 2);
    // assert: both are scopes, different kinds, each contains one expression
    bool saw_subcircuit = false;
    bool saw_sheet = false;
    for (const ExpressionCard& card : root_cards) {
        ASSERT_TRUE(card.is_scope);
        ASSERT_EQ(card.label, "XU1");
        ASSERT_EQ(card.count, 1);
        if (card.kind == "subcircuit")
            saw_subcircuit = true;
        else if (card.kind == "sheet")
            saw_sheet = true;
    }
    ASSERT_TRUE(saw_subcircuit);
    ASSERT_TRUE(saw_sheet);
}

TEST(ExpressionTreeChecks, differential_probe_prefix_stripped_per_operand) {
    // arrange: differential probe with both nodes under the same subcircuit
    ExpressionTree tree;
    tree.rebuild({{"V(XU1:1,XU1:2)", "Voltage"}});
    // act: drill into the XU1 scope
    tree.activate(0);
    const auto& cards = tree.cards();
    // assert: both operands are stripped to scope-relative form
    ASSERT_EQ(cards.size(), 1);
    ASSERT_FALSE(cards[0].is_scope);
    ASSERT_EQ(cards[0].label, "V(1,2)");
    ASSERT_EQ(cards[0].full_name, "V(XU1:1,XU1:2)");
}

TEST(ExpressionTreeChecks, rebuild_clears_filter) {
    // arrange: tree with a filter active
    ExpressionTree tree;
    tree.rebuild({{"V(1)", "Voltage"}, {"V(XU1:23)", "Voltage"}});
    tree.set_filter("XU1");
    // filter is active: cards are flattened
    ASSERT_EQ(tree.cards().size(), 1);
    // act: rebuild the tree
    tree.rebuild({{"V(1)", "Voltage"}, {"V(XU1:23)", "Voltage"}});
    // assert: filter is cleared, scoped view restored
    const auto& cards = tree.cards();
    ASSERT_EQ(cards.size(), 2);
    ASSERT_FALSE(cards[0].is_scope);
    ASSERT_EQ(cards[0].label, "V(1)");
    ASSERT_TRUE(cards[1].is_scope);
    ASSERT_EQ(cards[1].label, "XU1");
}

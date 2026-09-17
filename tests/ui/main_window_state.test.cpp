#include <gtest/gtest.h>

#include "ui/main_window_state.h"

// ========================================================================================
// state derivation
// ========================================================================================

TEST(MainWindowStateChecks, empty_input_yields_empty_state) {
    // arrange
    ActionStateInput input;
    // act
    const AppState state = derive_app_state(input);
    // assert
    EXPECT_EQ(state, AppState::Empty);
}

TEST(MainWindowStateChecks, netlist_without_raw_yields_editing_state) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    // act
    const AppState state = derive_app_state(input);
    // assert
    EXPECT_EQ(state, AppState::Editing);
}

TEST(MainWindowStateChecks, netlist_and_raw_with_hidden_charts_yields_netlist_with_results_state) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.has_raw = true;
    // act
    const AppState state = derive_app_state(input);
    // assert
    EXPECT_EQ(state, AppState::NetlistWithResults);
}

TEST(MainWindowStateChecks, netlist_and_raw_with_shown_charts_yields_chart_state) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.has_raw = true;
    input.charts_shown = true;
    // act
    const AppState state = derive_app_state(input);
    // assert
    EXPECT_EQ(state, AppState::Chart);
}

TEST(MainWindowStateChecks, raw_with_shown_charts_without_netlist_yields_chart_only_state) {
    // arrange
    ActionStateInput input;
    input.has_raw = true;
    input.charts_shown = true;
    // act
    const AppState state = derive_app_state(input);
    // assert
    EXPECT_EQ(state, AppState::ChartOnly);
}

TEST(MainWindowStateChecks, simulation_running_overrides_all_other_inputs) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.has_raw = true;
    input.charts_shown = true;
    input.simulation_running = true;
    // act
    const AppState state = derive_app_state(input);
    // assert
    EXPECT_EQ(state, AppState::Running);
}

TEST(MainWindowStateChecks, simulation_running_with_no_content_yields_running_state) {
    // arrange
    ActionStateInput input;
    input.simulation_running = true;
    // act
    const AppState state = derive_app_state(input);
    // assert
    EXPECT_EQ(state, AppState::Running);
}

// ========================================================================================
// enablement matrix
// ========================================================================================

TEST(MainWindowStateChecks, empty_state_only_enables_open) {
    // arrange
    ActionStateInput input;
    input.output_hidden = true;
    input.log_has_content = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_TRUE(enablement.open);
    EXPECT_FALSE(enablement.save);
    EXPECT_FALSE(enablement.show_netlist);
    EXPECT_FALSE(enablement.show_charts);
    EXPECT_FALSE(enablement.show_simulation_output);
    EXPECT_FALSE(enablement.configure_simulation);
    EXPECT_FALSE(enablement.run_simulation);
}

TEST(MainWindowStateChecks, editing_state_enables_netlist_dependent_actions) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_TRUE(enablement.open);
    EXPECT_TRUE(enablement.configure_simulation);
    EXPECT_TRUE(enablement.run_simulation);
    EXPECT_FALSE(enablement.save);
    EXPECT_FALSE(enablement.show_netlist);
    EXPECT_FALSE(enablement.show_charts);
    EXPECT_FALSE(enablement.show_simulation_output);
}

TEST(MainWindowStateChecks, editing_state_without_file_does_not_enable_save) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.netlist_editor_dirty = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_FALSE(enablement.save);
}

TEST(MainWindowStateChecks, editing_state_with_dirty_file_enables_save) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.has_netlist_file = true;
    input.netlist_editor_dirty = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_TRUE(enablement.save);
}

TEST(MainWindowStateChecks, editing_state_clean_editor_does_not_enable_save) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_FALSE(enablement.save);
}

TEST(MainWindowStateChecks, editing_state_with_hidden_log_output_enables_show_simulation_output) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.output_hidden = true;
    input.log_has_content = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_TRUE(enablement.show_simulation_output);
}

TEST(MainWindowStateChecks, running_state_disables_every_action) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.has_raw = true;
    input.charts_shown = true;
    input.simulation_running = true;
    input.netlist_editor_dirty = true;
    input.output_hidden = true;
    input.log_has_content = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_FALSE(enablement.open);
    EXPECT_FALSE(enablement.save);
    EXPECT_FALSE(enablement.show_netlist);
    EXPECT_FALSE(enablement.show_charts);
    EXPECT_FALSE(enablement.show_simulation_output);
    EXPECT_FALSE(enablement.configure_simulation);
    EXPECT_FALSE(enablement.run_simulation);
}

TEST(MainWindowStateChecks, chart_state_enables_show_netlist) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.has_raw = true;
    input.charts_shown = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_TRUE(enablement.show_netlist);
    EXPECT_TRUE(enablement.configure_simulation);
    EXPECT_TRUE(enablement.run_simulation);
    EXPECT_FALSE(enablement.show_charts);
}

TEST(MainWindowStateChecks, netlist_with_results_state_enables_show_charts) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.has_raw = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_TRUE(enablement.show_charts);
    EXPECT_TRUE(enablement.configure_simulation);
    EXPECT_TRUE(enablement.run_simulation);
    EXPECT_FALSE(enablement.show_netlist);
}

TEST(MainWindowStateChecks, netlist_with_results_state_with_dirty_file_enables_save) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.has_netlist_file = true;
    input.has_raw = true;
    input.netlist_editor_dirty = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_TRUE(enablement.save);
}

TEST(MainWindowStateChecks, chart_only_state_only_enables_open) {
    // arrange
    ActionStateInput input;
    input.has_raw = true;
    input.charts_shown = true;
    input.output_hidden = true;
    input.log_has_content = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_TRUE(enablement.open);
    EXPECT_FALSE(enablement.save);
    EXPECT_FALSE(enablement.show_netlist);
    EXPECT_FALSE(enablement.show_charts);
    EXPECT_FALSE(enablement.show_simulation_output);
    EXPECT_FALSE(enablement.configure_simulation);
    EXPECT_FALSE(enablement.run_simulation);
}

TEST(MainWindowStateChecks, visible_output_panel_disables_show_simulation_output) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.log_has_content = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_FALSE(enablement.show_simulation_output);
}

TEST(MainWindowStateChecks, empty_log_disables_show_simulation_output) {
    // arrange
    ActionStateInput input;
    input.has_netlist = true;
    input.output_hidden = true;
    // act
    const ActionStateEnablement enablement = compute_action_enablement(input);
    // assert
    EXPECT_FALSE(enablement.show_simulation_output);
}

// ========================================================================================
// state names
// ========================================================================================

TEST(MainWindowStateChecks, state_name_maps_every_state) {
    // assert
    EXPECT_EQ(app_state_name(AppState::Empty), "empty");
    EXPECT_EQ(app_state_name(AppState::Editing), "editing");
    EXPECT_EQ(app_state_name(AppState::Running), "running");
    EXPECT_EQ(app_state_name(AppState::Chart), "chart");
    EXPECT_EQ(app_state_name(AppState::NetlistWithResults), "netlist-with-results");
    EXPECT_EQ(app_state_name(AppState::ChartOnly), "charts-only");
}

TEST(MainWindowStateChecks, smith_flag_passes_through_to_the_enablement) {
    // arrange — input flags with the smith dataset active
    ActionStateInput input;
    input.has_netlist = true;
    input.has_raw = true;
    input.charts_shown = true;
    input.charts_smith = true;
    // act
    const auto enablement = compute_action_enablement(input);
    // assert
    EXPECT_TRUE(enablement.charts_smith);
}

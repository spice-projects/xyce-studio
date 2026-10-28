#include "main_window_state.h"

AppState derive_app_state(const ActionStateInput& input) {
    // simulation is in progress
    if (input.simulation_running)
        return AppState::Running;
    // charts view with both a netlist and raw output available
    if (input.has_raw && input.charts_shown && input.has_netlist)
        return AppState::Chart;
    // charts-only view (raw output without a netlist)
    if (input.has_raw && input.charts_shown)
        return AppState::ChartOnly;
    // netlist view while raw output is available
    if (input.has_raw && input.has_netlist)
        return AppState::NetlistWithResults;
    // netlist view without raw output
    if (input.has_netlist)
        return AppState::Editing;
    // no content at all
    return AppState::Empty;
}

ActionStateEnablement compute_action_enablement(const ActionStateInput& input) {
    // state for this input
    const AppState state = derive_app_state(input);
    // netlist-dependent actions are available while a netlist is loaded
    const bool netlist_actions_enabled = state == AppState::Editing || state == AppState::Chart || state == AppState::NetlistWithResults;
    // save is possible when the editor is dirty and backed by a netlist file
    const bool save_enabled = input.netlist_editor_dirty && netlist_actions_enabled && input.has_netlist_file;
    // simulation output can be re-shown when the splitter is hidden and the log holds content
    const bool show_output_enabled = input.output_hidden && input.log_has_content && netlist_actions_enabled;
    // build the enablement result
    ActionStateEnablement enablement;
    enablement.open = state != AppState::Running;
    enablement.save = save_enabled;
    enablement.show_netlist = state == AppState::Chart;
    enablement.show_charts = state == AppState::NetlistWithResults;
    enablement.show_simulation_output = show_output_enabled;
    enablement.configure_simulation = netlist_actions_enabled;
    enablement.run_simulation = netlist_actions_enabled;
    // chart context tools: FFT for time-domain abscissa, step tool for stepped runs
    enablement.fft = input.abscissa_is_time;
    enablement.step_tool = input.has_steps;
    // smith chart tabs drive the panel's cartesian-tool visibility
    enablement.charts_smith = input.charts_smith;
    // return the enablement result
    return enablement;
}

std::string_view app_state_name(const AppState state) {
    // map state to its name for logging
    switch (state) {
    case AppState::Empty:
        return "empty";
    case AppState::Editing:
        return "editing";
    case AppState::Running:
        return "running";
    case AppState::Chart:
        return "chart";
    case AppState::NetlistWithResults:
        return "netlist-with-results";
    case AppState::ChartOnly:
        return "charts-only";
    }
    // fallback for unknown states
    return "unknown";
}

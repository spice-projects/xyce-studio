# Feature parity audit (Phase 7.1)

This document records the audit that compares the legacy Slint frontend
(`xyce-studio`) with the native Albor frontend (`xyce-studio-next`) before the
executable cutover (7.3) and the retirement of `slint/` (7.4).

## Scope and method

The audit covers the four areas the migration plan names — netlist editing,
every simulation type, chart interactions and dialog results — plus the
surfaces around them (toolbar, document strip, output panel, CLI, KiCad plugin
mode).

1. **Static capability matrix** — every user-visible surface of both roots was
   read from the sources and compared.
2. **Automated suite mapping** — all 75 legacy UI scenarios (`tests_ui/`) were
   mapped to a native scenario, a native unit test, or an accepted difference;
   the gaps were ported (`tests_studio/`).
3. **Simulation sweep** — Xyce-backed runs cover a transient print, a DC sweep
   in `.prn` STD format, a PCE companion, a stepped `.FOUR`, a `.LIN`
   touchstone dataset on the Smith plane, and run-directory lifetime.
4. **Dialog results** — every analysis page, the options editor, the FFT
   dialog, the plot selection dialog and the plugin configuration were driven
   end to end.
5. **Regression suites** — both unit suites and the native UI suite were run;
   the legacy suite was run as the baseline.

Verdicts used below:

- **Covered** — a native scenario or unit test proves the same capability.
- **Ported** — a native scenario was added by this audit.
- **Accepted** — an intentional difference, with the reason.
- **N/A** — a Slint-layout specific assertion with no native equivalent.

## 1. Capability matrix

| Surface | Legacy | Native | Verdict |
| --- | --- | --- | --- |
| CLI `--xyce` | yes | yes | matches |
| CLI `--netlist` (`.cir` only) | yes | yes | matches |
| CLI output file at startup | `--raw` | `--dataset`, plus `--raw` alias added | matches |
| CLI `--log-level` / `-l` | yes | added | matches |
| CLI joined and separated values | yes | yes | matches |
| Toolbar: open, save, run/stop, configure, output, settings, exit | yes | yes | matches |
| Netlist vs charts switch | toolbar tools | document strip with dataset tabs | accepted (driver #1) |
| Simulation parameters | inline modal panel | native window | accepted (driver #1) |
| Options editor | inline modal panel | native window | matches |
| FFT setup | inline modal panel | native window | matches |
| Plot selection / Add Plot | inline modal panel | native window | matches |
| Plugin configuration | inline modal panel | native window | matches |
| All analyses (OP, DC, TRAN, AC, NOISE, HB, LIN) | yes | yes | matches |
| Options packages (analysis and global) | yes | yes | matches |
| PCE, sensitivity, `.MEASURE`, `.IC`, print companions | yes | yes | matches |
| Charts: line, Smith, zoom drag, hover, steps, reorder | yes | yes | matches (hover proven by unit tests) |
| Chart context menu (fit, autorange, extent, plots, FFT, steps, stack, new window) | yes | yes | matches (step tool now also on Smith) |
| Fourier results table | yes | yes | matches |
| Simulation output panel | log, severity badges, first-warning jump | log, severity badges | accepted (see 4.2) |
| Netlist editing, dirty state, save, save-as | yes | yes | matches |
| KiCad plugin mode (schematic source, read-only, locked file actions) | yes | yes | matches |
| Multi-window (chart window) | yes | yes | matches |

## 2. Legacy scenario mapping

Every scenario in `tests_ui/` (75) appears once. The evidence column names the
native scenario (`tests_studio/...`) or the native unit suite that proves the
capability.

### charts_context_test.py (8)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| add_chart_appends_after_the_declared_charts | Covered | `charts_controller.test.cpp::the_chart_stack_commands_insert_and_delete_around_the_target` |
| add_chart_inserts_after_the_targeted_chart | Covered | same unit test |
| add_chart_joins_the_shared_zoomed_abscissa_range | Covered | `charts_controller.test.cpp::a_chart_added_after_a_zoom_joins_the_shared_abscissa` |
| delete_chart_removes_the_targeted_chart | Covered | `charts_test.py::test_the_chart_context_menu_commands_change_the_panel` |
| delete_chart_keeps_a_blank_chart_when_the_last_is_removed | Covered | `charts_controller.test.cpp::the_chart_stack_commands_insert_and_delete_around_the_target` |
| autorange_resets_the_ordinate_axis_of_the_targeted_chart | Covered | `charts_controller.test.cpp::the_zoom_commands_reset_the_axes_the_context_menu_names` |
| drag_handle_moves_the_chart_to_the_target_slot | Covered | `charts_test.py::test_moving_a_chart_through_the_semantic_action_keeps_the_stack` |
| zoom_to_fit_restores_the_full_abscissa_range | Covered | `charts_controller.test.cpp::the_zoom_commands_reset_the_axes_the_context_menu_names` |

### charts_test.py (6)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| run_simulation_shows_the_charts_panel | Covered | `charts_test.py::test_a_successful_run_auto_opens_the_charts` |
| transient_tab_shows_the_full_abscissa_range | Covered | `chart_layout.test.cpp` extent tests |
| fft_tab_shows_the_normalized_fft_ranges | Covered | `charts_test.py::test_the_fft_dialog_appends_a_frequency_dataset` and `chart_layout.test.cpp` |
| fft_tab_zoom_applies_full_2d_on_dragged_chart_and_horizontal_only_on_the_other | Covered | `charts_controller.test.cpp::a_zoom_drag_updates_the_dragged_chart_in_two_axes_and_the_others_in_one` |
| descending_dc_sweep_shows_the_reversed_abscissa_range | Ported | `charts_controller.test.cpp::a_descending_sweep_maps_its_extent_left_to_right` |
| fft_tab_2_shows_the_pure_fft_range | Covered | `chart_layout.test.cpp` extent tests |

### default_plots_test.py (6)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| transient_plot_directives_open_one_chart_per_line | Covered | `charts_test.py::test_a_run_uses_the_plot_directives_for_its_charts` |
| ac_plot_directive_plots_the_computed_series | Covered | `tests/simulation/plot_parameters.test.cpp` grouping |
| several_expressions_on_one_line_fill_a_single_chart | Covered | `tests/simulation/plot_parameters.test.cpp` grouping |
| compound_voltage_difference_plots_from_the_node_voltages | Covered | `tests/simulation/plot_parameters.test.cpp` compound expansion |
| plotted_quantity_missing_from_the_print_is_added_to_it | Ported | `default_plots_test.py::test_a_plotted_quantity_missing_from_the_print_is_added_to_it` (behaviour fixed in this audit) |
| charts_follow_the_plot_directives_edited_before_a_rerun | Ported | `default_plots_test.py::test_the_charts_follow_the_plot_directives_edited_before_a_rerun` |

### expression_legend_test.py (2)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| add_plot_legend_lists_dataset_units_and_grows_with_custom_expressions | Ported | `legend_test.py` (the abscissa is not offered, see 4.3) |
| fft_legend_excludes_the_time_unit | Ported | `legend_test.py` FFT half |

### fourier_output_test.py (2)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| fourier_results_panel_shows_spectrum_table | Covered | `charts_test.py::test_a_fourier_output_opens_as_its_results_table`, `tests/charts/fourier_results.test.cpp` |
| fourier_results_panel_labels_each_sweep_step | Ported | `outputs_test.py::test_a_stepped_fourier_run_labels_each_step` |

### from_scratch_netlist_test.py (4)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| run_opens_the_simulation_dialog_and_keeps_the_typed_netlist | Ported | `from_scratch_test.py::test_a_run_without_an_analysis_asks_for_one_and_keeps_the_netlist` (behaviour fixed in this audit) |
| configure_a_typed_netlist_and_simulate_it | Ported | `from_scratch_test.py::test_a_second_run_uses_the_edited_transient_parameters` |
| save_stays_enabled_after_running_a_typed_netlist | Ported | `from_scratch_test.py::test_a_run_keeps_the_typed_netlist_dirty` |
| save_persists_a_typed_netlist_to_a_chosen_file | Ported | `from_scratch_test.py::test_saving_a_typed_netlist_writes_the_chosen_file` |

### ic_parameters_test.py (4)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| transient_ic_line_merges_two_statements_into_one | Ported | `ic_test.py::test_two_ic_statements_merge_into_one_line` |
| dc_ic_line_round_trips | Ported | `ic_test.py::test_the_dc_page_round_trips_the_ic_line` |
| ac_tab_keeps_the_ic_line_as_pass_through | Ported | `ic_test.py::test_the_ac_page_keeps_the_ic_line` |
| ic_line_validation_rejects_unparseable_text | Ported | `ic_test.py::test_unparseable_ic_text_keeps_the_dialog_open` (validation added in this audit) |

### kicad_reconfigure_test.py (1)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| second_run_uses_the_edited_transient_parameters | Covered | `kicad_test.py::test_a_schematic_run_uses_the_directives_the_dialog_accepted` |

### lin_touchstone_test.py (4)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| lin_run_opens_touchstone_tab_with_suggested_charts | Covered | `charts_test.py::test_touchstone_dataset_opens_a_smith_chart` |
| lin_run_with_step_opens_touchstone_tab_with_stepped_charts | Covered | `smith_test.py` stepped dataset, `charts_controller.test.cpp` Smith tests |
| smith_chart_shows_axes_tick_labels_and_hover_readout | Covered | `charts_controller.test.cpp::a_smith_dataset_builds_a_smith_frame_and_reads_the_gamma_plane` (UI hover is not drivable, see 4.6) |
| step_tool_opens_from_the_smith_chart_context_menu | Ported | `smith_test.py` (behaviour fixed in this audit) |

### netlist_editor_test.py (1)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| edit_and_save_netlist_round_trip | Covered | `netlist_editor_test.py::test_edit_and_save_netlist_round_trip` and `test_reopened_netlist_starts_clean` |

### options_test.py (4)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| edit_general_option_regenerates_the_options_line | Covered | `options_test.py::test_the_editor_writes_an_accepted_option_and_keeps_the_analysis` |
| escape_closes_the_options_dialog_without_changing_the_netlist | Covered | `options_test.py::test_cancel_leaves_the_netlist_untouched` |
| accept_keeps_bare_flag_options_until_they_are_cleared | Covered | `options_test.py::test_a_documented_choice_replaces_a_bare_flag`, `tests/simulation/option_editor.test.cpp` |
| configure_dialog_edits_timeint_option_and_selects_a_method_choice | Covered | `simulation_parameters_test.py::test_option_packages_and_companion_sections_round_trip` |

### pce_output_test.py (1)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| pce_print_loads_pce_tab_next_to_transient | Ported | `outputs_test.py::test_a_pce_print_run_opens_the_pce_dataset` |

### pce_parameters_test.py (3)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| pce_parameters_round_trip | Covered | `simulation_parameters_test.py::test_pce_section_round_trips` |
| pce_seeded_from_netlist_directives | Covered | same native scenario |
| pce_run_produces_print_output_file | Ported | `outputs_test.py::test_a_pce_print_run_opens_the_pce_dataset` |

### plugin_config_test.py (2)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| config_dialog_shows_configured_xyce_path | Covered | `plugin_config_test.py::test_dialog_shows_configured_path_on_open` |
| configured_path_persists_across_launches | Covered | `plugin_config_test.py::test_accepted_path_persists_across_launches` |

### prn_output_test.py (1)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| std_print_format_loads_prn_file | Ported | `outputs_test.py::test_a_std_print_run_opens_the_prn_dataset` |

### simulation_parameters_test.py (9)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| transient_parameters_round_trip | Covered | `simulation_parameters_test.py::test_transient_netlist_seeds_the_dialog`, `test_accept_rewrites_the_transient_directive` |
| dc_sweep_validation_rejects_empty_sweep | Covered | `simulation_parameters_test.py::test_dc_tab_reports_the_missing_sweep_variable` |
| transient_print_checkboxes_fit_the_panel | N/A | Slint row geometry; native rows are generated from the manifest |
| toggling_print_checkbox_does_not_reflow_the_row | N/A | same |
| ac_print_checkbox_does_not_reflow_the_row | N/A | same |
| lin_print_checkbox_does_not_reflow_the_row | N/A | same |
| noise_print_checkbox_does_not_reflow_the_row | N/A | same |
| hb_print_checkbox_does_not_reflow_the_row | N/A | same |
| cancel_and_escape_keep_original_values | Covered | `simulation_parameters_test.py::test_cancel_discards_the_edit` (the Cancel button is the dialog's cancel action) |

### simulation_reconfigure_test.py (1)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| second_run_uses_the_edited_transient_parameters | Ported | `from_scratch_test.py::test_a_second_run_uses_the_edited_transient_parameters` |

### simulation_run_test.py (5)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| output_panel_shows_and_retains_simulation_log | Covered | `simulation_output_test.py::test_run_shows_the_output_log_and_close_hides_the_panel` |
| run_reports_invalid_configured_executable | Covered | `main_window_presenter.test.cpp::run_refuses_an_invalid_xyce_executable` |
| statusbar_reports_live_progress_and_opens_the_output_panel | Covered | `simulation_output_test.py::test_status_reports_the_run_progress_while_it_is_live` |
| output_panel_counts_the_warnings_and_navigates_to_the_first | Accepted | badges count warnings/errors; no jump-to-first (4.2) |
| exit_terminates_the_application | Covered | `main_window_test.py::test_exit_action_closes_the_shell` |

### smoke_test.py (4)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| application_starts | Covered | every `tests_studio` scenario, `main_window_test.py` |
| main_window_appears | Covered | `main_window_test.py::test_toolbar_actions_announce_their_purpose` |
| toolbar_element_is_located | Covered | same native scenario |
| initial_status_text_is_empty | Covered | `netlist_editor_test.py::test_untitled_editor_starts_clean_and_empty` |

### temp_output_cleanup_test.py (2)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| previous_run_directory_is_removed_and_app_exit_clears_the_latest | Ported | `temp_cleanup_test.py::test_the_previous_run_directory_is_removed_and_exit_clears_the_latest` |
| exit_while_a_run_is_in_flight_reaps_the_runner_and_removes_its_directory | Ported | `temp_cleanup_test.py::test_exit_while_a_run_is_in_flight_removes_its_directory` |

### toolbar_test.py (3)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| toolbar_contains_ten_tools_in_expected_state | Accepted | the native toolbar keeps the action tools; the Netlist/Charts tools are the document strip (4.1) |
| run_simulation_updates_toolbar_and_panels | Covered | `simulation_output_test.py` run/stop/progress scenarios |
| toolbar_state_after_loading_netlist_file | Ported | `toolbar_test.py::test_loading_a_netlist_enables_the_document_actions` |

### typed_netlist_run_test.py (2)

| Legacy scenario | Verdict | Native evidence |
| --- | --- | --- |
| save_tool_stays_enabled_after_running_the_typed_netlist | Ported | `from_scratch_test.py::test_a_run_keeps_the_typed_netlist_dirty` |
| edited_source_line_survives_the_second_simulation_run | Ported | same scenario now runs twice and asserts the text |

## 3. Issues found and fixed by this audit

| # | Issue | Fix | Tests |
| --- | --- | --- | --- |
| 1 | A run over a netlist without an analysis launched the raw netlist instead of asking for one | `MainWindowPresenter` sets a pending run, opens the parameters dialog and resumes on accept; a cancellation drops it | `main_window_presenter.test.cpp` (3), `simulation_parameters_presenter.test.cpp` (3), `from_scratch_test.py` |
| 2 | A quantity named only by `.PLOT` was never printed, so its chart stayed empty | The plotted variables join the analysis print and the run netlist is rendered from the augmented configuration | `default_plots_test.py` (2) |
| 3 | The step tool was hidden on Smith charts | The context menu and the semantic action offer it on both planes | `smith_test.py`, `chart_view.test.cpp` |
| 4 | The CLI lacked `--log-level`/`-l` and the `--raw` spelling; parsing lived in `main` and was untested | `app/startup_options` parses every option (both spellings, accumulation, `.cir` filter) and `main` applies it | `startup_options.test.cpp` (6) |
| 5 | Unparseable initial-conditions text was silently dropped on accept | The dialog refuses it with "Invalid initial conditions" | `ic_test.py` |
| 6 | The initial-conditions field had no automation id | One shared `simulation_ic` id, unique in the tree because only the selected page exists | `ic_test.py` (4) |
| 7 | The legend announced no categories and both dialogs shared one legend id | Each dialog owns its legend (`add_plot_legend`, `fft_legend`) and the announcement lists the categories | `legend_test.py` |
| 8 | A typed netlist could only be saved through the platform panel, which automation cannot reach | `XYCE_STUDIO_SAVE_PATH` supplies the destination in automation builds | `from_scratch_test.py` |
| 9 | A dialog opened before the view was seeded described an empty document | The presenter falls back to the source content when the editor is empty | unit suites |
| 10 | No native proof for a descending sweep, the Smith step tool, temp cleanup on exit, or save-as | Ported scenarios listed above | see 2 |
| 11 | The plot selection marked a complex AC source selected because one of its derived series was plotted, so a chart plotting only `db(I(L1))` showed both `db(I(L1))` and `I(L1)` as displayed (reported after the audit) | A series counts only under its own name, so a complex source is selected through the derived `db`/`phase` rows the chart actually plots | `charts_presenter.test.cpp` (2), `complex_plots_test.py` (2) |
| 12 | A run always opened the output panel, so a quiet run replaced the charts with an empty-looking log (reported after the audit) | The panel starts hidden and opens on the first warning or error of the run, respects a manual close for the rest of that run, hides after a run that produced results, and stays visible on cancel/failure/no-result; the toolbar action is enabled only while a hidden log holds content | `main_window_presenter.test.cpp` (6), `simulation_output_test.py` (3) |
| 13 | A standalone launch logged `KIPRJMOD environment variable is not set` as an error, although starting outside KiCad is the normal case (reported after the audit) | `KiCadSession::from_environment()` asks `KiCadConnection::is_kicad_mode()` first, so the project variable is only validated for a plugin launch; a plugin launch that omits it still reports the error | `kicad_session.test.cpp` standalone scenarios; both startup logs checked by hand |
| 14 | A run-produced dataset whose file name carried no analysis marker was typed by its name alone, so the chart context menu omitted FFT even when the abscissa was time; every KiCad schematic run hit this (reported after the audit) | The run stamps the analysis its configured print names onto the parsed dataset, the way the legacy metadata stamp did, and FFT availability follows the abscissa unit instead of the file-name guess | `charts_presenter.test.cpp` (2), `charts_test.py` (1) |

## 4. Accepted differences

### 4.1 Dialogs and document switching

The migration drivers keep the native dialogs as separate OS windows and the
document strip as the dataset switcher. The legacy inline modal panels and its
Netlist/Charts toolbar toggles therefore have no 1:1 control; every capability
they expose exists natively (verified by the scenarios above).

### 4.2 Output-panel severity navigation

Legacy lets a click on the warning/error counters jump to the first matching
log line. The native panel shows the same counts and its log is a selectable,
copyable text field, but it has no jump-to-first. Adding it needs a clickable
badge widget; it is left as a follow-up so the audit does not change the
output panel's design at cutover time.

### 4.3 Plot selection does not offer the abscissa

The native plot selection lists signals only; the legacy dialog also offered the
abscissa (`time`) as a series. Plotting the abscissa against itself is not
meaningful, so the native legend lists signal categories only. The FFT dialog
excludes the abscissa in both frontends.

### 4.4 `--raw` accepts any dataset

Legacy filters `--raw` by known analysis-output extensions. The native alias
accepts any path and lets the chart loader refuse an unreadable file, which
matches the existing `--dataset` behaviour and rejects the file with a log line
instead of silently ignoring it.

### 4.5 Slint-specific layout assertions

The legacy print-checkbox reflow tests assert Slint row geometry. Native rows
are generated from the manifest and laid out by Albor; the equivalent values are
asserted by the round-trip scenarios, and there is no reflow to regress.

### 4.6 Pointer-driven hover in the UI harness

The Albor automation host has no pointer-move command, so the chart and Smith
hover readouts are proven by unit tests (`chart_view.test.cpp`,
`charts_controller.test.cpp`) rather than by a UI scenario. The readout itself is
reachable through the view callbacks and covered end to end by the controller
tests.

### 4.7 The output panel opens on a diagnostic

Legacy clears the log when a run starts but leaves the panel as the user left it,
and no log line opens it; the toolbar action is the only way in until the run
ends. The native panel follows the requested behaviour instead: the run starts
hidden, the first warning or error of the run opens it by itself (once, so a
manual close is respected), a run that produced results hides it again, and a
cancel, failure or resultless run leaves it visible. This is a superset of the
legacy behaviour, so no legacy scenario regresses.

## 5. Verification

| Check | Result |
| --- | --- |
| `tests_studio` (native UI) | 93 scenarios pass (was 69) |
| `studio-tests` (native unit) | 209 tests pass |
| `tests` (shared unit) | 2999 tests pass |
| `tests_ui` (legacy baseline) | 75 scenarios attempted; 53 pass, the other launches exceed the harness's 30 s startup budget on this machine (the Slint application does reach its MCP server when launched by hand). Phase 4 recorded 75/75 on CI; the application itself is unchanged |
| Formatter | `build/check-format.sh` clean |
| Release build | `studio-release` clean |

Run the suites with the display held awake, because the automation host serves
one request per frame:

```bash
caffeinate -dimsu .venv/bin/python -m unittest discover -s tests_studio -p "*_test.py"
./.build-studio-debug/studio-tests/xyce-studio-tests
./.build-debug/tests/xyce-studio-tests
```

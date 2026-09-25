#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "../core/util.h"
#include "../dsp/fft.h"
#include "../io/touchstone_file.h"
#include "../io/xyce_csd_file.h"
#include "../io/xyce_csv_file.h"
#include "../io/xyce_fft_file.h"
#include "../io/xyce_prn_file.h"
#include "../io/xyce_raw_file.h"
#include "../io/xyce_tecplot_file.h"
#include "../kicad/kicad_session.h"
#include "../netlist/editor_netlist_source.h"
#include "../netlist/netlist.h"
#include "../simulation/print_parameters.h"
#include "../simulation/simulation_config.h"
#include "main_window_presenter.h"
#include "main_window_state.h"
#include "simulation_runner.h"

namespace
{
    std::string window_to_string(fft::WindowFunction wf) {
        switch (wf) {
        case fft::WindowFunction::RECTANGULAR:
            return "RECTANGULAR";
        case fft::WindowFunction::HAMMING:
            return "HAMMING";
        case fft::WindowFunction::HANNING:
            return "HANN";
        case fft::WindowFunction::BLACKMAN:
            return "BLACKMAN";
        default:
            return "UNKNOWN";
        }
    }

    // map a PlotType to a human-readable tab label; falls back to the raw
    // file's title for UNKNOWN plot types (e.g. parsed headers we don't
    // classify). Touchstone files (detected via the data_format metadata
    // key) show the analysis title instead of the AC Analysis label.
    std::string plot_type_to_label(const XyceOutputFile& file) {
        // touchstone files carry a data_format metadata entry
        if (file.metadata().count("data_format") > 0)
            return file.title();
        switch (file.plot_type()) {
        case PlotType::TRANSIENT:
            return "Transient";
        case PlotType::AC:
            return "AC Analysis";
        case PlotType::DC:
            return "DC Sweep";
        case PlotType::DC_OPERATING_POINT:
            return "DC Operating Point";
        case PlotType::NOISE:
            return "Noise Analysis";
        case PlotType::PCE:
            return "PCE Analysis";
        case PlotType::FFT:
        case PlotType::UNKNOWN:
        default:
            return file.title();
        }
    }

    // suggested smith chart plots: one smith chart with the diagonal matrix
    // entries (s11, s22, ...) which plot as reflection coefficients on the
    // gamma plane; the equivalent entries of y/z parameter files map through
    // the smith conversions
    std::vector<std::vector<std::string>> smith_suggested_plots(const XyceOutputFile& file) {
        // parameter type prefix (s/y/z) from the file metadata
        std::string parameter_type = "S";
        if (const auto entry = file.metadata().find("parameter_type"); entry != file.metadata().end())
            parameter_type = entry->second;
        // port count from the file metadata
        int num_ports = 2;
        if (const auto entry = file.metadata().find("num_ports"); entry != file.metadata().end())
            num_ports = std::stoi(entry->second);
        // diagonal entry names, s11, s22, ...
        std::vector<std::string> names;
        for (int p = 0; p < num_ports; ++p)
            names.push_back(parameter_type + std::to_string(p + 1) + std::to_string(p + 1));
        // one smith chart carrying the diagonal entries
        return {std::move(names)};
    }
} // namespace

SlintMainWindowPresenter::SlintMainWindowPresenter(MainWindowViewDef& view, std::unique_ptr<NetlistSource> netlist_source, PluginConfig plugin_config, std::shared_ptr<KiCadSession> kicad_session) :
    m_view(view), m_kicad_session(std::move(kicad_session)), m_netlist_source(netlist_source != nullptr ? std::move(netlist_source) : std::make_unique<EditorNetlistSource>([this]() { return m_view.netlist_editor_content(); }, std::filesystem::path{})), m_simulation_config(SimulationConfig::from_xyce_directives({})), m_plugin_config(std::move(plugin_config)) {
    // initialize the toolbar action states before the window is shown
    refresh_action_states();
}

SlintMainWindowPresenter::~SlintMainWindowPresenter() = default;

void SlintMainWindowPresenter::on_open_xyce_file(const std::filesystem::path& path) {
    // analyze the file extension
    const auto extension = path.extension().string();
    // netlist file extension
    if (extension == ".cir") {
        // create an editor netlist source backed by the selected file, reading
        // live editor text
        m_netlist_source = std::make_unique<EditorNetlistSource>([this]() { return m_view.netlist_editor_content(); }, path);
        // update the window title from the source
        set_base_title(m_netlist_source->title());
        // the netlist editor is now editable
        m_view.set_netlist_editor_read_only(false);
        // load the netlist content
        const auto [reloaded, content] = m_netlist_source->load_netlist();
        // set the editor content to the loaded netlist
        update_netlist_editor_content(content, false);
        // clear the parse cache so the next run re-parses the new content
        m_pending_sanitized_netlist.clear();
        m_pending_topology = NetlistTopology{};
        m_pending_original_netlist.clear();
        // clear the cached simulation config, forcing a fresh parse on next run
        m_simulation_config = SimulationConfig::from_xyce_directives({});
        // remove the raw output file reference
        m_analysis_measurements = std::nullopt;
        // remove the parsed FFT calculation files, they belong to a previous run
        m_fft_measurements.clear();
        // remove the parsed touchstone file, it belongs to a previous file
        m_s_parameter_measurements = std::nullopt;
        // drop every dataset chart state, they belong to a previous file
        m_view.release_all_charts();
        // clear plot datasets
        m_plot_datasets.clear();
        // reset active dataset index
        m_active_dataset_index = 0;
        // synchronize plot tabs with view
        sync_plot_tabs_with_view();
        // show the netlist view over the charts view
        m_view.show_netlist_view();
        // refresh toolbar/menu states
        refresh_action_states();
        return;
    }
    // raw file extension
    if (extension == ".raw") {
        // parse the raw file
        auto raw_file = xyce_raw_file_parser(path);
        // check raw file was parsed
        if (raw_file.has_value()) {
            // load the parsed raw file
            load_analysis_measurements(std::move(raw_file.value()));
        }
        return;
    }
    // csd file extension, covering the .TD.csd AC_IC variant as well
    if (extension == ".csd") {
        // parse the csd file
        auto csd_file = xyce_csd_file_parser(path);
        // check csd file was parsed
        if (csd_file.has_value()) {
            // load the parsed csd file
            load_analysis_measurements(std::move(csd_file.value()));
        }
        return;
    }
    // prn file extension
    if (extension == ".prn") {
        // parse the prn file
        auto prn_file = xyce_prn_file_parser(path);
        // check prn file was parsed
        if (prn_file.has_value()) {
            // load the parsed prn file
            load_analysis_measurements(std::move(prn_file.value()));
        }
        return;
    }
    // csv file extension, covering all the analysis-specific .csv variants
    // produced by .PRINT FORMAT=CSV
    if (extension == ".csv") {
        // parse the csv file
        auto csv_file = xyce_csv_file_parser(path);
        // check csv file was parsed
        if (csv_file.has_value()) {
            // load the parsed csv file
            load_analysis_measurements(std::move(csv_file.value()));
        }
        return;
    }
    // dat file extension, covering all the analysis-specific .dat variants
    // produced by .PRINT FORMAT=TECPLOT, including the intrusive PCE
    // companion files
    if (extension == ".dat") {
        // parse the tecplot file
        auto tecplot_file = xyce_tecplot_file_parser(path);
        // check tecplot file was parsed
        if (tecplot_file.has_value()) {
            // load the parsed tecplot file
            load_analysis_measurements(std::move(tecplot_file.value()));
        }
        return;
    }
}

void SlintMainWindowPresenter::on_save_netlist() {
    // an untitled netlist has no file behind it: ask the user for a location and filename before anything can be written
    if (!m_netlist_source->has_backing_file()) {
        // run the save-as flow through the view (native dialog)
        const auto path = m_view.request_netlist_save_path();
        // user canceled: the netlist stays dirty for the next save attempt
        if (!path.has_value())
            return;
        // default to the netlist format when the chosen name carries no extension
        std::filesystem::path target = path.value();
        if (target.extension().empty())
            target += ".cir";
        // rebind the source to the chosen file so later saves write in place
        m_netlist_source = std::make_unique<EditorNetlistSource>([this]() { return m_view.netlist_editor_content(); }, target);
        // the chosen file becomes the window title (clears the dirty marker)
        set_base_title(m_netlist_source->title());
        // consume the file seed now: the just-created source would otherwise read the file on its first load and miss later editor edits
        static_cast<void>(m_netlist_source->load_netlist());
    }
    // save content in the netlist source
    m_netlist_source->save_netlist();
    // reset the dirty flag and refresh states (set_base_title may have cleared the flag already, so the refresh is unconditional)
    set_netlist_editor_dirty(false);
    refresh_action_states();
}

void SlintMainWindowPresenter::on_show_netlist() {
    // switch to the netlist view
    m_view.show_netlist_view();
    // refresh state
    refresh_action_states();
}

void SlintMainWindowPresenter::on_show_charts() {
    // switch to the charts view
    m_view.show_charts_view();
    // refresh state
    refresh_action_states();
}

void SlintMainWindowPresenter::on_show_simulation_output() {
    // show the simulation output panel
    m_view.show_simulation_output_panel();
    // refresh state
    refresh_action_states();
}

void SlintMainWindowPresenter::on_close_simulation_output() {
    // hide the simulation output panel
    m_view.hide_simulation_output_panel();
    // refresh toolbar/menu states
    refresh_action_states();
}

void SlintMainWindowPresenter::on_run_simulation() {
    // validate the plugin configuration before launching
    if (!m_plugin_config.is_xyce_executable_valid()) {
        // update the statusbar with an error
        m_view.set_status_text("Configured Xyce executable path is invalid");
        // exit
        return;
    }
    // load the netlist source content
    const auto [reloaded, content] = m_netlist_source->load_netlist();
    // re-parse when the schematic changed or when no cached state exists yet (e.g. the first run exited early on an empty netlist)
    if (reloaded || m_pending_sanitized_netlist.empty()) {
        // parse the netlist and extract the topology
        const auto [sanitized_netlist, topology] = parse_netlist(content);
        // guard against an empty netlist (parse_netlist always produces at least "\n" even for empty input, so check for that sentinel too)
        if (sanitized_netlist.empty() || sanitized_netlist == "\n") {
            // update the statusbar
            m_view.set_status_text("No netlist content to simulate");
            // update the editor with the final netlist
            if (update_netlist_editor_content("", false))
                refresh_action_states();
            // exit
            return;
        }
        // initialize the simulation config from the parsed directives only when the netlist
        // content actually changed since the last parse; a schematic re-export carrying the
        // same content (e.g. KiCad autosave) must not discard the user's accepted dialog
        // configuration, the schematic does not hold the edited directives back
        if (content != m_pending_original_netlist) {
            const auto simulation_config = SimulationConfig::from_xyce_directives(topology.m_directives);
            if (!std::holds_alternative<std::monostate>(simulation_config.analysis))
                m_simulation_config = simulation_config;
        }
        // remember the parse result for the launch (always updated when the
        m_pending_sanitized_netlist = sanitized_netlist;
        m_pending_topology = topology;
        m_pending_original_netlist = content;
    }
    // prompt the user when no analysis is configured yet
    if (std::holds_alternative<std::monostate>(m_simulation_config.analysis)) {
        // mark the pending dialog as a simulation run
        m_run_pending = true;
        // ask the view to show the configure dialog; the accepted configuration
        // is delivered through on_simulation_parameters_dialog_result
        static_cast<void>(m_view.show_simulation_parameters_dialog(m_simulation_config));
        // exit
        return;
    }
    // launch the simulation with the configured analysis
    launch_simulation();
}

void SlintMainWindowPresenter::on_cancel_simulation() {
    // request the view to cancel the running simulation process
    m_view.cancel_simulation_process();
}

void SlintMainWindowPresenter::launch_simulation() {
    // build the directives from the config with topology expansion
    const auto directives = m_simulation_config.to_xyce_directives(m_pending_topology);
    // merge the directives into the sanitized netlist before .END for the editor
    const auto final_netlist = build_final_netlist(m_pending_sanitized_netlist, directives, m_pending_topology.m_passthrough_directives);
    // build the netlist handed to Xyce with the FILE= option stripped from the
    // analysis RAW .PRINT statement only; Xyce then writes the RAW file next to
    // the temporary netlist under its own default name, so the file the
    // application maps is never rewritten by a later run (on Windows the
    // rewrite fails while the file is mapped, on macOS it invalidates the
    // previous mapping). Unassociated and legacy print directives are never
    // mapped by the application, so they keep their output files.
    const auto analysis_print = m_simulation_config.analysis_print_statement();
    // the analysis print statement matches by prefix: analyses may append
    // analysis-specific output variables to the serialized print statement
    // (e.g. the NOISE DNI()/DNO() operators per the Xyce reference guide)
    const auto is_analysis_print = [&analysis_print](const std::string& directive) -> bool {
        if (!analysis_print.has_value() || directive.size() < analysis_print->size())
            return false;
        return directive.compare(0, analysis_print->size(), *analysis_print) == 0 && (directive.size() == analysis_print->size() || directive[analysis_print->size()] == ' ');
    };
    std::vector<std::string> simulation_directives;
    simulation_directives.reserve(directives.size());
    for (const auto& directive : directives)
        simulation_directives.push_back(is_analysis_print(directive) ? strip_print_file_option(directive) : directive);
    const auto simulation_netlist = build_final_netlist(m_pending_sanitized_netlist, simulation_directives, m_pending_topology.m_passthrough_directives);
    // update the editor with the final netlist
    if (update_netlist_editor_content(final_netlist, m_pending_original_netlist != final_netlist))
        refresh_action_states();
    // working directory for the netlist source
    const auto working_directory = m_netlist_source->working_directory();
    // create a temporary netlist file for the runner
    const auto temp_path = SimulationRunner::create_temp_netlist(simulation_netlist);
    // check the temporary netlist was created
    if (temp_path.empty()) {
        // update the statusbar with an error
        m_view.set_status_text("Failed to create temporary netlist file");
        // exit
        return;
    }
    // clear the parsed FFT calculation files, they belong to the previous run
    m_fft_measurements.clear();
    // clear the parsed PCE output file, it belongs to the previous run
    m_pce_measurements = std::nullopt;
    // clear the parsed touchstone file, it belongs to the previous run
    m_s_parameter_measurements = std::nullopt;
    // remember the run paths for the finished handler; the view owns the runner
    m_simulation_working_directory = working_directory;
    m_simulation_netlist_path = temp_path;
    // mark the simulation as running
    m_simulation_running = true;
    // reset the log for this run
    m_view.clear_simulation_output();
    // launch the simulation through the view, which wires the runner
    m_view.start_simulation_process(m_plugin_config.xyce_executable_path(), temp_path, working_directory);
    // refresh toolbar/menu states
    refresh_action_states();
    // update the statusbar
    m_view.set_status_text("Simulation started...");
}

void SlintMainWindowPresenter::on_configure_simulation() {
    // load the netlist content
    const auto [reloaded, content] = m_netlist_source->load_netlist();
    // check content was reloaded
    if (reloaded)
        update_netlist_editor_content(content, false);
    // parse the netlist and extract the topology
    const auto [sanitized_netlist, topology] = parse_netlist(content);
    // build the simulation config from the parsed directives; only overwrite
    // the user's saved config when the netlist content actually changed since
    // the last parse — a schematic re-export carrying the same content (e.g.
    // KiCad autosave) must not revert the dialog to the schematic directives
    // and discard the user's accepted configuration
    const auto parsed_config = SimulationConfig::from_xyce_directives(topology.m_directives);
    if (content != m_pending_original_netlist && !std::holds_alternative<std::monostate>(parsed_config.analysis))
        m_simulation_config = parsed_config;
    // remember the parse result so the accepted config can rebuild the netlist
    m_pending_sanitized_netlist = sanitized_netlist;
    m_pending_topology = topology;
    m_pending_original_netlist = content;
    // this dialog is a configure operation, not a pending simulation run
    m_run_pending = false;
    // ask the view to show the dialog with the current config; the accepted
    // configuration is delivered through on_simulation_parameters_dialog_result
    static_cast<void>(m_view.show_simulation_parameters_dialog(m_simulation_config));
}

void SlintMainWindowPresenter::on_configure_plugin() {
    // the view owns the config dialog; it seeds it with the current config and
    // reports the accepted result back through on_plugin_config_dialog_result
    static_cast<void>(m_view.show_plugin_config_dialog(m_plugin_config));
}

void SlintMainWindowPresenter::on_plugin_config_dialog_result(const PluginConfig& config) {
    // log the update before applying
    spdlog::info("Plugin configuration updated: Xyce path = {}", m_plugin_config.xyce_executable_path());
    // store the updated plugin configuration
    m_plugin_config = config;
}

void SlintMainWindowPresenter::on_simulation_parameters_dialog_result(const SimulationConfig& config) {
    // store the updated simulation configuration
    m_simulation_config = config;
    // resume a pending simulation run when the dialog was opened from run
    if (m_run_pending) {
        // launch the simulation with the configured analysis
        launch_simulation();
        // exit
        return;
    }
    // build the directives from the config with topology expansion
    const auto directives = m_simulation_config.to_xyce_directives(m_pending_topology);
    // merge the directives into the sanitized netlist before .END
    const auto final_netlist = build_final_netlist(m_pending_sanitized_netlist, directives, m_pending_topology.m_passthrough_directives);
    // update the editor with the final netlist
    if (update_netlist_editor_content(final_netlist, m_pending_original_netlist != final_netlist))
        refresh_action_states();
}

void SlintMainWindowPresenter::on_fft_dialog_result(std::vector<AnyExpression*> selected_expressions, const fft::FftParameters& fft_params) {
    // the transform needs the charts data (expression manager and step
    // information) from the active dataset
    auto* file = active_dataset_file();
    if (file == nullptr)
        return;
    ExpressionManager& expression_manager = file->expression_manager();
    const StepInformation& step_information = file->step_information();
    // validate the expression selection
    if (selected_expressions.empty()) {
        m_view.set_status_text("No expressions selected for FFT");
        return;
    }
    // from/to abscissa values chosen in the dialog
    const double from_abscissa_value = fft_params.start;
    const double to_abscissa_value = fft_params.stop;
    // list of frequency bins for each step, to be concatenated across steps later
    std::vector<std::vector<double>> frequency_chunks;
    // fft data chunks for each expression, to be concatenated across steps later
    std::vector<std::vector<std::vector<double>>> fft_chunks(selected_expressions.size());
    // processed step indices and abscissa slices
    std::vector<size_t> fft_steps;
    std::vector<std::pair<size_t, size_t>> fft_abscissa_indices;
    std::vector<std::pair<double, double>> fft_abscissa_value_ranges;
    // fft step index offset
    size_t fft_offset = 0;
    // loop steps
    for (size_t step = 0; step < step_information.length(); ++step) {
        // abscissa values for this step — zero copy per-step view
        std::span<const double> step_abscissa = expression_manager.abscissa().step_data(step);
        // find the indices corresponding to the selected abscissa range
        auto it_left = std::lower_bound(step_abscissa.begin(), step_abscissa.end(), from_abscissa_value);
        auto it_right = std::upper_bound(step_abscissa.begin(), step_abscissa.end(), to_abscissa_value);
        // from and to indices for the selected abscissa range (inclusive of from, exclusive of to)
        const size_t from_index = static_cast<size_t>(std::distance(step_abscissa.begin(), it_left));
        const size_t to_index = static_cast<size_t>(std::distance(step_abscissa.begin(), it_right));
        // require at least 2 samples
        if (to_index - from_index < 2) {
            spdlog::warn("Skipping FFT for step {}: selected range has fewer than 2 samples", step);
            continue;
        }
        // expressions in this step
        std::vector<std::span<const double>> y_matrix;
        y_matrix.reserve(selected_expressions.size());
        for (AnyExpression* expression : selected_expressions) {
            // only real-valued expressions are eligible for the transform
            if (std::holds_alternative<Expression<double>>(*expression)) {
                auto& double_expr = std::get<Expression<double>>(*expression);
                const auto y_data = double_expr.step_data(step);
                y_matrix.push_back(y_data.subspan(from_index, to_index - from_index));
            }
        }
        // skip empty matrices (should not happen since the dialog filters real expressions)
        if (y_matrix.empty())
            continue;
        // extract the x interval for the FFT
        const auto x_interval = step_abscissa.subspan(from_index, to_index - from_index);
        try {
            // compute the FFT; all expressions in y_matrix are processed together for this step
            auto result = fft::compute_fft_many(x_interval, y_matrix, fft_params.np, fft_params.window, fft_params.format, 0, x_interval.size() - 1, fft_params.output, fft_params.keep_dc);
            // check the frequency axis is not empty
            if (result.frequencies.empty()) {
                spdlog::error("FFT computation returned an empty frequency axis for step {}", step);
                m_view.set_status_text("FFT computation failed");
                return;
            }
            // remember the first and last frequency bins before moving the chunks
            const double first_frequency = result.frequencies.front();
            const double last_frequency = result.frequencies.back();
            const size_t chunk_size = result.frequencies.size();
            // store the step output slice
            fft_abscissa_indices.emplace_back(fft_offset, fft_offset + chunk_size);
            // update the offset
            fft_offset += chunk_size;
            // store the frequency bins and per-expression values
            frequency_chunks.push_back(std::move(result.frequencies));
            for (size_t i = 0; i < result.values.size(); ++i)
                fft_chunks[i].push_back(std::move(result.values[i]));
            // append the step and its abscissa value range
            fft_steps.push_back(step);
            fft_abscissa_value_ranges.emplace_back(first_frequency, last_frequency);
        }
        catch (const std::exception& e) {
            // log information
            spdlog::error("FFT computation failed for step {}: {}", step, e.what());
            // update the statusbar
            m_view.set_status_text("FFT computation failed");
            // exit
            return;
        }
    }
    // require at least one processed step
    if (fft_steps.empty()) {
        // log information
        spdlog::warn("FFT computation skipped: no step has at least 2 samples in the selected range");
        // update the statusbar
        m_view.set_status_text("FFT computation skipped: no data in the selected range");
        // exit
        return;
    }
    // build title from the FFT parameters (window and output frequency range)
    const double fft_min_freq = fft_abscissa_value_ranges.front().first;
    const double fft_max_freq = fft_abscissa_value_ranges.front().second;
    std::string fft_title = "FFT: " + window_to_string(fft_params.window) + ", " + std::to_string(static_cast<long long>(fft_min_freq + 0.5)) + "–" + std::to_string(static_cast<long long>(fft_max_freq + 0.5)) + " Hz";
    // build FFT expressions using the Expression<double> constructor with step slices
    std::vector<AnyExpression> fft_expressions;
    {
        // create flat frequency data
        std::vector<double> freq_data;
        freq_data.reserve(fft_offset);
        // concatenate the frequency chunks across steps
        for (const auto& chunk : frequency_chunks)
            freq_data.insert(freq_data.end(), chunk.begin(), chunk.end());
        // append the expression for the frequency abscissa with unit "Hz"
        fft_expressions.emplace_back(Expression<double>("Frequency", std::move(freq_data), fft_abscissa_indices, "Hz"));
    }
    // suggested plots
    std::vector<std::vector<std::string>> suggested_plots;
    for (size_t i = 0; i < selected_expressions.size(); ++i) {
        // current expression
        auto& real_expression = std::get<Expression<double>>(*selected_expressions[i]);
        // determine the unit
        std::string unit;
        if (fft_params.output == fft::FftOutput::PHASE)
            unit = "°";
        else if (fft_params.output == fft::FftOutput::MAGNITUDE_DB)
            unit = "dB";
        else
            unit = real_expression.unit();
        // expression name for the FFT result
        const auto expr_name = "FFT(" + real_expression.name() + ")";
        // create flat data
        std::vector<double> data;
        data.reserve(fft_offset);
        // concatenate the FFT chunks across steps
        for (const auto& chunk : fft_chunks[i])
            data.insert(data.end(), chunk.begin(), chunk.end());
        // suggest a maximum of three expressions for plotting
        if (suggested_plots.size() < 3)
            suggested_plots.push_back({expr_name});
        // append the expression for the FFT result with the step slices
        fft_expressions.emplace_back(Expression<double>(expr_name, std::move(data), fft_abscissa_indices, unit));
    }
    // step information for the FFT output (only include the processed steps)
    StepInformation fft_step_information(step_information.keys(), step_information.values(), fft_abscissa_value_ranges);
    // build the expression manager (moves from the expression list)
    ExpressionManager fft_expression_manager(fft_expressions, fft_abscissa_indices);
    // allocate a new dataset id
    const int dataset_id = m_next_dataset_id++;
    // create a raw file with the FFT results
    auto fft_raw = std::make_shared<XyceOutputFile>("", fft_title, false, std::move(fft_step_information), PlotType::FFT, AbscissaScale::LINEAR, std::move(fft_expression_manager), nullptr, suggested_plots);
    // append the FFT dataset as a closable tab
    m_plot_datasets.push_back(PlotDataset{
        .id = dataset_id,
        .file = std::move(fft_raw),
        .closable = true,
    });
    // new tab index
    const size_t new_tab_index = m_plot_datasets.size() - 1;
    // synchronize tabs with the view
    sync_plot_tabs_with_view();
    // activate the newly created FFT dataset
    activate_plot_dataset(new_tab_index);
    // switch to the charts view
    m_view.show_charts_view();
    // refresh toolbar/menu states
    refresh_action_states();
}

void SlintMainWindowPresenter::on_select_plot_tab(int index) {
    // bounds check the tab index
    if (index < 0 || static_cast<size_t>(index) >= m_plot_datasets.size())
        return;
    // ignore when already active
    if (static_cast<size_t>(index) == m_active_dataset_index)
        return;
    // activate the selected dataset and switch charts
    activate_plot_dataset(static_cast<size_t>(index));
    // refresh toolbar/menu states
    refresh_action_states();
}

void SlintMainWindowPresenter::on_close_plot_tab(int index) {
    // bounds check the tab index
    if (index < 0 || static_cast<size_t>(index) >= m_plot_datasets.size())
        return;
    // only closable datasets can be closed
    if (!m_plot_datasets[static_cast<size_t>(index)].closable)
        return;
    // drop the chart state of the closed tab
    m_view.release_charts(m_plot_datasets[static_cast<size_t>(index)].id);
    // erase the dataset at the given index
    m_plot_datasets.erase(m_plot_datasets.begin() + index);
    // handle case where no datasets remain
    if (m_plot_datasets.empty()) {
        // reset active dataset index
        m_active_dataset_index = 0;
        // drop any remaining chart state
        m_view.release_all_charts();
        // synchronize plot tabs with view
        sync_plot_tabs_with_view();
        // refresh toolbar/menu states
        refresh_action_states();
        // exit
        return;
    }
    // handle case where active tab was closed or index is past end
    if (m_active_dataset_index == static_cast<size_t>(index)) {
        // clamp active index if it was the last element
        if (m_active_dataset_index >= m_plot_datasets.size())
            m_active_dataset_index = m_plot_datasets.size() - 1;
        // synchronize tabs before activating
        sync_plot_tabs_with_view();
        // activate the new current dataset
        activate_plot_dataset(m_active_dataset_index);
    }
    else if (m_active_dataset_index > static_cast<size_t>(index)) {
        // decrement active index because a preceding item was removed
        --m_active_dataset_index;
        // synchronize plot tabs with view
        sync_plot_tabs_with_view();
        // update active plot tab in view
        m_view.set_active_plot_tab(static_cast<int>(m_active_dataset_index));
    }
    else {
        // synchronize plot tabs with view
        sync_plot_tabs_with_view();
        // update active plot tab in view
        m_view.set_active_plot_tab(static_cast<int>(m_active_dataset_index));
    }
    // refresh toolbar/menu states
    refresh_action_states();
}

void SlintMainWindowPresenter::on_chart_calculate_fft(size_t chart_index) {
    // the FFT dialog needs the charts data (expression manager and step
    // information) from the active dataset
    if (active_dataset_file() == nullptr)
        return;
    // ask the view to show the FFT setup dialog for the chart; the accepted
    // expressions and parameters are delivered through on_fft_dialog_result
    m_view.show_fft_dialog(chart_index);
}

void SlintMainWindowPresenter::on_chart_step_tool(size_t chart_index) {
    // the step tool needs the charts data (step information) from the active
    // dataset
    if (active_dataset_file() == nullptr)
        return;
    // ask the view to show the step tool dialog for the chart; the accepted
    // step selection is applied back to the chart through the renderer
    m_view.show_step_tool_dialog(chart_index);
}

void SlintMainWindowPresenter::on_chart_new_window(size_t) {
    // spawn a new window seeded with the active dataset file
    if (m_active_dataset_index < m_plot_datasets.size() && m_plot_datasets[m_active_dataset_index].file)
        m_view.spawn_raw_file_window(m_plot_datasets[m_active_dataset_index].file);
}

void SlintMainWindowPresenter::on_chart_moved(const size_t from, const size_t to) {
    // nothing to reorder without a loaded dataset
    if (active_dataset_file() == nullptr)
        return;
    // mediate the reorder so a future change can persist the chart order
    m_view.move_chart(from, to);
}

void SlintMainWindowPresenter::load_analysis_measurements(std::shared_ptr<XyceOutputFile> raw_file) {
    // guard against a missing file
    if (raw_file == nullptr)
        return;
    // drop existing datasets and chart state before loading a new raw file
    m_fft_measurements.clear();
    m_pce_measurements = std::nullopt;
    m_s_parameter_measurements = std::nullopt;
    m_analysis_measurements = std::nullopt;
    m_view.release_all_charts();
    m_plot_datasets.clear();
    m_active_dataset_index = 0;
    // append the parsed raw file as a primary (non-closable) plot dataset
    m_plot_datasets.push_back(PlotDataset{.id = m_next_dataset_id++, .file = std::move(raw_file), .closable = false});
    // the loaded file is the analysis measurements of this window
    m_analysis_measurements = m_plot_datasets.back().file;
    // synchronize tabs with the view, activate the new dataset (the renderer
    // creates a fresh chart state for it) and switch to the charts view
    sync_plot_tabs_with_view();
    activate_plot_dataset(m_plot_datasets.size() - 1);
    show_simulation_output_view();
}

void SlintMainWindowPresenter::on_simulation_finished(int exit_code, bool was_canceled) {
    // mark the simulation as no longer running
    m_simulation_running = false;
    // handle canceled simulations
    if (was_canceled) {
        // show the output panel so the user can inspect the log
        m_view.show_simulation_output_panel();
        // update the statusbar
        m_view.set_status_text("Simulation canceled");
        // refresh toolbar/menu states
        refresh_action_states();
        // exit
        return;
    }
    // check for success
    if (exit_code == 0) {
        // 1. resolve and parse the analysis output; the returned instance is
        // ready for rendering: the tab plot type follows the
        // configured analysis print, not the produced file
        auto analysis_measurements = resolve_analysis_output(m_simulation_netlist_path, m_simulation_working_directory);
        // 2. the analysis type owns the knowledge of what companion data the
        // run dumps besides the analysis output: a .LIN analysis with a
        // touchstone format dumps one s-parameter file, a .TRAN analysis with
        // .FFT directives dumps the FFT calculation files and a .TRAN or .DC
        // analysis with .PCE parameters and a .PCE companion print dumps the
        // PCE statistics output
        const auto produced = m_simulation_config.produced_measurements();
        // step slices source for the companion loaders, taken from the
        // analysis output before it is moved into the dataset
        const StepInformation* analysis_steps = analysis_measurements.has_value() ? &(*analysis_measurements)->step_information() : nullptr;
        // 3. start a fresh result set
        if (analysis_measurements.has_value()) {
            // the run produced an analysis output: the primary dataset
            // identity is kept so the renderer re-points its charts and zoom
            // windows, plots and step selections survive the re-run of the
            // same netlist; only the companion chart states are released
            for (size_t i = 1; i < m_plot_datasets.size(); ++i)
                m_view.release_charts(m_plot_datasets[i].id);
            // allocate space for at most one primary dataset
            m_plot_datasets.resize(std::min<size_t>(m_plot_datasets.size(), 1));
        }
        else {
            // the run produced no analysis output: drop the whole previous
            // result set, the stale tabs do not correspond with the latest
            // simulation
            for (const auto& dataset : m_plot_datasets)
                m_view.release_charts(dataset.id);
            // clear the primary dataset as well
            m_plot_datasets.clear();
        }
        // clear any previously loaded companion measurements
        m_fft_measurements.clear();
        m_pce_measurements = std::nullopt;
        m_s_parameter_measurements = std::nullopt;
        m_analysis_measurements = std::nullopt;
        // 4. render the main simulation tab when the run produced analysis output
        if (analysis_measurements.has_value()) {
            // check if a previous simulation produced a primary dataset
            if (m_plot_datasets.empty()) {
                // the primary dataset is empty, so push the new analysis output as the first dataset
                m_plot_datasets.push_back(PlotDataset{.id = m_next_dataset_id++, .file = std::move(*analysis_measurements), .closable = false});
            }
            else {
                // a re-run replaces the primary file in place
                m_plot_datasets[0].file = std::move(*analysis_measurements);
            }
            // the installed file is the analysis measurements of this run
            m_analysis_measurements = m_plot_datasets[0].file;
            // activate the primary dataset
            m_active_dataset_index = 0;
            sync_plot_tabs_with_view();
            activate_plot_dataset(0);
        }
        // 5. companion measurements: load the produced data and render its tabs
        if (produced.s_parameters)
            load_s_parameter_measurements(analysis_steps);
        if (produced.fft)
            load_fft_measurements();
        if (produced.pce)
            load_pce_measurements(*m_simulation_config.pce_print_parameters());
        // 6. the run produced no data at all
        if (!analysis_measurements.has_value() && !m_s_parameter_measurements.has_value() && m_fft_measurements.empty() && !m_pce_measurements.has_value()) {
            // update the statusbar and show the output panel so the user can inspect the log
            m_view.set_status_text("Simulation finished but output file could not be found");
            m_view.show_simulation_output_panel();
            refresh_action_states();
            // exit
            return;
        }
        // 7. when the analysis output is missing the first companion tab becomes active
        if (!analysis_measurements.has_value()) {
            m_active_dataset_index = 0;
            activate_plot_dataset(0);
        }
        // 8. copy the produced analysis output file to the user-indicated location; RAW and PROBE formats are mutually exclusive so exactly one of the two paths resolves
        if (const auto raw_path = m_simulation_config.raw_output_file_path(m_simulation_netlist_path); raw_path.has_value() && std::filesystem::exists(*raw_path))
            copy_simulation_output_to_destination(*raw_path);
        else if (const auto csd_path = m_simulation_config.csd_output_file_path(m_simulation_netlist_path); csd_path.has_value() && std::filesystem::exists(*csd_path))
            copy_simulation_output_to_destination(*csd_path);
        // switch to the charts view
        m_view.show_charts_view();
        // hide the output panel — it is only shown on failure
        m_view.hide_simulation_output_panel();
        // update the statusbar
        m_view.set_status_text("Simulation finished successfully");
        // refresh toolbar/menu states
        refresh_action_states();
        // exit
        return;
    }
    else {
        // simulation failed with a non-zero exit code
        m_view.set_status_text("Simulation failed (exit code " + std::to_string(exit_code) + ")");
    }
    // show the output panel so the user can inspect the log on failure
    m_view.show_simulation_output_panel();
    // refresh toolbar/menu states
    refresh_action_states();
}

void SlintMainWindowPresenter::copy_simulation_output_to_destination(const std::filesystem::path& produced_path) {
    // resolve the user-facing copy destination from the analysis print; RAW and PROBE formats are mutually exclusive so one of the two destinations resolves
    auto destination = m_simulation_config.raw_output_copy_destination(m_simulation_working_directory);
    // PROBE runs copy to the csd destination
    if (!destination.has_value())
        destination = m_simulation_config.csd_output_copy_destination(m_simulation_working_directory);
    // no destination configured, or it is the produced file itself
    if (!destination.has_value() || *destination == produced_path)
        return;
    // the produced file must exist
    std::error_code ec;
    if (!std::filesystem::exists(produced_path))
        return;
    // create the destination parent directory when missing
    std::filesystem::create_directories(destination->parent_path(), ec);
    // copy the produced file, overwriting the previous run's copy
    std::filesystem::copy_file(produced_path, *destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        spdlog::warn("Failed to copy output file to '{}': {}", destination->string(), ec.message());
}

std::optional<std::shared_ptr<XyceOutputFile>> SlintMainWindowPresenter::resolve_analysis_output(const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) {
    // get the analysis print parameters; without a .PRINT directive Xyce
    // writes no output file at all, so there is nothing to resolve
    const auto analysis_print = m_simulation_config.analysis_print_parameters();
    if (!analysis_print.has_value())
        return std::nullopt;
    // determine the output file format and build the expected file path: .prn-producing formats are STD (the default when no FORMAT is given), NOINDEX, GNUPLOT and SPLOT; CSV produces the same table with a comma delimiter and a .csv extension; TECPLOT produces the same table in the tecplot format with a .dat extension; RAW and PROBE produce netlist-derived files (.raw and .csd) because their FILE= option is stripped for the run
    bool is_prn_format = true;
    bool is_csv_format = false;
    bool is_tecplot_format = false;
    bool is_probe_format = false;
    std::filesystem::path output_path;
    // check for known .prn-producing formats; empty format defaults to STD
    if (!analysis_print->print_format.empty()) {
        // format
        auto format = to_upper(analysis_print->print_format);
        // all this formats produce .prn files
        is_prn_format = (format == "STD" || format == "NOINDEX" || format == "GNUPLOT" || format == "SPLOT");
        // CSV format produces the same table with a comma delimiter in a .csv file
        is_csv_format = (format == "CSV");
        // TECPLOT format produces the same table in the tecplot format in a .dat file
        is_tecplot_format = (format == "TECPLOT");
        // PROBE format produces the .csd output file
        is_probe_format = (format == "PROBE");
    }
    if (is_prn_format || is_csv_format || is_tecplot_format) {
        // .prn, .csv and .dat formats preserve the FILE= option
        if (!analysis_print->print_file.empty()) {
            const auto resolved = std::filesystem::path(strip_outer_quotes(analysis_print->print_file));
            output_path = resolved.is_absolute() ? resolved : working_directory / resolved;
        }
        else {
            // no FILE= specified: Xyce writes <netlist><suffix> next to the netlist; the suffix depends on the print type and the format
            output_path = std::filesystem::path(netlist_path).string() + (is_csv_format ? csv_output_suffix(analysis_print->print_type) : is_tecplot_format ? tecplot_output_suffix(analysis_print->print_type) : prn_output_suffix(analysis_print->print_type));
        }
    }
    else if (is_probe_format) {
        // PROBE format: FILE= is stripped for the Xyce run so the produced file
        // is always next to the netlist; the path carries the .TD suffix for
        // AC_IC runs, which produce time-domain output in a .TD.csd file
        const auto csd_path = m_simulation_config.csd_output_file_path(netlist_path);
        // without a produced csd file there is nothing to load
        if (!csd_path.has_value() || !std::filesystem::exists(*csd_path))
            return std::nullopt;
        // parse the produced csd file and prepare the instance: the tab plot
        // type follows the configured analysis print type, not the produced
        // file, so the label is identical for every format
        auto measurements = xyce_csd_file_parser(*csd_path);
        if (measurements.has_value())
            apply_analysis_print_metadata(**measurements);
        return measurements;
    }
    else {
        // RAW format: FILE= is stripped for the Xyce run so the produced file is always next to the netlist
        output_path = std::filesystem::path(netlist_path).string() + ".raw";
    }
    // check the file exists
    if (!std::filesystem::exists(output_path))
        return {};
    // parse the file with the appropriate parser and prepare the instance: the
    // tab plot type follows the configured analysis print type, not
    // the produced file, so the label is identical for every format
    auto measurements = is_csv_format ? xyce_csv_file_parser(output_path) : is_tecplot_format ? xyce_tecplot_file_parser(output_path) : (is_prn_format ? xyce_prn_file_parser(output_path) : xyce_raw_file_parser(output_path));
    if (measurements.has_value())
        apply_analysis_print_metadata(**measurements);
    return measurements;
}

void SlintMainWindowPresenter::apply_analysis_print_metadata(XyceOutputFile& file) const {
    // the tab plot type is derived from the configured analysis print type, not
    // from the produced file: prn files carry no analysis-type header and raw
    // Plotname strings must not shadow the configured print, so the tab label
    // is identical regardless of the .PRINT format; the title keeps the
    // circuit name provided by the parsers (raw Title header, prn file stem)
    const auto analysis_print = m_simulation_config.analysis_print_parameters();
    // without analysis print parameters the parser-derived metadata stands
    if (!analysis_print.has_value())
        return;
    // normalize the print type for the comparisons below
    auto print_type = to_upper(analysis_print->print_type);
    // map the print type to the plot type
    PlotType plot_type = PlotType::UNKNOWN;
    // process the print type
    if (print_type == "TRAN")
        plot_type = PlotType::TRANSIENT;
    else if (print_type == "AC" || print_type.find("AC") == 0)
        plot_type = PlotType::AC;
    else if (print_type == "DC")
        plot_type = PlotType::DC;
    else if (print_type == "NOISE")
        plot_type = PlotType::NOISE;
    else if (print_type == "OP")
        plot_type = PlotType::DC_OPERATING_POINT;
    // update file
    file.set_plot_type(plot_type);
}

void SlintMainWindowPresenter::load_s_parameter_measurements(const StepInformation* analysis_steps) {
    // count the P devices in the netlist; per the reference guide the default
    // .LIN output name is <netlist>.sNp, N being the port count
    const auto num_ports = static_cast<int>(std::count_if(m_pending_topology.m_devices.begin(), m_pending_topology.m_devices.end(), [](const Device& device) { return device.m_type_letter == "P"; }));
    // resolve the s-parameter output file path from the .LIN config
    std::optional<std::filesystem::path> s_parameter_path;
    if (const auto resolved = m_simulation_config.s_parameter_output_file_path(m_simulation_netlist_path, m_simulation_working_directory, num_ports)) {
        // Xyce normally writes the FILE= value verbatim, but appends .s2p
        // when the value names the netlist itself, so probe both candidates
        if (std::filesystem::exists(*resolved)) {
            s_parameter_path = resolved;
        }
        else {
            std::filesystem::path suffixed = *resolved;
            suffixed += ".s2p";
            if (std::filesystem::exists(suffixed))
                s_parameter_path = suffixed;
        }
    }
    // no s-parameter output was produced
    if (!s_parameter_path.has_value())
        return;
    // parse the file, carrying the analysis output's step information when
    // present so .STEP runs map into per-step slices
    auto measurements = touchstone_file_parser(s_parameter_path->string(), analysis_steps);
    if (!measurements.has_value()) {
        spdlog::warn("Failed to parse s-parameter output file '{}'", s_parameter_path->string());
        return;
    }
    // store the parsed measurements and render the tabs: the LIN Analysis tab
    // with the suggested rectangular plots, followed by the Smith Chart tab
    // plotting the same data on the gamma plane with the diagonal entries
    m_s_parameter_measurements = *measurements;
    m_plot_datasets.push_back(PlotDataset{
        .id = m_next_dataset_id++,
        .file = *measurements,
        .closable = false,
        .smith = false,
    });
    m_plot_datasets.push_back(PlotDataset{
        .id = m_next_dataset_id++,
        .file = std::move(*measurements),
        .closable = false,
        .smith = true,
    });
    spdlog::info("Loaded s-parameter output file '{}'", m_s_parameter_measurements.value()->filename().string());
    // synchronize plot tabs with view
    sync_plot_tabs_with_view();
}

void SlintMainWindowPresenter::load_fft_measurements() {
    // resolve the FFT output file path pattern from the .TRAN config
    const auto fft_pattern = m_simulation_config.fft_output_file_path_pattern(m_simulation_netlist_path);
    if (!fft_pattern.has_value())
        return;
    // the FFT data maps onto the analysis output's step slices when the run
    // produced one; a run without a .PRINT directive produces no analysis
    // output and the files' own step structure then drives the parsing
    const bool has_analysis_output = !m_plot_datasets.empty() && m_plot_datasets[0].file != nullptr;
    const StepInformation* analysis_steps = has_analysis_output ? &m_plot_datasets[0].file->step_information() : nullptr;
    ExpressionManager* analysis_expressions = has_analysis_output ? &m_plot_datasets[0].file->expression_manager() : nullptr;
    // parse the matching FFT output files; the .TRAN analysis with .FFT
    // directives dumps one file per abscissa group
    if (auto parsed_files = xyce_fft_file_parser(*fft_pattern, analysis_steps, analysis_expressions)) {
        // store the parsed FFT files
        m_fft_measurements = std::move(*parsed_files);
        // render one tab per FFT output file, as non-closable datasets
        for (auto& fft_file : m_fft_measurements)
            m_plot_datasets.push_back(PlotDataset{
                .id = m_next_dataset_id++,
                .file = fft_file,
                .closable = false,
            });
        // synchronize plot tabs with view
        sync_plot_tabs_with_view();
    }
    // log the number of loaded FFT files
    spdlog::info("Loaded {} Xyce FFT calculation file(s)", m_fft_measurements.size());
}

void SlintMainWindowPresenter::load_pce_measurements(const PrintParameters& pce_print) {
    // resolve the produced PCE output file path from the .PCE companion print
    const auto pce_path = m_simulation_config.pce_output_file_path(m_simulation_netlist_path, m_simulation_working_directory);
    // the run produced no PCE output file next to the netlist
    if (!pce_path.has_value() || !std::filesystem::exists(*pce_path))
        return;
    // parse the produced file with the parser matching the configured print format; the default format (empty) writes the prn table
    const auto format = to_upper(pce_print.print_format);
    if (auto measurements = format == "CSV" ? xyce_csv_file_parser(*pce_path) : format == "TECPLOT" ? xyce_tecplot_file_parser(*pce_path) : xyce_prn_file_parser(*pce_path)) {
        // stamp the dedicated PCE plot type: the analysis print metadata would mislabel the tab as the analysis and the parsers classify by filename, which carries no PCE marker
        (*measurements)->set_plot_type(PlotType::PCE);
        // store the parsed PCE output file
        m_pce_measurements = *measurements;
        // render the PCE output as a non-closable tab
        m_plot_datasets.push_back(PlotDataset{.id = m_next_dataset_id++, .file = *measurements, .closable = false});
        // synchronize plot tabs with view
        sync_plot_tabs_with_view();
        // log the loaded PCE file
        spdlog::info("Loaded Xyce PCE output file '{}'", pce_path->string());
    }
    else {
        // the produced file could not be parsed
        spdlog::warn("Failed to parse PCE output file '{}'", pce_path->string());
    }
}

void SlintMainWindowPresenter::on_simulation_stdout(const std::string& line) {
    // forward the stdout line to the view for display
    m_view.append_simulation_output_line(line);
}

void SlintMainWindowPresenter::on_simulation_stderr(const std::string& line) {
    // log the error line
    spdlog::warn("{}", line);
    // append the error line to the simulation output log so the full Xyce log
    // (stdout and stderr) is visible in the output panel
    m_view.append_simulation_output_line(line);
    // update the statusbar with the latest error line
    m_view.set_status_text("Simulation error: " + line);
}

void SlintMainWindowPresenter::on_netlist_editor_modified() {
    // track whether the editor still holds content
    m_netlist_has_content = !m_view.netlist_editor_content().empty();
    // mark the editor dirty and refresh states when the flag changed
    if (set_netlist_editor_dirty(true))
        refresh_action_states();
}

void SlintMainWindowPresenter::on_extract_schematic_netlist() {
    // extract the netlist from the schematic through the session-backed source
    const auto [reloaded, content] = m_netlist_source->load_netlist();
    // ensure the editor content matches the schematic netlist
    update_netlist_editor_content(content, false);
    // keep the netlist editor read-only in KiCad plugin mode
    m_view.set_netlist_editor_read_only(true);
    // show the netlist editor over the charts view
    m_view.show_netlist_view();
    // refresh toolbar/menu states
    refresh_action_states();
}

const std::optional<std::shared_ptr<XyceOutputFile>>& SlintMainWindowPresenter::analysis_measurements() const {
    // return the current analysis measurement reference
    return m_analysis_measurements;
}

const std::vector<std::shared_ptr<XyceOutputFile>>& SlintMainWindowPresenter::fft_measurements() const {
    // return the parsed FFT calculation output files
    return m_fft_measurements;
}

const std::optional<std::shared_ptr<XyceOutputFile>>& SlintMainWindowPresenter::pce_measurements() const {
    // return the parsed PCE output file
    return m_pce_measurements;
}

const std::optional<std::shared_ptr<XyceOutputFile>>& SlintMainWindowPresenter::s_parameter_measurements() const {
    // return the parsed s-parameter output file
    return m_s_parameter_measurements;
}

void SlintMainWindowPresenter::refresh_action_states() {
    // gather the input flags describing the current window state
    ActionStateInput input;
    input.has_netlist = m_netlist_has_content;
    input.has_netlist_file = m_netlist_source != nullptr && !m_netlist_source->is_read_only();
    // the active dataset drives the chart tool enablement
    auto* file = active_dataset_file();
    input.has_raw = file != nullptr;
    input.charts_shown = m_view.charts_shown();
    input.simulation_running = m_simulation_running;
    input.netlist_editor_dirty = m_netlist_editor_dirty;
    input.output_hidden = m_view.simulation_output_panel_hidden();
    input.log_has_content = m_view.simulation_output_has_content();
    // chart context tools are tied to the loaded output
    input.abscissa_is_time = file != nullptr && file->expression_manager().abscissa().unit() == "s";
    input.has_steps = file != nullptr && file->step_information().length() > 1;
    // the smith chart tab drives the panel's cartesian-tool visibility
    input.charts_smith = !m_plot_datasets.empty() && m_plot_datasets[m_active_dataset_index].smith;
    // compute the action enablement for the current state
    ActionStateEnablement enablement = compute_action_enablement(input);
    // file actions are only available in standalone mode; KiCad provides the
    // netlist, so there is no file to load/save when connected
    if (m_kicad_session != nullptr) {
        enablement.open = false;
        enablement.save = false;
    }
    // apply the enablement to the view
    m_view.apply_action_enablement(enablement);
    // drive the Run/Stop toolbar toggle from the simulation state
    m_view.set_simulation_running(m_simulation_running);
}

XyceOutputFile* SlintMainWindowPresenter::active_dataset_file() const {
    // out of bounds or no file behind the active dataset
    if (m_active_dataset_index >= m_plot_datasets.size())
        return nullptr;
    return m_plot_datasets[m_active_dataset_index].file.get();
}

void SlintMainWindowPresenter::sync_plot_tabs_with_view() { // build the tab items list from the active datasets
    std::vector<PlotTabItem> tabs;
    tabs.reserve(m_plot_datasets.size());
    // convert each dataset to a tab item
    for (const auto& dataset : m_plot_datasets) {
        // tab label; smith datasets always show the Smith Chart label
        tabs.push_back(PlotTabItem{
            .id = dataset.id,
            .title = dataset.smith ? "Smith Chart" : (dataset.file ? plot_type_to_label(*dataset.file) : ""),
            .closable = dataset.closable,
        });
    }
    // active index as int or -1 if empty
    const int active_tab = m_plot_datasets.empty() ? -1 : static_cast<int>(m_active_dataset_index);
    // send tabs and active index to the view
    m_view.set_plot_tabs(tabs, active_tab);
}

void SlintMainWindowPresenter::activate_plot_dataset(size_t index) {
    // guard against out of bounds index
    if (index >= m_plot_datasets.size())
        return;
    // store the active dataset index
    m_active_dataset_index = index;
    // file of the activated dataset
    auto* file = m_plot_datasets[index].file.get();
    // check the file is present
    if (file != nullptr) {
        // activate the dataset in the renderer; switching back to a dataset
        // restores its charts with zoom windows, plots and step selections intact;
        // smith datasets suggest the diagonal entries instead of the rectangular plots
        m_view.update_charts(m_plot_datasets[index].id, file->expression_manager(), file->step_information(), file->abscissa_scale(), m_plot_datasets[index].smith ? smith_suggested_plots(*file) : file->suggested_plots(), m_plot_datasets[index].smith);
        // update the active tab in the view
        m_view.set_active_plot_tab(static_cast<int>(index));
        // update the base title from the dataset title
        set_base_title(file->title());
    }
}

void SlintMainWindowPresenter::show_simulation_output_view() {
    // update the window title from the active dataset
    auto* file = active_dataset_file();
    if (file != nullptr)
        set_base_title(file->title());
    // clear the netlist editor content
    update_netlist_editor_content("", false);
    // show the charts view over the netlist editor
    m_view.show_charts_view();
    // hide the simulation output panel for the raw-file view
    m_view.hide_simulation_output_panel();
    // refresh toolbar/menu states
    refresh_action_states();
}

void SlintMainWindowPresenter::set_base_title(const std::string& title) {
    // store the clean base title
    m_base_title = title;
    // re-render the title with the current dirty marker preserved
    set_netlist_editor_dirty(m_netlist_editor_dirty);
}

bool SlintMainWindowPresenter::set_netlist_editor_dirty(bool flag) {
    // previous dirty state
    const bool previous_dirty_state = m_netlist_editor_dirty;
    // store the dirty state
    m_netlist_editor_dirty = flag;
    // prefix the base title while the editor holds unsaved changes
    m_view.set_title(flag ? "* " + m_base_title : m_base_title);
    // return true if the dirty state changed, false if it remained the same
    return previous_dirty_state != m_netlist_editor_dirty;
}

bool SlintMainWindowPresenter::update_netlist_editor_content(const std::string& content, bool dirty_flag) {
    // set the editor content through the view
    m_view.set_netlist_editor_content(content);
    // track whether the editor holds content
    m_netlist_has_content = !content.empty();
    // return whether the dirty flag changed
    return set_netlist_editor_dirty(dirty_flag);
}

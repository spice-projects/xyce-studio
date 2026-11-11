#include "presenters/main_window_presenter.h"

#include <algorithm>
#include <utility>
#include <variant>

#include <spdlog/spdlog.h>

#include "config/plugin_config.h"
#include "core/util.h"
#include "io/temp_output_directory.h"
#include "netlist/editor_netlist_source.h"
#include "simulation/plot_parameters.h"
#include "simulation/print_parameters.h"

namespace
{
    // how many log lines the duplicate check remembers, which covers each repeated copy
    constexpr std::size_t RECENT_LOG_LINES = 64;

    // remove FILE= from every .PRINT directive, continuations included, so Xyce writes its output in the run directory
    std::string strip_print_file_options(const std::string_view content) {
        // the transformed netlist
        std::string result;
        // reserve the original size, which the strip only shrinks
        result.reserve(content.size());
        // the current physical line start
        std::size_t start = 0;
        // the first line is the netlist title, which is never a directive
        bool first_line = true;
        // a .DATA block carries data lines, not directives
        bool in_data = false;
        // the netlist body ends at .END
        bool ended = false;
        // walk the netlist line by line
        while (start < content.size()) {
            // the current line end, or the end of the content
            std::size_t end = content.find('\n', start);
            // the line without its terminator
            const std::string_view line = content.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            // the trimmed upper case line the markers are recognized by
            const std::string upper = to_upper(trim(line));
            // a line after .END is data, not a directive
            if (ended) {
                // pass the line through
                result.append(line);
            }
            // a data block keeps its lines verbatim
            else if (in_data) {
                // pass the data line through
                result.append(line);
                // the block ends with .ENDDATA
                if (upper.starts_with(".ENDDATA"))
                    in_data = false;
            }
            // the .END marker ends the directive section
            else if (upper == ".END" || upper.starts_with(".END ")) {
                // pass the marker through
                result.append(line);
                // the remaining lines are not directives
                ended = true;
            }
            // a .DATA block starts after its directive
            else if (upper == ".DATA" || upper.starts_with(".DATA ")) {
                // pass the directive through
                result.append(line);
                // the following lines are data
                in_data = true;
            }
            // a print directive owns the continuation lines that follow it
            else if (!first_line && upper.starts_with(".PRINT")) {
                // the logical statement the physical lines form
                std::string statement(trim(line));
                // consume every + continuation line so a FILE= split across lines is still removed
                while (end != std::string_view::npos) {
                    // the next physical line start
                    const std::size_t next_start = end + 1;
                    // a statement that ends at the content end has no continuation
                    if (next_start >= content.size())
                        break;
                    // the next physical line end
                    const std::size_t next_end = content.find('\n', next_start);
                    // the next line without its terminator
                    const std::string_view next = content.substr(next_start, next_end == std::string_view::npos ? std::string_view::npos : next_end - next_start);
                    // the trimmed continuation body
                    const std::string next_trimmed = trim(next);
                    // only a + line continues the directive
                    if (next_trimmed.empty() || next_trimmed.front() != '+')
                        break;
                    // join the continuation into the logical statement
                    statement += ' ';
                    statement += trim(next_trimmed.substr(1));
                    // continue after the consumed continuation
                    end = next_end;
                }
                // write the directive without its FILE= option as one logical line
                result += strip_print_file_option(statement);
            }
            // every other line passes through unchanged
            else {
                // pass the line through
                result.append(line);
            }
            // the title line is behind the walk
            first_line = false;
            // a line that ended at the content end has no newline to keep
            if (end == std::string_view::npos)
                break;
            // keep the line break
            result.push_back('\n');
            // continue after the line break
            start = end + 1;
        }
        // hand out the transformed netlist
        return result;
    }

    // insert the managed directives before the last top-level .END line, or append them with a new marker
    std::string insert_run_directives(std::string body, const std::vector<std::string>& directives, const std::vector<std::string>& passthrough) {
        // the block the directives form
        std::string block;
        // append every directive in order
        for (const std::string& directive : directives) {
            if (!block.empty())
                block.push_back('\n');
            block += directive;
        }
        // append every passthrough directive in order
        for (const std::string& directive : passthrough) {
            if (!block.empty())
                block.push_back('\n');
            block += directive;
        }
        // a body without directives stays unchanged
        if (block.empty())
            return body;
        // the last line that is exactly the end marker
        std::size_t end_start = std::string::npos;
        // the current line start
        std::size_t pos = 0;
        // walk the body line by line
        while (pos <= body.size()) {
            // the current line end, or the end of the body
            const std::size_t line_end = body.find('\n', pos);
            // the line without its terminator
            const std::string_view line = std::string_view(body).substr(pos, line_end == std::string::npos ? std::string::npos : line_end - pos);
            // a top-level end marker is the insertion point
            if (to_upper(trim(line)) == ".END")
                end_start = pos;
            // a body that ended at the content end has no next line
            if (line_end == std::string::npos)
                break;
            // continue after the line break
            pos = line_end + 1;
        }
        // a body without an end marker gains one after the directives
        if (end_start == std::string::npos) {
            // end the last line before the block
            if (!body.empty() && body.back() != '\n')
                body.push_back('\n');
            // append the directives and the marker
            body += block + "\n.END\n";
            // hand out the assembled netlist
            return body;
        }
        // insert the block before the end marker
        body.insert(end_start, block + "\n");
        // hand out the assembled netlist
        return body;
    }

    // pin the run directory when it holds an unreadable produced output
    void retain_unreadable_output(TempOutputDirectory& artifacts, const std::filesystem::path& netlist_path) {
        // the directory walk reports its errors through this code
        std::error_code error;
        // walk the produced files
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(artifacts.path(), error)) {
            // only a regular file can be an output
            if (!entry.is_regular_file(error))
                continue;
            // the run netlist is not a produced output
            if (entry.path() == netlist_path)
                continue;
            // keep the directory so the refused output survives this process
            artifacts.pin();
            // surface the retained source path in the log
            spdlog::warn("Retained temporary run directory after an unreadable output: {}", artifacts.path().string());
            // the directory is retained
            return;
        }
    }
} // namespace

studio::MainWindowPresenter::MainWindowPresenter(MainWindowViewDef& view, const PluginConfig& plugin_config) :
    m_view(&view),
    // the shell starts from an untitled source, which reads live editor text until a save names a file
    m_netlist_source(std::make_unique<EditorNetlistSource>([this]() { return m_view->netlist_text(); }, std::filesystem::path{})),
    m_plugin_config(plugin_config) {
    // initialize the toolbar action states before the window is shown
    refresh_action_states();
}

void studio::MainWindowPresenter::set_dataset_file_handler(DatasetFileHandler handler) { m_dataset_file_handler = std::move(handler); }

void studio::MainWindowPresenter::set_run_output_handler(RunOutputHandler handler) { m_run_output_handler = std::move(handler); }

void studio::MainWindowPresenter::set_run_started_handler(RunStartedHandler handler) { m_run_started_handler = std::move(handler); }

void studio::MainWindowPresenter::on_open_file(const std::filesystem::path& path) {
    // a file opened from inside a live run directory outlives the run lease
    TempOutputDirectory::pin_path(path);
    // a non netlist file is offered to the charts before it is ignored
    if (path.extension().string() != ".cir") {
        // a chart dataset consumes the file and the editor keeps its content
        if (m_dataset_file_handler && m_dataset_file_handler(path, nullptr))
            return;
        // an unsupported file leaves the shell untouched
        return;
    }
    // create an editor netlist source backed by the selected file, reading live editor text
    m_netlist_source = std::make_unique<EditorNetlistSource>([this]() { return m_view->netlist_text(); }, path);
    // update the window title from the source
    set_base_title(m_netlist_source->title());
    // the netlist editor is now editable
    m_view->set_netlist_read_only(false);
    // load the netlist content
    const auto [reloaded, content] = m_netlist_source->load_netlist();
    // set the editor content to the loaded netlist
    update_netlist_editor_content(content, false);
    // refresh toolbar/menu states
    refresh_action_states();
}

void studio::MainWindowPresenter::on_save() {
    // an untitled netlist has no file behind it: ask the user for a location and filename before anything can be written
    if (!m_netlist_source->has_backing_file()) {
        // run the save-as flow through the view (native dialog)
        const auto path = m_view->request_netlist_save_path();
        // user canceled: the netlist stays dirty for the next save attempt
        if (!path.has_value())
            return;
        // default to the netlist format when the chosen name carries no extension
        std::filesystem::path target = path.value();
        if (target.extension().empty())
            target += ".cir";
        // rebind the source to the chosen file so later saves write in place
        m_netlist_source = std::make_unique<EditorNetlistSource>([this]() { return m_view->netlist_text(); }, target);
        // the chosen file becomes the window title (clears the dirty marker)
        set_base_title(m_netlist_source->title());
        // consume the file seed now, which the first load would otherwise take instead of later edits
        static_cast<void>(m_netlist_source->load_netlist());
    }
    // save content in the netlist source
    m_netlist_source->save_netlist();
    // reset the dirty flag and refresh states, which set_base_title may have already cleared
    set_netlist_editor_dirty(false);
    refresh_action_states();
}

void studio::MainWindowPresenter::on_netlist_edited() {
    // track whether the editor still holds content
    m_netlist_has_content = !m_view->netlist_text().empty();
    // mark the editor dirty and refresh states when the flag changed
    if (set_netlist_editor_dirty(true))
        refresh_action_states();
}

void studio::MainWindowPresenter::on_run_clicked() {
    // the run action doubles as the stop action while a simulation runs
    if (m_simulation_running) {
        // request a graceful shutdown through the view
        m_view->cancel_simulation_process();
        // exit
        return;
    }
    // start a simulation run over the current netlist content
    run_simulation();
}

void studio::MainWindowPresenter::run_simulation() {
    // validate the plugin configuration before launching
    if (!m_plugin_config.is_xyce_executable_valid()) {
        // update the statusbar with an error
        m_view->set_status_text("Configured Xyce executable path is invalid");
        // exit
        return;
    }
    // load the netlist source content, which reads the live editor text
    const auto [reloaded, content] = m_netlist_source->load_netlist();
    // guard against an editor that holds no netlist content
    if (content.empty()) {
        // update the statusbar
        m_view->set_status_text("No netlist content to simulate");
        // exit
        return;
    }
    // the workspace the simulator runs in, which resolves relative FILE= destinations
    const std::filesystem::path working_directory = m_netlist_source->working_directory();
    // parse the netlist once, so the run and the output resolution share the configuration it declares
    const auto [sanitized, topology] = parse_netlist(content);
    // remember the configuration and the paths a finished run resolves its datasets from
    m_run_config = SimulationConfig::from_xyce_directives(topology.m_directives);
    // remember the charts the netlist's .PLOT directives declare
    m_run_suggested_plots = plot_chart_groups(topology.m_plot_directives);
    m_run_num_ports = std::max(1, static_cast<int>(std::count_if(topology.m_devices.begin(), topology.m_devices.end(), [](const Device& device) { return device.m_type_letter == "P"; })));
    m_run_working_directory = working_directory;
    // create the private directory every output of this run writes into
    const auto run_artifacts = TempOutputDirectory::create();
    // check the run directory was created
    if (!run_artifacts) {
        // update the statusbar with an error
        m_view->set_status_text("Failed to create temporary output directory");
        // exit
        return;
    }
    // build the temporary netlist path inside the run directory
    const auto temp_path = run_artifacts->netlist_path();
    // remember the private netlist the produced file names derive from
    m_run_netlist_path = temp_path;
    // remember the title the finished run's tab shows, which the live source may no longer own
    m_run_title = m_netlist_source->title().empty() ? "Simulation" : m_netlist_source->title();
    // record the FILE= destination of every .PRINT directive with the default file the run produces for it
    m_run_output_copies = collect_print_file_copies(topology.m_directives, temp_path, working_directory);
    // rebuild the netlist without the chart-only directives, which the simulator must not see
    const std::string run_netlist = insert_run_directives(sanitized, topology.m_directives, topology.m_passthrough_directives);
    // remove every FILE= destination so the produced files stay in the private run directory
    if (!run_artifacts->write_netlist(strip_print_file_options(run_netlist))) {
        // update the statusbar with an error
        m_view->set_status_text("Failed to create temporary netlist file");
        // exit and let the run directory owner remove the empty directory
        return;
    }
    // keep the run directory alive until the run finishes, which no dataset references yet
    m_active_run_artifacts = run_artifacts;
    // mark the simulation as running
    m_simulation_running = true;
    // drop the distilled state and the remembered log of the previous run
    m_log_parser.reset();
    m_recent_log_lines.clear();
    // reset the output log for this run and reveal the panel
    m_view->clear_output();
    m_view->show_output_panel();
    // release the previous run's companions before the simulator rewrites any external file they map
    if (m_run_started_handler)
        m_run_started_handler();
    // launch the simulation through the view, which wires the runner
    m_view->start_simulation_process(m_plugin_config.xyce_executable_path(), temp_path, working_directory);
    // refresh toolbar/menu states
    refresh_action_states();
    // reveal the run indicator from the first line the parser sees
    publish_simulation_progress();
    // clear the statusbar message, the output panel already reports the run
    m_view->set_status_text("");
}

void studio::MainWindowPresenter::on_output_close() {
    // hide the output panel; the next run reveals it again
    m_view->hide_output_panel();
}

void studio::MainWindowPresenter::on_simulation_stdout(const std::string& line) {
    // classify the line, append it to the output log and refresh the progress
    handle_simulation_log_line(line, false);
}

void studio::MainWindowPresenter::on_simulation_stderr(const std::string& line) {
    // classify the line, append it to the output log and refresh the progress
    handle_simulation_log_line(line, true);
}

void studio::MainWindowPresenter::handle_simulation_log_line(const std::string& line, const bool from_stderr) {
    // drop the copy of a line the other stream already delivered, Xyce writes every message to both
    if (is_cross_stream_duplicate(line, from_stderr))
        return;
    // remember the line so the copy arriving on the other stream is recognized
    m_recent_log_lines.push_back(RememberedLogLine{.line = line, .from_stderr = from_stderr});
    // drop the oldest line once the tail is full
    if (m_recent_log_lines.size() > RECENT_LOG_LINES)
        m_recent_log_lines.pop_front();
    // the row the line is appended at, the parser records it for the first warning and error
    const std::size_t row = m_view->output_line_count();
    // classify the line by the report prefix Xyce wrote it with
    const LogSeverity severity = m_log_parser.feed(line, row);
    // forward the line to the view for display
    m_view->append_output_line(line, severity);
    // hand the refreshed progress to the view
    publish_simulation_progress();
}

void studio::MainWindowPresenter::publish_simulation_progress() {
    // the parser distills the percentage, the estimate, the phase and the counts
    const XyceLogState& state = m_log_parser.state();
    SimulationProgress progress;
    progress.running = m_simulation_running;
    progress.determinate = state.percentage.has_value();
    progress.percentage = state.percentage.value_or(0.0);
    progress.eta = state.eta;
    progress.phase = state.phase;
    progress.warnings = state.warnings;
    progress.errors = state.errors;
    // hand the refreshed run state to the status bar and the output header
    m_view->set_simulation_progress(progress);
}

bool studio::MainWindowPresenter::is_cross_stream_duplicate(const std::string& line, const bool from_stderr) const {
    // look for an identical line the other stream already delivered; a repeated line stays visible
    return std::any_of(m_recent_log_lines.begin(), m_recent_log_lines.end(), [&line, from_stderr](const RememberedLogLine& remembered) { return remembered.from_stderr != from_stderr && remembered.line == line; });
}

void studio::MainWindowPresenter::on_simulation_finished(const int exit_code, const bool was_canceled) {
    // a duplicate or late finish belongs to no live run
    if (!m_simulation_running)
        return;
    // mark the simulation as no longer running
    m_simulation_running = false;
    // refresh states so the run action reads Run again
    refresh_action_states();
    // fold the indicator away while the log keeps the final counts
    publish_simulation_progress();
    // keep the output panel visible so the log can be inspected
    m_view->show_output_panel();
    // handle canceled simulations
    if (was_canceled) {
        // update the statusbar
        m_view->set_status_text("Simulation canceled");
        // release the canceled run directory, no dataset references its outputs
        m_active_run_artifacts.reset();
        // exit
        return;
    }
    // a successful run offers its outputs to the charts before the directory lease is dropped
    if (exit_code == 0) {
        // the run directory that owns the produced files
        std::shared_ptr<TempOutputDirectory> artifacts = m_active_run_artifacts;
        // describe the finished run so the charts resolve every produced dataset from one configuration
        const RunOutput output{.config = m_run_config, .netlist_path = m_run_netlist_path, .working_directory = m_run_working_directory, .num_ports = m_run_num_ports, .suggested_plots = m_run_suggested_plots, .title = m_run_title};
        // a run output handler adopts the datasets and the directory lease together
        const bool handler_called = artifacts != nullptr && m_run_output_handler;
        // the handler reports the datasets it adopted
        const RunOutputResult result = handler_called ? m_run_output_handler(output, artifacts) : RunOutputResult{};
        // the analysis file itself decides whether its user copy may be replaced
        const bool primary_loaded = result.analysis_loaded;
        // resolve the analysis print file so a failed parse does not overwrite any user copy sharing its produced file
        std::filesystem::path analysis_output_path;
        if (const auto analysis_print = m_run_config.analysis_print_parameters())
            analysis_output_path = default_print_output_file(*analysis_print, m_run_netlist_path);
        // copy every recorded FILE= destination from the default file the run produced
        bool copy_failed = false;
        for (const PrintOutputCopy& copy : m_run_output_copies) {
            // preserve the user copy of the analysis file when no dataset could read the produced file
            if (!primary_loaded && !analysis_output_path.empty() && copy.produced_file == analysis_output_path)
                continue;
            // remember a failed destination copy so its produced file is not deleted
            if (!copy_output_to_destination(copy))
                copy_failed = true;
        }
        // retain the run directory when a copy failed, the produced file is the only remaining source
        if (copy_failed && artifacts != nullptr) {
            // pin the directory so its files survive this process
            artifacts->pin();
            // surface the retained source path in the log
            spdlog::warn("Retained temporary run directory after a failed destination copy: {}", artifacts->path().string());
        }
        // a run that expected output but produced none is reported as such, so stale charts are not mistaken for results
        const auto produced = m_run_config.produced_measurements();
        const bool expected_output = m_run_config.analysis_print_parameters().has_value() || produced.s_parameters || produced.fft || produced.fourier || produced.pce;
        // retain an unreadable produced output so the user can inspect the file the charts refused
        if (handler_called && result.outcome == RunOutputOutcome::None && expected_output && artifacts != nullptr)
            retain_unreadable_output(*artifacts, m_run_netlist_path);
        // the datasets keep the run directory alive until their tabs close
        m_active_run_artifacts.reset();
        // report the outcome, which names a missing output or a failed copy instead of a plain success
        if (handler_called && result.outcome == RunOutputOutcome::None && expected_output)
            m_view->set_status_text("Simulation finished but output file could not be found");
        else if (copy_failed)
            m_view->set_status_text("Simulation finished but output copy failed");
        else
            m_view->set_status_text("Simulation finished");
        // exit
        return;
    }
    // a failed run releases the run directory, no dataset references its outputs
    m_active_run_artifacts.reset();
    // report the failure
    m_view->set_status_text("Simulation failed with exit code " + std::to_string(exit_code));
}

bool studio::MainWindowPresenter::copy_output_to_destination(const PrintOutputCopy& copy) const {
    // no copy is needed when the destination is the produced file itself
    if (copy.destination_file == copy.produced_file)
        return true;
    // a missing produced file leaves no source to copy, which is not a failed copy
    std::error_code error;
    if (!std::filesystem::exists(copy.produced_file, error))
        return true;
    // create the destination parent directory when it is missing
    std::filesystem::create_directories(copy.destination_file.parent_path(), error);
    // write the copy beside the destination so an open mapping keeps the previous inode
    std::filesystem::path staging = copy.destination_file;
    staging += ".studio-copy";
    // copy the produced file into the staging path
    std::filesystem::copy_file(copy.produced_file, staging, std::filesystem::copy_options::overwrite_existing, error);
    // only a completed staging copy may replace the destination
    if (!error) {
        // replace the destination with the completed copy
        std::filesystem::rename(staging, copy.destination_file, error);
        // a platform that refuses to replace an open destination retries after removing it
        if (error) {
            // remove the destination so the second rename can place the copy
            std::error_code remove_error;
            std::filesystem::remove(copy.destination_file, remove_error);
            // retry the rename with the destination gone
            error.clear();
            std::filesystem::rename(staging, copy.destination_file, error);
        }
    }
    // drop a staging file the failed copy left behind
    if (error) {
        // remove the partial staging copy
        std::error_code remove_error;
        std::filesystem::remove(staging, remove_error);
        // log the destination that could not be updated and the source that is retained
        spdlog::warn("Failed to copy output file to '{}' from '{}': {}", copy.destination_file.string(), copy.produced_file.string(), error.message());
        // report the failure
        return false;
    }
    // report the successful copy
    return true;
}

const SimulationConfig& studio::MainWindowPresenter::prepare_simulation_parameters() {
    // load the current editor content, which is what the dialog describes
    const auto [reloaded, content] = m_netlist_source->load_netlist();
    // a reloaded source seeds the editor before the parse reads it back
    if (reloaded)
        update_netlist_editor_content(content, false);
    // split the managed directives out of the netlist body, which the accepted configuration rebuilds
    const auto [sanitized_netlist, topology] = parse_netlist(content);
    // remember the parse result so the accepted configuration can rebuild the netlist
    m_sanitized_netlist = sanitized_netlist;
    m_pending_topology = topology;
    m_pending_original_netlist = content;
    // build the configuration the netlist directives describe
    SimulationConfig parsed = SimulationConfig::from_xyce_directives(topology.m_directives);
    // the options belong to the netlist even when it declares no analysis
    m_simulation_config.options = parsed.options;
    // a netlist that declares an analysis replaces the analysis the dialog last accepted
    if (!std::holds_alternative<std::monostate>(parsed.analysis))
        m_simulation_config = std::move(parsed);
    // hand the dialog the configuration it edits
    return m_simulation_config;
}

void studio::MainWindowPresenter::on_simulation_parameters_accepted(const SimulationConfig& config) {
    // a configuration accepted before the netlist was parsed describes the current editor content
    if (m_pending_original_netlist.empty() && m_sanitized_netlist.empty())
        static_cast<void>(prepare_simulation_parameters());
    // store the configuration the dialog accepted, which is what a later run serializes
    m_simulation_config = config;
    // render the managed directives the configuration owns
    const std::vector<std::string> directives = m_simulation_config.to_xyce_directives(m_pending_topology);
    // re-insert them before .END in the sanitized netlist
    const std::string final_netlist = build_final_netlist(m_sanitized_netlist, directives, m_pending_topology.m_passthrough_directives);
    // an edit that changed the rendered netlist marks the editor dirty, which the save action follows
    if (update_netlist_editor_content(final_netlist, m_netlist_editor_dirty || m_pending_original_netlist != final_netlist))
        refresh_action_states();
    // the rewritten netlist is the content a later action operates on
    m_pending_original_netlist = final_netlist;
}

void studio::MainWindowPresenter::refresh_action_states() {
    // open stays available; save needs unsaved changes on content a writable source can write
    m_view->set_open_enabled(true);
    m_view->set_save_enabled(m_netlist_editor_dirty && m_netlist_has_content && !m_netlist_source->is_read_only());
    // the run action reads Run once the editor holds netlist content and Stop while the simulation runs
    m_view->set_run_enabled(m_netlist_has_content);
    // the configure action needs a netlist to describe an analysis
    m_view->set_configure_enabled(m_netlist_has_content);
    m_view->set_simulation_running(m_simulation_running);
}

bool studio::MainWindowPresenter::set_netlist_editor_dirty(bool flag) {
    // previous dirty state
    const bool previous_dirty_state = m_netlist_editor_dirty;
    // store the dirty state
    m_netlist_editor_dirty = flag;
    // prefix the base title while the editor holds unsaved changes
    m_view->set_title(flag ? "* " + m_base_title : m_base_title);
    // return true if the dirty state changed, false if it remained the same
    return previous_dirty_state != m_netlist_editor_dirty;
}

void studio::MainWindowPresenter::set_base_title(const std::string& title) {
    // store the clean base title
    m_base_title = title;
    // re-render the title with the current dirty marker preserved
    set_netlist_editor_dirty(m_netlist_editor_dirty);
}

bool studio::MainWindowPresenter::update_netlist_editor_content(const std::string& content, bool dirty_flag) {
    // set the editor content through the view
    m_view->set_netlist_text(content);
    // track whether the editor holds content
    m_netlist_has_content = !content.empty();
    // return whether the dirty state changed
    return set_netlist_editor_dirty(dirty_flag);
}

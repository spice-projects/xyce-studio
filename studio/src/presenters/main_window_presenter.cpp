#include "presenters/main_window_presenter.h"

#include <algorithm>
#include <utility>

#include "config/plugin_config.h"
#include "io/temp_output_directory.h"
#include "netlist/editor_netlist_source.h"

namespace
{
    // how many log lines the duplicate check remembers; the repeated copy of a
    // line always follows within a chunk of it
    constexpr std::size_t RECENT_LOG_LINES = 64;
} // namespace

studio::MainWindowPresenter::MainWindowPresenter(MainWindowViewDef& view, const PluginConfig& plugin_config) :
    m_view(&view),
    // the shell starts from an untitled source, which reads live editor text and
    // defers every write until a save-as names the file
    m_netlist_source(std::make_unique<EditorNetlistSource>([this]() { return m_view->netlist_text(); }, std::filesystem::path{})),
    m_plugin_config(plugin_config) {
    // initialize the toolbar action states before the window is shown
    refresh_action_states();
}

void studio::MainWindowPresenter::on_open_file(const std::filesystem::path& path) {
    // netlist file extension; the analysis output formats arrive with the
    // dataset slice and are refused until then
    if (path.extension().string() != ".cir")
        return;
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
        // consume the file seed now: the just-created source would otherwise read the file on its first load and miss later editor edits
        static_cast<void>(m_netlist_source->load_netlist());
    }
    // save content in the netlist source
    m_netlist_source->save_netlist();
    // reset the dirty flag and refresh states (set_base_title may have cleared the flag already, so the refresh is unconditional)
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
    // write the editor content as-is; the full simulation configuration
    // machinery refines what is simulated in a later slice
    if (!run_artifacts->write_netlist(content)) {
        // update the statusbar with an error
        m_view->set_status_text("Failed to create temporary netlist file");
        // exit and let the run directory owner remove the empty directory
        return;
    }
    // keep the run directory alive until the run finishes; no dataset
    // references its outputs in this slice yet
    m_active_run_artifacts = run_artifacts;
    // mark the simulation as running
    m_simulation_running = true;
    // drop the distilled state and the remembered log of the previous run
    m_log_parser.reset();
    m_recent_log_lines.clear();
    // reset the output log for this run and reveal the panel
    m_view->clear_output();
    m_view->show_output_panel();
    // launch the simulation through the view, which wires the runner
    m_view->start_simulation_process(m_plugin_config.xyce_executable_path(), temp_path, m_netlist_source->working_directory());
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
    // look for an identical line the other stream already delivered; a line the same stream repeats is
    // legitimate (Xyce separates its progress blocks with blank lines and repeats its summaries in a
    // .STEP run) and stays visible
    return std::any_of(m_recent_log_lines.begin(), m_recent_log_lines.end(), [&line, from_stderr](const RememberedLogLine& remembered) { return remembered.from_stderr != from_stderr && remembered.line == line; });
}

void studio::MainWindowPresenter::on_simulation_finished(const int exit_code, const bool was_canceled) {
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
    // release the finished run directory; the dataset slice will retain it when
    // it loads the produced output files
    m_active_run_artifacts.reset();
    // report the outcome
    if (exit_code == 0)
        m_view->set_status_text("Simulation finished");
    else
        m_view->set_status_text("Simulation failed with exit code " + std::to_string(exit_code));
}

void studio::MainWindowPresenter::refresh_action_states() {
    // open stays available while no simulation is running; save needs unsaved
    // changes on netlist content that a writable source can write
    m_view->set_open_enabled(true);
    m_view->set_save_enabled(m_netlist_editor_dirty && m_netlist_has_content && !m_netlist_source->is_read_only());
    // the run action reads Run once the editor holds netlist content and Stop while the simulation runs
    m_view->set_run_enabled(m_netlist_has_content);
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

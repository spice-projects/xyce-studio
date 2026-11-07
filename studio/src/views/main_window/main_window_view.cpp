#include "views/main_window/main_window_view.h"

#include <filesystem>
#include <utility>

#include "albor/application/application.h"
#include "albor/application/file_dialog.h"
#include "albor/core/paint.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "main_window.h"

namespace
{
    // the log the editor holds is capped, so an unbounded simulation cannot exhaust memory
    constexpr std::size_t kOutputLineCap = 10000;

    // tabs expand at eight column stops so the log columns line up
    constexpr std::size_t kTabStop = 8;

    // replace tab characters with spaces at fixed stops so the log columns line up
    std::string expand_tabs(std::string_view line) {
        std::string expanded;
        expanded.reserve(line.size());
        // the column tracks the byte position, which is where the stops count from
        std::size_t column = 0;
        for (const char c : line) {
            if (c == '\t') {
                // the stop is the distance to the next multiple of the tab width
                const std::size_t to_next_stop = kTabStop - (column % kTabStop);
                expanded.append(to_next_stop, ' ');
                column += to_next_stop;
            }
            else {
                expanded.push_back(c);
                ++column;
            }
        }
        return expanded;
    }

    // the amber the warning lines carry, which matches the slint frontend
    albor::Color warning_color() { return albor::Color::rgb(0xd9 / 255.0f, 0xa4 / 255.0f, 0x41 / 255.0f); }

    // the red the error lines carry
    albor::Color error_color() { return albor::Color::rgb(0xe0 / 255.0f, 0x5b / 255.0f, 0x4b / 255.0f); }

    // build the filter the open panel offers, which is the netlist format plus
    // every analysis output format the presenter dispatches by extension
    std::vector<albor::FileFilter> open_filters() { return {albor::FileFilter{"Xyce Files", {"cir", "raw", "prn", "csd", "csv", "dat"}}}; }
} // namespace

studio::MainWindowView::MainWindowView(albor::Application& application) :
    m_app(&application) {}

studio::MainWindowView::~MainWindowView() {
    // the presenter no longer exists, so the runner's teardown callbacks report nowhere
    m_events = nullptr;
    // a running simulation stops so its reader threads join before the ui dies
    if (m_simulation_runner != nullptr)
        m_simulation_runner->stop_and_wait();
}

bool studio::MainWindowView::create() {
    // describe the resizable shell the editor and charts will fill
    albor::WindowOptions options;
    options.title = "Xyce Studio";
    options.size = albor::Size{1200.0f, 800.0f};
    options.visible = false;
    // build the compiled shell tree in a fresh window
    m_handle = m_app->create_window(options, m_root);
    // a refused window leaves nothing to theme or show
    if (!m_handle.valid() || m_root == nullptr) {
        m_handle = {};
        return false;
    }
    // theme the tree before the first frame, which is what keeps every chrome widget palette driven
    m_theme = install_theme(*m_app, m_handle);
    // the shell is built but hidden, which the caller reveals once it is wired
    if (albor::Window* window = m_app->window(m_handle))
        window->show();
    return true;
}

albor::WindowHandle studio::MainWindowView::handle() const { return m_handle; }

void studio::MainWindowView::set_on_plugin_settings(std::function<void()> handler) {
    // without a live facade there is no signal to bind
    if (m_root == nullptr)
        return;
    m_root->set_on_plugin_settings_clicked(std::move(handler));
}

void studio::MainWindowView::on_window_closed(albor::WindowHandle handle) {
    // ignore windows this view does not own
    if (!(handle == m_handle))
        return;
    // the facade dies with the window, which the ui setters must not touch
    m_root = nullptr;
    m_handle = {};
}

void studio::MainWindowView::set_event_handler(MainWindowViewEvents* handler) {
    // remember the presenter the shell reports to
    m_events = handler;
    // without a live facade there is nothing to bind
    if (m_root == nullptr)
        return;
    // the open action resolves the native panel before the presenter sees it
    m_root->set_on_open_clicked([this] { handle_open(); });
    // the save action reaches the presenter directly
    m_root->set_on_save_clicked([this] {
        if (m_events != nullptr)
            m_events->on_save();
    });
    // an edit reports through the same seam
    m_root->set_on_netlist_edited([this] {
        if (m_events != nullptr)
            m_events->on_netlist_edited();
    });
    // the run action starts or stops the simulation
    m_root->set_on_run_clicked([this] {
        if (m_events != nullptr)
            m_events->on_run_clicked();
    });
    // the close action folds the output panel away
    m_root->set_on_output_close_clicked([this] {
        if (m_events != nullptr)
            m_events->on_output_close();
    });
}

void studio::MainWindowView::set_netlist_text(std::string_view content) {
    if (m_root == nullptr)
        return;
    m_root->set_netlist_text(std::string(content));
}

std::string studio::MainWindowView::netlist_text() const {
    // a closed shell edits nothing
    if (m_root == nullptr)
        return {};
    return m_root->netlist_text();
}

void studio::MainWindowView::set_netlist_read_only(bool read_only) {
    if (m_root != nullptr)
        m_root->set_netlist_read_only(read_only);
}

void studio::MainWindowView::set_status_text(std::string_view text) {
    if (m_root != nullptr)
        m_root->set_status(std::string(text));
}

void studio::MainWindowView::set_title(std::string_view title) {
    // the platform window title carries the netlist state
    if (albor::Window* window = m_app->window(m_handle))
        window->set_title(std::string(title));
}

void studio::MainWindowView::set_open_enabled(bool enabled) {
    if (m_root != nullptr)
        m_root->set_open_enabled(enabled);
}

void studio::MainWindowView::set_save_enabled(bool enabled) {
    if (m_root != nullptr)
        m_root->set_save_enabled(enabled);
}

void studio::MainWindowView::set_run_enabled(bool enabled) {
    if (m_root != nullptr)
        m_root->set_run_enabled(enabled);
}

void studio::MainWindowView::set_simulation_running(bool running) {
    if (m_root == nullptr)
        return;
    // the action reads stop while the run lives, which the label carries
    m_root->set_run_label(running ? "Stop" : "Run");
}

void studio::MainWindowView::show_output_panel() {
    if (m_root != nullptr)
        m_root->set_output_visible(true);
}

void studio::MainWindowView::hide_output_panel() {
    if (m_root != nullptr)
        m_root->set_output_visible(false);
}

void studio::MainWindowView::clear_output() {
    // drop the buffered lines so a new run starts from an empty log
    m_output_pending.clear();
    m_output_text.clear();
    m_output_decorations.clear();
    m_output_line_count = 0;
    m_output_trim_offset = 0;
    if (m_root == nullptr)
        return;
    m_root->set_output_text(m_output_text);
    m_root->set_output_decorations(m_output_decorations);
}

void studio::MainWindowView::append_output_line(std::string_view line, LogSeverity severity) {
    // the line joins the batch, expanded once so the flush copies each line exactly once
    m_output_pending.emplace_back(expand_tabs(line), severity);
    // one flush per event loop iteration, which coalesces the per line facade writes
    if (m_output_flush_scheduled)
        return;
    m_output_flush_scheduled = true;
    m_app->post([this] { flush_output(); });
}

std::size_t studio::MainWindowView::output_line_count() const { return m_output_line_count; }

void studio::MainWindowView::flush_output() {
    // the flush is running, so the next batch schedules its own task
    m_output_flush_scheduled = false;
    // the facade is gone, so nothing can be rendered anymore
    if (m_root == nullptr) {
        m_output_pending.clear();
        return;
    }
    // every buffered line joins the log, styled by its severity
    for (const auto& [line, severity] : m_output_pending) {
        const std::size_t start = m_output_text.size();
        m_output_text.append(line);
        m_output_text.push_back('\n');
        ++m_output_line_count;
        // a warning or error line takes the severity colour as one decoration over the whole line
        if (severity == LogSeverity::warning || severity == LogSeverity::error)
            m_output_decorations.push_back(albor::TextDecoration{.start = start, .end = start + line.size(), .foreground = severity == LogSeverity::warning ? warning_color() : error_color()});
    }
    m_output_pending.clear();
    // trim the oldest lines once the cap is exceeded, which keeps the newest ones
    while (m_output_line_count > kOutputLineCap) {
        const std::size_t newline = m_output_text.find('\n', m_output_trim_offset);
        // a log without a newline cannot be trimmed line by line
        if (newline == std::string::npos)
            break;
        m_output_trim_offset = newline + 1;
        --m_output_line_count;
    }
    // a trim rewrote the text, so the decorations shift into the kept range
    if (m_output_trim_offset > 0) {
        m_output_text.erase(0, m_output_trim_offset);
        std::vector<albor::TextDecoration> shifted;
        shifted.reserve(m_output_decorations.size());
        for (const auto& decoration : m_output_decorations) {
            // a decoration entirely inside the dropped head is gone
            if (decoration.end <= m_output_trim_offset)
                continue;
            shifted.push_back(albor::TextDecoration{.start = decoration.start > m_output_trim_offset ? decoration.start - m_output_trim_offset : 0, .end = decoration.end - m_output_trim_offset, .foreground = decoration.foreground, .background = decoration.background, .underline = decoration.underline});
        }
        m_output_decorations = std::move(shifted);
        m_output_trim_offset = 0;
    }
    // the facade reads the log and its decorations as one binding each
    m_root->set_output_text(m_output_text);
    m_root->set_output_decorations(m_output_decorations);
}

void studio::MainWindowView::start_simulation_process(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) {
    // recreate the runner so a finished process never blocks the next run
    m_simulation_runner = std::make_unique<SimulationRunner>([this](std::function<void()> task) {
        // the runner dispatches its callbacks onto the ui thread, which the event loop owns
        m_app->post(std::move(task));
    });
    // the console lines reach the presenter through the same seam as the ui events
    m_simulation_runner->set_stdout_callback([this](const std::string& line) {
        if (m_events != nullptr)
            m_events->on_simulation_stdout(line);
    });
    m_simulation_runner->set_stderr_callback([this](const std::string& line) {
        if (m_events != nullptr)
            m_events->on_simulation_stderr(line);
    });
    m_simulation_runner->set_finished_callback([this](int exit_code, bool was_canceled) {
        if (m_events != nullptr)
            m_events->on_simulation_finished(exit_code, was_canceled);
    });
    m_simulation_runner->start(program, netlist_path, working_directory);
}

void studio::MainWindowView::cancel_simulation_process() {
    if (m_simulation_runner != nullptr)
        m_simulation_runner->cancel();
}

std::optional<std::filesystem::path> studio::MainWindowView::request_netlist_save_path() {
    // describe the native save panel, owned by the shell so it presents as its sheet
    albor::SaveFileOptions options;
    options.parent = m_handle;
    options.title = "Save Netlist";
    options.default_name = "untitled.cir";
    return m_app->save_file(options);
}

void studio::MainWindowView::handle_open() {
    // the chosen path reaches the presenter, or nothing when the panel was cancelled
    const std::optional<std::filesystem::path> path = run_open_panel("Open Netlist");
    if (path.has_value() && m_events != nullptr)
        m_events->on_open_file(*path);
}

std::optional<std::filesystem::path> studio::MainWindowView::run_open_panel(const std::string& title) {
    albor::OpenFileOptions options;
    options.parent = m_handle;
    options.title = title;
    options.filters = open_filters();
    return m_app->open_file(options);
}

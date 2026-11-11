#include "views/main_window/main_window_view.h"

#include <cmath>
#include <filesystem>
#include <utility>

#include "albor/application/application.h"
#include "albor/application/file_dialog.h"
#include "albor/core/clipboard.h"
#include "albor/core/color_scheme.h"
#include "albor/core/paint.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/text_field.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "main_window.h"
#include "netlist/netlist_lexer.h"
#include "views/charts/charts_view.h"

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

    // the colour a netlist token draws with, matching the slint palette
    albor::Color netlist_token_color(NetlistTokenType type, bool dark_mode) {
        if (dark_mode) {
            switch (type) {
            case NetlistTokenType::COMMENT:
                return albor::Color::rgb(0x8b / 255.0f, 0x94 / 255.0f, 0x9e / 255.0f);
            case NetlistTokenType::DIRECTIVE:
                return albor::Color::rgb(0x79 / 255.0f, 0xc0 / 255.0f, 0xff / 255.0f);
            case NetlistTokenType::DEVICE:
                return albor::Color::rgb(0xff / 255.0f, 0xa6 / 255.0f, 0x57 / 255.0f);
            case NetlistTokenType::NUMBER:
                return albor::Color::rgb(0xa5 / 255.0f, 0xd6 / 255.0f, 0xff / 255.0f);
            case NetlistTokenType::EXPRESSION:
                return albor::Color::rgb(0xff / 255.0f, 0x7b / 255.0f, 0x72 / 255.0f);
            case NetlistTokenType::STRING_LITERAL:
                return albor::Color::rgb(0x7e / 255.0f, 0xe7 / 255.0f, 0x87 / 255.0f);
            case NetlistTokenType::CONTINUATION:
                return albor::Color::rgb(0xd2 / 255.0f, 0xa8 / 255.0f, 0xff / 255.0f);
            case NetlistTokenType::KEYWORD:
                return albor::Color::rgb(0xff / 255.0f, 0x88 / 255.0f, 0x88 / 255.0f);
            case NetlistTokenType::MODEL:
                return albor::Color::rgb(0xa5 / 255.0f, 0xd6 / 255.0f, 0xff / 255.0f);
            case NetlistTokenType::OPERATOR:
                return albor::Color::rgb(0xe0 / 255.0f, 0xe0 / 255.0f, 0xe0 / 255.0f);
            default:
                return {};
            }
        }
        switch (type) {
        case NetlistTokenType::COMMENT:
            return albor::Color::rgb(0x6a / 255.0f, 0x73 / 255.0f, 0x7d / 255.0f);
        case NetlistTokenType::DIRECTIVE:
            return albor::Color::rgb(0x05 / 255.0f, 0x50 / 255.0f, 0xae / 255.0f);
        case NetlistTokenType::DEVICE:
            return albor::Color::rgb(0x95 / 255.0f, 0x38 / 255.0f, 0x00 / 255.0f);
        case NetlistTokenType::NUMBER:
            return albor::Color::rgb(0x0a / 255.0f, 0x30 / 255.0f, 0x69 / 255.0f);
        case NetlistTokenType::EXPRESSION:
            return albor::Color::rgb(0xcf / 255.0f, 0x22 / 255.0f, 0x2e / 255.0f);
        case NetlistTokenType::STRING_LITERAL:
            return albor::Color::rgb(0x11 / 255.0f, 0x63 / 255.0f, 0x29 / 255.0f);
        case NetlistTokenType::CONTINUATION:
            return albor::Color::rgb(0x82 / 255.0f, 0x50 / 255.0f, 0xdf / 255.0f);
        case NetlistTokenType::KEYWORD:
            return albor::Color::rgb(0xcc / 255.0f, 0x22 / 255.0f, 0x22 / 255.0f);
        case NetlistTokenType::MODEL:
            return albor::Color::rgb(0x0a / 255.0f, 0x30 / 255.0f, 0x69 / 255.0f);
        case NetlistTokenType::OPERATOR:
            return albor::Color::rgb(0x22 / 255.0f, 0x22 / 255.0f, 0x22 / 255.0f);
        default:
            return {};
        }
    }

    // build the styled ranges over a netlist, one per highlighted token
    std::vector<albor::TextDecoration> build_netlist_decorations(std::string_view text, bool dark_mode) {
        std::vector<albor::TextDecoration> decorations;
        std::size_t offset = 0;
        for (const NetlistTokenLine& line : tokenize_netlist(text)) {
            for (const NetlistToken& token : line.m_tokens) {
                // the plain tokens follow the field text, so they need no range
                if (token.m_type != NetlistTokenType::WHITESPACE && token.m_type != NetlistTokenType::NODE && token.m_type != NetlistTokenType::PLAIN_TEXT) {
                    decorations.push_back(albor::TextDecoration{.start = offset, .end = offset + token.m_text.size(), .foreground = netlist_token_color(token.m_type, dark_mode)});
                }
                offset += token.m_text.size();
            }
            offset += line.m_line_ending.size();
        }
        return decorations;
    }

    // spell one warning or error count for the output header badge
    std::string count_text(int count, std::string_view noun) { return std::to_string(count) + " " + std::string(noun) + (count == 1 ? "" : "s"); }

    // fold the run state into the one line the status bar shows
    std::string compose_progress_text(const studio::SimulationProgress& progress) {
        std::string text;
        if (progress.determinate) {
            text = std::to_string(static_cast<int>(std::lround(progress.percentage))) + "%";
            if (!progress.eta.empty())
                text += " · ETA " + progress.eta;
        }
        else if (!progress.phase.empty()) {
            text = progress.phase;
        }
        else {
            text = "Running...";
        }
        if (progress.warnings > 0)
            text += " · " + std::to_string(progress.warnings) + (progress.warnings == 1 ? " warning" : " warnings");
        if (progress.errors > 0)
            text += " · " + std::to_string(progress.errors) + (progress.errors == 1 ? " error" : " errors");
        return text;
    }

    // find a node by its author id, walking the retained tree
    albor::UiNode* find_node(albor::UiNode& node, std::string_view explicit_id) {
        if (node.identity().explicit_id == explicit_id)
            return &node;
        for (const auto& child : node.children()) {
            if (albor::UiNode* found = find_node(*child, explicit_id); found != nullptr)
                return found;
        }
        return nullptr;
    }

    // find a text field by its author id, walking the retained tree
    albor::TextField* find_text_field(albor::UiNode& node, std::string_view explicit_id) {
        if (node.identity().explicit_id == explicit_id) {
            if (auto* field = dynamic_cast<albor::TextField*>(&node))
                return field;
        }
        for (const auto& child : node.children()) {
            if (auto* found = find_text_field(*child, explicit_id))
                return found;
        }
        return nullptr;
    }

    // build the filter the open panel offers, which is the netlist format and its outputs
    std::vector<albor::FileFilter> open_filters() { return {albor::FileFilter{"Xyce Files", {"cir", "raw", "prn", "csd", "csv", "dat", "s1p", "s2p", "s3p", "s4p", "ts"}}}; }
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
    // a scheme flip rebuilds the token colours and the chart palette
    if (m_theme != nullptr)
        m_theme->set_scheme_changed_handler([this](const albor::ColorScheme scheme) {
            schedule_netlist_highlight();
            if (m_on_scheme_changed)
                m_on_scheme_changed(scheme == albor::ColorScheme::Dark);
        });
    // create the native charts view and insert it into the host the shell always carries
    if (albor::WindowStage* stage = m_app->stage(m_handle)) {
        if (albor::UiNode* host = find_node(*m_root, "charts_host")) {
            auto charts = std::make_unique<ChartsView>(&stage->binding_engine(), &m_app->timers());
            // the context menu measures its rows through the window text engine
            charts->set_text_engine(&stage->text_engine());
            m_charts_view = charts.get();
            host->add_child(std::move(charts));
            host->invalidate_layout();
        }
    }
    // seed the editor highlight from the empty netlist
    rebuild_netlist_highlight();
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

void studio::MainWindowView::set_on_configure_simulation(std::function<void()> handler) {
    // without a live facade there is no signal to bind
    if (m_root == nullptr)
        return;
    m_root->set_on_configure_simulation_clicked(std::move(handler));
}

void studio::MainWindowView::set_on_show_charts(std::function<void()> handler) {
    // without a live facade there is no signal to bind
    if (m_root == nullptr)
        return;
    m_root->set_on_show_charts_clicked(std::move(handler));
}

void studio::MainWindowView::set_charts_visible(const bool visible) {
    // without a live facade there is no body to swap
    if (m_root == nullptr)
        return;
    m_root->set_charts_visible(visible);
}

void studio::MainWindowView::set_charts_enabled(const bool enabled) {
    // without a live facade there is no action to enable
    if (m_root == nullptr)
        return;
    m_root->set_charts_enabled(enabled);
}

studio::ChartsView* studio::MainWindowView::charts_view() const { return m_charts_view; }

void studio::MainWindowView::set_on_scheme_changed(std::function<void(bool)> handler) { m_on_scheme_changed = std::move(handler); }

bool studio::MainWindowView::is_dark() const { return m_theme != nullptr && m_theme->scheme() == albor::ColorScheme::Dark; }

void studio::MainWindowView::on_window_closed(albor::WindowHandle handle) {
    // ignore windows this view does not own
    if (!(handle == m_handle))
        return;
    // the facade dies with the window, which the ui setters must not touch
    m_root = nullptr;
    // the charts view dies with the tree
    m_charts_view = nullptr;
    // the window handle no longer names a live window
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
    // an edit reports through the same seam, and rebuilds the token colours once per batch
    m_root->set_on_netlist_edited([this] {
        schedule_netlist_highlight();
        if (m_events != nullptr)
            m_events->on_netlist_edited();
    });
    // the run action starts or stops the simulation
    m_root->set_on_run_clicked([this] {
        if (m_events != nullptr)
            m_events->on_run_clicked();
    });
    // the output action reveals the panel the close action folded away
    m_root->set_on_output_clicked([this] { show_output_panel(); });
    // the close action folds the output panel away
    m_root->set_on_output_close_clicked([this] {
        if (m_events != nullptr)
            m_events->on_output_close();
    });
    // the copy action copies the selection, or the whole log when nothing is selected
    m_root->set_on_output_copy_clicked([this] { copy_output(); });
    // the exit action closes the shell window, which the application tears down
    m_root->set_on_exit_clicked([this] { (void)m_app->request_close(m_handle); });
}

void studio::MainWindowView::set_netlist_text(std::string_view content) {
    if (m_root == nullptr)
        return;
    m_root->set_netlist_text(std::string(content));
    // the loaded text carries its own token colours
    rebuild_netlist_highlight();
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

void studio::MainWindowView::set_configure_enabled(bool enabled) {
    if (m_root != nullptr)
        m_root->set_configure_enabled(enabled);
}

void studio::MainWindowView::set_simulation_running(bool running) {
    if (m_root == nullptr)
        return;
    // an unchanged state leaves the toolbar subtree and the theme alone
    if (m_simulation_running == running)
        return;
    // the action reads stop while the run lives, which swaps the toolbar subtree
    m_simulation_running = running;
    m_root->set_simulation_running(running);
}

void studio::MainWindowView::set_simulation_progress(const SimulationProgress& progress) {
    if (m_root == nullptr)
        return;
    // the output header shows one badge per reported severity
    m_root->set_output_warning_text(count_text(progress.warnings, "warning"));
    m_root->set_output_warning_visible(progress.warnings > 0);
    m_root->set_output_error_text(count_text(progress.errors, "error"));
    m_root->set_output_error_visible(progress.errors > 0);
    // the status line carries the phase, the estimate and the counts
    m_root->set_progress_text(compose_progress_text(progress));
    m_root->set_progress_determinate(progress.determinate);
    m_root->set_progress_value(progress.determinate ? static_cast<float>(progress.percentage / 100.0) : 0.0f);
    // only a presence change rebuilds the indicator subtree
    if (m_progress_visible != progress.running) {
        m_progress_visible = progress.running;
        m_root->set_progress_visible(progress.running);
    }
}

void studio::MainWindowView::show_output_panel() {
    if (m_root == nullptr)
        return;
    // a revealed panel makes its log reachable from the toolbar
    m_root->set_output_available(true);
    // an already mounted panel needs no rebuild
    if (m_output_visible)
        return;
    m_output_visible = true;
    m_root->set_output_visible(true);
}

void studio::MainWindowView::hide_output_panel() {
    if (m_root == nullptr)
        return;
    // an already hidden panel leaves the tree and the theme alone
    if (!m_output_visible)
        return;
    m_output_visible = false;
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
    // an empty log has no content the toolbar action could reveal
    m_root->set_output_available(false);
    // an empty log reports no severity, so both header badges are hidden
    m_root->set_output_warning_text("");
    m_root->set_output_warning_visible(false);
    m_root->set_output_error_text("");
    m_root->set_output_error_visible(false);
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
    // any log line makes the panel reachable from the toolbar again
    m_root->set_output_available(true);
}

void studio::MainWindowView::rebuild_netlist_highlight() {
    // the queued task is running, so the next edit schedules its own
    m_netlist_highlight_scheduled = false;
    // the facade or the theme is gone, so there are no colours to resolve
    if (m_root == nullptr || m_theme == nullptr)
        return;
    // the installed scheme decides which token palette applies
    const bool dark_mode = m_theme->scheme() == albor::ColorScheme::Dark;
    m_root->set_netlist_decorations(build_netlist_decorations(m_root->netlist_text(), dark_mode));
}

void studio::MainWindowView::schedule_netlist_highlight() {
    // one rebuild per event loop iteration, which coalesces the per keystroke edits
    if (m_netlist_highlight_scheduled)
        return;
    m_netlist_highlight_scheduled = true;
    m_app->post([this] { rebuild_netlist_highlight(); });
}

void studio::MainWindowView::copy_output() {
    // a selection in the log copies through the field, which keeps its own clipboard path
    if (m_handle.valid()) {
        if (albor::UiNode* root = m_app->root(m_handle)) {
            if (albor::TextField* log = find_text_field(*root, "output_log"); log != nullptr && log->has_selection()) {
                log->copy();
                return;
            }
        }
    }
    // without a selection the whole log is the copy
    if (albor::Clipboard* clipboard = albor::process_clipboard(); clipboard != nullptr && clipboard->ready())
        clipboard->set_text(m_output_text);
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

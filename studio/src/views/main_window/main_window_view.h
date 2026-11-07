#pragma once

#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "albor/window/window_handle.h"
#include "runner/simulation_runner.h"
#include "views/main_window/main_window_view_def.h"

namespace albor
{
    class Application;
    class MainWindow;
    class ThemeController;
    struct TextDecoration;
} // namespace albor

namespace studio
{
    // the Albor shell window; it implements the surface the presenter drives,
    // resolves the native file panels itself and owns the simulation process
    class MainWindowView final : public MainWindowViewDef
    {
    public:
        explicit MainWindowView(albor::Application& application);

        MainWindowView(const MainWindowView&) = delete;

        MainWindowView& operator=(const MainWindowView&) = delete;

        ~MainWindowView() override;

        // create and show the shell window, returning false when the platform refused it
        [[nodiscard]] bool create();

        // hand out the window handle the dialog parent is resolved from
        [[nodiscard]] albor::WindowHandle handle() const;

        // install the handler the plugin settings action reaches
        void set_on_plugin_settings(std::function<void()> handler);

        // clear the borrowed facade when the application reports the window is gone
        void on_window_closed(albor::WindowHandle handle);

        // install the presenter that receives the shell interactions; null clears it
        void set_event_handler(MainWindowViewEvents* handler) override;

        // replace the netlist text the editor edits, which seeds the surface without reporting an edit
        void set_netlist_text(std::string_view content) override;

        // hand out the netlist text the editor currently edits
        [[nodiscard]] std::string netlist_text() const override;

        // set or clear the editor read only state
        void set_netlist_read_only(bool read_only) override;

        // replace the one line status the shell reports
        void set_status_text(std::string_view text) override;

        // replace the window title, which the presenter drives from the netlist state
        void set_title(std::string_view title) override;

        // enable or disable the open action
        void set_open_enabled(bool enabled) override;

        // enable or disable the save action
        void set_save_enabled(bool enabled) override;

        // enable or disable the run action
        void set_run_enabled(bool enabled) override;

        // swap the run action label between run and stop
        void set_simulation_running(bool running) override;

        // render the live run state in the status bar and the output header
        void set_simulation_progress(const SimulationProgress& progress) override;

        // reveal the simulation output panel over the lower part of the shell
        void show_output_panel() override;

        // hide the simulation output panel
        void hide_output_panel() override;

        // drop every buffered output line, which a new run starts from
        void clear_output() override;

        // append one console line to the output log, styled by its severity
        void append_output_line(std::string_view line, LogSeverity severity) override;

        // hand out how many lines the output log holds
        [[nodiscard]] std::size_t output_line_count() const override;

        // launch the simulation process over the given netlist
        void start_simulation_process(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) override;

        // request a graceful shutdown of the running simulation process
        void cancel_simulation_process() override;

        // ask the user for the save location and filename of an untitled netlist
        [[nodiscard]] std::optional<std::filesystem::path> request_netlist_save_path() override;

    private:
        // flush every buffered output line into the facade in one pass
        void flush_output();

        // reapply the installed theme over the tree after a conditional subtree mounts (albor issue #173)
        void apply_runtime_theme();

        // rebuild the netlist token colours after a load, edit or theme change
        void rebuild_netlist_highlight();

        // queue one highlight rebuild per event loop iteration, which coalesces typing
        void schedule_netlist_highlight();

        // copy the log selection, or the whole log when nothing is selected
        void copy_output();

        // run the open panel and hand the chosen file to the presenter
        void handle_open();

        // run the open panel over the given options, reporting the choice or nothing
        [[nodiscard]] std::optional<std::filesystem::path> run_open_panel(const std::string& title);

        // the application that owns the window and its engines
        albor::Application* m_app = nullptr;

        // the shell window, invalid while it is closed
        albor::WindowHandle m_handle;

        // the borrowed generated facade
        albor::MainWindow* m_root = nullptr;

        // the presenter that receives the shell interactions, null while none is wired
        MainWindowViewEvents* m_events = nullptr;

        // the theme bindings installed over the shell tree
        std::unique_ptr<albor::ThemeController> m_theme;

        // the whole log as one string, one line per row
        std::string m_output_text;

        // the styled ranges over the log, one per warning or error line
        std::vector<albor::TextDecoration> m_output_decorations;

        // how many lines the log holds
        std::size_t m_output_line_count = 0;

        // the byte the kept log starts at, which the cap trim advances
        std::size_t m_output_trim_offset = 0;

        // the lines that arrived since the last flush, which coalesces the
        // per line facade writes into one pass per event loop iteration
        std::deque<std::pair<std::string, LogSeverity>> m_output_pending;

        // whether a flush task is already queued, which keeps one flush per batch
        bool m_output_flush_scheduled = false;

        // whether the output panel is currently mounted
        bool m_output_visible = false;

        // whether the run action currently reads stop
        bool m_simulation_running = false;

        // whether the status bar currently carries the run indicator
        bool m_progress_visible = false;

        // whether a highlight rebuild task is already queued, which coalesces typing
        bool m_netlist_highlight_scheduled = false;

        // whether a conditional subtree mounted since the last theme pass (albor issue #173)
        bool m_runtime_theme_pending = false;

        // the simulation process, recreated for every run
        std::unique_ptr<SimulationRunner> m_simulation_runner;
    };
} // namespace studio

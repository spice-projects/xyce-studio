#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "simulation/xyce_log_parser.h"

namespace studio
{
    // the run state the status bar renders while a simulation is live
    struct SimulationProgress
    {
        // whether the run is still in flight, which reveals the indicator
        bool running = false;
        // whether Xyce reported a completion percentage, which makes the bar determinate
        bool determinate = false;
        // the reported completion percentage in the 0..100 range
        double percentage = 0.0;
        // the estimate Xyce spelled out, empty until it reported one
        std::string eta;
        // the analysis phase Xyce announced last, empty until it announced one
        std::string phase;
        // the warnings and errors reported so far
        int warnings = 0;
        int errors = 0;
    };

    // the interactions the shell reports to its presenter
    class MainWindowViewEvents
    {
    public:
        virtual ~MainWindowViewEvents() = default;

        // the user picked a file in the open panel, which the view resolved
        virtual void on_open_file(const std::filesystem::path& path) = 0;

        // the user asked to save the netlist
        virtual void on_save() = 0;

        // the user edited the netlist text
        virtual void on_netlist_edited() = 0;

        // the user pressed the run action, which starts or stops the simulation
        virtual void on_run_clicked() = 0;

        // the user closed the simulation output panel
        virtual void on_output_close() = 0;

        // one console line the simulation process wrote, which the runner
        // dispatched onto the ui thread
        virtual void on_simulation_stdout(const std::string& line) = 0;

        // one diagnostic line the simulation process wrote on its error stream
        virtual void on_simulation_stderr(const std::string& line) = 0;

        // the simulation process ended, either by itself or through a cancel
        virtual void on_simulation_finished(int exit_code, bool was_canceled) = 0;
    };

    // the surface the main window presenter drives, abstracted from the window toolkit
    class MainWindowViewDef
    {
    public:
        virtual ~MainWindowViewDef() = default;

        // install the presenter that receives the shell interactions; null clears it
        virtual void set_event_handler(MainWindowViewEvents* handler) = 0;

        // replace the netlist text the editor edits, which seeds the surface without reporting an edit
        virtual void set_netlist_text(std::string_view content) = 0;

        // hand out the netlist text the editor currently edits
        [[nodiscard]] virtual std::string netlist_text() const = 0;

        // set or clear the editor read only state
        virtual void set_netlist_read_only(bool read_only) = 0;

        // replace the one line status the shell reports
        virtual void set_status_text(std::string_view text) = 0;

        // replace the window title, which the presenter drives from the netlist state
        virtual void set_title(std::string_view title) = 0;

        // enable or disable the open action
        virtual void set_open_enabled(bool enabled) = 0;

        // enable or disable the save action
        virtual void set_save_enabled(bool enabled) = 0;

        // enable or disable the run action, which the action states refresh
        virtual void set_run_enabled(bool enabled) = 0;

        // swap the run action label between run and stop, which the running state drives
        virtual void set_simulation_running(bool running) = 0;

        // render the live run state in the status bar and the output header
        virtual void set_simulation_progress(const SimulationProgress& progress) = 0;

        // reveal the simulation output panel over the lower part of the shell
        virtual void show_output_panel() = 0;

        // hide the simulation output panel
        virtual void hide_output_panel() = 0;

        // drop every buffered output line, which a new run starts from
        virtual void clear_output() = 0;

        // append one console line to the output log, styled by its severity
        virtual void append_output_line(std::string_view line, LogSeverity severity) = 0;

        // hand out how many lines the output log holds, which the parser rows are indexed by
        [[nodiscard]] virtual std::size_t output_line_count() const = 0;

        // launch the simulation process over the given netlist, which streams
        // its console output through the simulation events
        virtual void start_simulation_process(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) = 0;

        // request a graceful shutdown of the running simulation process
        virtual void cancel_simulation_process() = 0;

        // ask the user for the save location and filename of an untitled netlist
        [[nodiscard]] virtual std::optional<std::filesystem::path> request_netlist_save_path() = 0;
    };
} // namespace studio

#pragma once

#include <cstddef>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>

#include "netlist/netlist_source.h"
#include "simulation/xyce_log_parser.h"
#include "views/main_window/main_window_view_def.h"

class PluginConfig;
class TempOutputDirectory;

namespace studio
{
    // owns the netlist lifecycle of the shell: open, save, the unsaved changes
    // marker and the simulation run the shell streams into its output panel
    class MainWindowPresenter final : public MainWindowViewEvents
    {
    public:
        // the shell surface and the plugin configuration must outlive the presenter
        MainWindowPresenter(MainWindowViewDef& view, const PluginConfig& plugin_config);

        MainWindowPresenter(const MainWindowPresenter&) = delete;

        MainWindowPresenter& operator=(const MainWindowPresenter&) = delete;

        // the user picked a file in the open panel, which the view resolved
        void on_open_file(const std::filesystem::path& path) override;

        // the user asked to save the netlist
        void on_save() override;

        // the user edited the netlist text
        void on_netlist_edited() override;

        // the user pressed the run action, which starts or stops the simulation
        void on_run_clicked() override;

        // the user closed the simulation output panel
        void on_output_close() override;

        // one console line the simulation process wrote on its standard output
        void on_simulation_stdout(const std::string& line) override;

        // one diagnostic line the simulation process wrote on its error stream
        void on_simulation_stderr(const std::string& line) override;

        // the simulation process ended, either by itself or through a cancel
        void on_simulation_finished(int exit_code, bool was_canceled) override;

    private:
        // start a simulation run over the current netlist content
        void run_simulation();

        // classify one console line, append it to the output log and refresh the progress
        void handle_simulation_log_line(const std::string& line, bool from_stderr);

        // re-render the one line status from the log parser state
        void publish_simulation_progress();

        // check whether the same line already arrived on the other stream, which
        // Xyce writes when a message goes to both
        bool is_cross_stream_duplicate(const std::string& line, bool from_stderr) const;

        // one remembered log line with the stream it arrived on
        struct RememberedLogLine
        {
            std::string line;
            bool from_stderr = false;
        };

        // re-apply the toolbar action enablement for the current state
        void refresh_action_states();

        // store the dirty state and re-render the title with the marker
        bool set_netlist_editor_dirty(bool flag);

        // store the clean title and re-render the title
        void set_base_title(const std::string& title);

        // set the editor content and the tracked dirty flag
        bool update_netlist_editor_content(const std::string& content, bool dirty_flag);

        // the shell surface the presenter drives
        MainWindowViewDef* m_view;

        // the source the netlist loads from and saves into
        std::unique_ptr<NetlistSource> m_netlist_source;

        // the plugin configuration the Xyce executable path is read from
        const PluginConfig& m_plugin_config;

        // the clean title without the unsaved changes marker
        std::string m_base_title;

        // whether the editor holds unsaved changes
        bool m_netlist_editor_dirty = false;

        // whether the editor holds content
        bool m_netlist_has_content = false;

        // whether a simulation process is live
        bool m_simulation_running = false;

        // the parser that distills the Xyce console log into a run state
        XyceLogParser m_log_parser;

        // the tail of log lines remembered for the cross stream duplicate check
        std::deque<RememberedLogLine> m_recent_log_lines;

        // the private run directory the live simulation writes its outputs into
        std::shared_ptr<TempOutputDirectory> m_active_run_artifacts;
    };
} // namespace studio

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../config/plugin_config.h"
#include "../dsp/fft.h"
#include "../io/xyce_output_file.h"
#include "../netlist/netlist.h"
#include "../netlist/netlist_source.h"
#include "../simulation/simulation_config.h"
#include "main_window_view_def.h"

class KiCadSession;

// business/orchestration logic for the slint main window, decoupled from the
// ui through MainWindowViewDef and MainWindowViewDefEvents; the presenter
// implements the event handler interface and receives user-interaction
// callbacks from the view without the view knowing the presenter exists
class SlintMainWindowPresenter : public MainWindowViewDefEvents
{
public:
    SlintMainWindowPresenter(MainWindowViewDef& view, std::unique_ptr<NetlistSource> netlist_source, PluginConfig plugin_config, std::shared_ptr<KiCadSession> kicad_session);

    ~SlintMainWindowPresenter() override;

    SlintMainWindowPresenter(const SlintMainWindowPresenter&) = delete;
    SlintMainWindowPresenter& operator=(const SlintMainWindowPresenter&) = delete;

    // file operations
    void on_open_xyce_file(const std::filesystem::path& path) override;
    void on_save_netlist() override;

    // view switching
    void on_show_netlist() override;
    void on_show_charts() override;
    void on_show_simulation_output() override;
    void on_close_simulation_output() override;

    // simulation control
    void on_run_simulation() override;
    void on_cancel_simulation() override;
    void on_configure_simulation() override;

    // plugin configuration
    void on_configure_plugin() override;
    void on_plugin_config_dialog_result(const PluginConfig& config) override;

    // simulation configuration
    void on_simulation_parameters_dialog_result(const SimulationConfig& config) override;

    // FFT configuration, delivered by the view after the FFT dialog closes
    void on_fft_dialog_result(std::vector<AnyExpression*> selected_expressions, const fft::FftParameters& parameters) override;

    // plot tabs
    void on_select_plot_tab(int index) override;
    void on_close_plot_tab(int index) override;

    // charts context menu
    void on_chart_calculate_fft(size_t chart_index) override;
    void on_chart_step_tool(size_t chart_index) override;
    void on_chart_new_window(size_t chart_index) override;

    // chart reordering through the drag handle; the presenter mediates so a
    // future change can persist the chart order
    void on_chart_moved(size_t from, size_t to) override;

    // load an already-parsed raw file into this window and switch to the charts
    // view; used to seed windows spawned through App::new_window
    void load_raw_file(std::shared_ptr<XyceOutputFile> raw_file);

    // simulation lifecycle events (forwarded by the view from the runner)
    void on_simulation_finished(int exit_code, bool was_canceled) override;
    void on_simulation_stdout(const std::string& line) override;
    void on_simulation_stderr(const std::string& line) override;

    // editor events
    void on_netlist_editor_modified() override;

    // kiCad schematic integration
    void on_extract_schematic_netlist() override;

    // accessors
    [[nodiscard]] const std::optional<std::shared_ptr<XyceOutputFile>>& raw_file() const;
    [[nodiscard]] const std::vector<std::shared_ptr<XyceOutputFile>>& fft_files() const;
    [[nodiscard]] const std::optional<std::shared_ptr<XyceOutputFile>>& touchstone_file() const;
    [[nodiscard]] size_t active_dataset_index() const { return m_active_dataset_index; }

private:
    // dataset representing an output file visualized in a tab
    struct PlotDataset
    {
        int id = 0;
        std::shared_ptr<XyceOutputFile> file;
        bool closable = false;
        // smith datasets build smith-kind charts plotting the gamma plane
        bool smith = false;
    };

    // synchronize plot tab list and active index with the view
    void sync_plot_tabs_with_view();

    // activate the dataset at the given index and update charts in the view
    void activate_plot_dataset(size_t index);

    // launch the simulation with the configured analysis and the stored parse
    // result; used by on_run_simulation and by the pending dialog result
    void launch_simulation();

    void show_raw_file_view();

    void set_base_title(const std::string& title);

    bool set_netlist_editor_dirty(bool flag);

    bool update_netlist_editor_content(const std::string& content, bool dirty_flag);

    // recompute and forward the action enablement to the view
    void refresh_action_states();

    // copy the produced raw output file to the user-indicated location when the
    // analysis print carries a file; the application maps the produced file
    // (which is never rewritten by a later run) while the copy keeps the
    // user-visible file up to date
    void copy_raw_output_to_destination(const std::filesystem::path& raw_path);

    MainWindowViewDef& m_view;

    std::shared_ptr<KiCadSession> m_kicad_session;

    std::unique_ptr<NetlistSource> m_netlist_source;
    bool m_netlist_editor_dirty = false;
    bool m_netlist_has_content = false;

    std::vector<PlotDataset> m_plot_datasets;
    size_t m_active_dataset_index = 0;
    int m_next_dataset_id = 1;

    std::optional<std::shared_ptr<XyceOutputFile>> m_xyce_raw_file;
    std::vector<std::shared_ptr<XyceOutputFile>> m_fft_files;
    std::vector<std::shared_ptr<XyceOutputFile>> m_prn_files;
    // touchstone output file produced by a .LIN run; a single run produces at
    // most one file (a .STEP run concatenates all steps into the same file)
    std::optional<std::shared_ptr<XyceOutputFile>> m_touchstone_file;

    SimulationConfig m_simulation_config;
    PluginConfig m_plugin_config;

    std::string m_base_title;

    // parse result of the netlist shown in the editor, kept while a modal
    // simulation parameters dialog is open so the accepted configuration can
    // rebuild the netlist (configure flow) or launch the simulation (run flow)
    std::string m_pending_sanitized_netlist;
    NetlistTopology m_pending_topology;
    std::string m_pending_original_netlist;

    // simulation run state; the view owns the platform runner, the presenter
    // keeps the paths it needs once the run finishes
    bool m_simulation_running = false;
    bool m_run_pending = false;
    std::filesystem::path m_simulation_working_directory;
    std::filesystem::path m_simulation_netlist_path;
};

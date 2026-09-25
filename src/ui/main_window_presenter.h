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

// business/orchestration logic for the slint main window, decoupled from the ui through MainWindowViewDef and MainWindowViewDefEvents; the presenter implements the event handler interface and receives user-interaction callbacks from the view without the view knowing the presenter exists
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

    // chart reordering through the drag handle; the presenter mediates so a future change can persist the chart order
    void on_chart_moved(size_t from, size_t to) override;

    // load the analysis output into this window and switch to the charts view; used to seed windows spawned through App::new_window
    void load_analysis_measurements(std::shared_ptr<XyceOutputFile> file);

    // simulation lifecycle events (forwarded by the view from the runner)
    void on_simulation_finished(int exit_code, bool was_canceled) override;
    void on_simulation_stdout(const std::string& line) override;
    void on_simulation_stderr(const std::string& line) override;

    // editor events
    void on_netlist_editor_modified() override;

    // kiCad schematic integration
    void on_extract_schematic_netlist() override;

    // accessors
    [[nodiscard]] const std::optional<std::shared_ptr<XyceOutputFile>>& analysis_measurements() const;
    [[nodiscard]] const std::vector<std::shared_ptr<XyceOutputFile>>& fft_measurements() const;
    [[nodiscard]] const std::optional<std::shared_ptr<XyceOutputFile>>& pce_measurements() const;
    [[nodiscard]] const std::optional<std::shared_ptr<XyceOutputFile>>& s_parameter_measurements() const;
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

    // file backing the active plot dataset, nullptr when no dataset is active
    [[nodiscard]] XyceOutputFile* active_dataset_file() const;

    // activate the dataset at the given index and update charts in the view
    void activate_plot_dataset(size_t index);

    // launch the simulation with the configured analysis and the stored parse result; used by on_run_simulation and by the pending dialog result
    void launch_simulation();

    void show_simulation_output_view();

    void set_base_title(const std::string& title);

    bool set_netlist_editor_dirty(bool flag);

    bool update_netlist_editor_content(const std::string& content, bool dirty_flag);

    // recompute and forward the action enablement to the view
    void refresh_action_states();

    // copy the produced default output file of a recorded .PRINT FILE= destination, the application maps the produced file (which is never rewritten by a later run) while the copy keeps the user-visible file up to date
    void copy_simulation_output_to_destination(const PrintOutputCopy& copy);

    // resolve, parse and prepare the analysis print output file for the configured format; the returned instance is ready for rendering: the tab plot type follows the configured analysis print, not the produced file, so the label is identical regardless of the .PRINT format
    [[nodiscard]] std::optional<std::shared_ptr<XyceOutputFile>> resolve_analysis_output(const std::filesystem::path& netlist_path);

    // apply the analysis print metadata (tab plot type) to the analysis output; derived from the configured analysis print type, not from the produced file, so the tab label is identical regardless of the .PRINT format; the title keeps the circuit name provided by the parsers
    void apply_analysis_print_metadata(XyceOutputFile& file) const;

    // load the s-parameter file a .LIN run wrote and render its tabs (the LIN Analysis tab and the Smith Chart tab); the analysis output's step information is carried over when present so .STEP runs map into per-step slices
    void load_s_parameter_measurements(const StepInformation* analysis_steps);

    // load the FFT calculation files a .TRAN run with .FFT directives wrote and render one tab per file; the FFT data maps onto the analysis output's step slices
    void load_fft_measurements();

    // load the PCE statistics file a DC or transient run with .PCE parameters and a .PCE companion print wrote and render its tab; pce_print carries the configured print so the parser matches the output format
    void load_pce_measurements(const PrintParameters& pce_print);

    MainWindowViewDef& m_view;

    std::shared_ptr<KiCadSession> m_kicad_session;

    std::unique_ptr<NetlistSource> m_netlist_source;
    bool m_netlist_editor_dirty = false;
    bool m_netlist_has_content = false;

    std::vector<PlotDataset> m_plot_datasets;
    size_t m_active_dataset_index = 0;
    int m_next_dataset_id = 1;

    // analysis output of the last run or the loaded output file in this window; it is NOT the active tab, the active dataset file is resolved through active_dataset_file()
    std::optional<std::shared_ptr<XyceOutputFile>> m_analysis_measurements;
    std::vector<std::shared_ptr<XyceOutputFile>> m_fft_measurements;
    // pce measurements produced by a DC or transient run; a single run produces at most one .PCE companion output file
    std::optional<std::shared_ptr<XyceOutputFile>> m_pce_measurements;
    // s-parameter measurements produced by a .LIN run; a single run produces at most one file (a .STEP run concatenates all steps into the same file)
    std::optional<std::shared_ptr<XyceOutputFile>> m_s_parameter_measurements;

    SimulationConfig m_simulation_config;
    PluginConfig m_plugin_config;

    std::string m_base_title;

    // parse result of the netlist shown in the editor, kept while a modal simulation parameters dialog is open so the accepted configuration can rebuild the netlist (configure flow) or launch the simulation (run flow)
    std::string m_pending_sanitized_netlist;
    NetlistTopology m_pending_topology;
    std::string m_pending_original_netlist;

    // simulation run state; the view owns the platform runner, the presenter keeps the paths and the recorded .PRINT FILE= copies it needs once the run finishes
    bool m_simulation_running = false;
    bool m_run_pending = false;
    std::filesystem::path m_simulation_working_directory;
    std::filesystem::path m_simulation_netlist_path;
    std::vector<PrintOutputCopy> m_simulation_output_copies;
};

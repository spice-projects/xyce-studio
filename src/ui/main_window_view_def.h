#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../config/plugin_config.h"
#include "../core/step_information.h"
#include "../dsp/fft.h"
#include "../expression/expression.h"
#include "../expression/expression_manager.h"
#include "../io/xyce_output_file.h"
#include "../simulation/simulation_config.h"
#include "main_window_state.h"

struct PlotTabItem
{
    int id = 0;
    std::string title;
    bool closable = false;
};

// abstract view interface for the main window, so the presenter can be tested without a ui
class MainWindowView
{
public:
    virtual ~MainWindowView() = default;

    // window chrome
    virtual void set_title(const std::string& title) = 0;
    virtual void set_status_text(const std::string& text) = 0;
    virtual void apply_action_enablement(const ActionStateEnablement& enablement) = 0;

    // simulation run state, drives the Run/Stop toolbar toggle
    virtual void set_simulation_running(bool running) = 0;

    // content views (netlist editor vs charts, mutually exclusive)
    virtual void show_netlist_view() = 0;
    virtual void show_charts_view() = 0;
    virtual void set_netlist_editor_content(const std::string& content) = 0;
    [[nodiscard]] virtual std::string netlist_editor_content() const = 0;
    virtual void set_netlist_editor_read_only(bool read_only) = 0;
    [[nodiscard]] virtual bool charts_shown() const = 0;

    // simulation output panel / log
    virtual void show_simulation_output_panel() = 0;
    virtual void hide_simulation_output_panel() = 0;
    virtual void clear_simulation_output() = 0;
    virtual void append_simulation_output_line(const std::string& line) = 0;
    [[nodiscard]] virtual bool simulation_output_panel_hidden() const = 0;
    [[nodiscard]] virtual bool simulation_output_has_content() const = 0;

    // charts; datasets are identified by the plot tab id so the renderer keeps
    // an independent chart state per tab (zoom, plots, step selection) and a
    // tab switch restores the previous state instead of rebuilding
    virtual void update_charts(int dataset_id, ExpressionManager& expression_manager, const StepInformation& step_information, AbscissaScale abscissa_scale, const std::vector<std::vector<std::string>>& suggested_plots) = 0;

    // drop the chart state of the dataset with the given tab id
    virtual void release_charts(int dataset_id) = 0;

    // drop the chart state of every dataset
    virtual void release_all_charts() = 0;

    // plot tabs
    virtual void set_plot_tabs(const std::vector<PlotTabItem>& tabs, int active_index) = 0;
    virtual void set_active_plot_tab(int active_index) = 0;

    // move the chart at the given index to the given index in the vertical
    // stack; the presenter mediates so a future change can persist the order
    virtual void move_chart(size_t from, size_t to) = 0;

    // show the FFT setup dialog for the chart at the given index; the accepted
    // result is delivered asynchronously through on_fft_dialog_result
    virtual void show_fft_dialog(size_t chart_index) = 0;

    // show the step tool dialog for the chart at the given index; the accepted
    // step selection is applied back to the chart through the renderer
    virtual void show_step_tool_dialog(size_t chart_index) = 0;

    // modal dialogs (the view's job, they need a parent window)
    [[nodiscard]] virtual std::optional<SimulationConfig> show_simulation_parameters_dialog(const SimulationConfig& current) = 0;
    [[nodiscard]] virtual std::optional<PluginConfig> show_plugin_config_dialog(const PluginConfig& current) = 0;

    // simulation process lifecycle (presenter decides when, the view wires the process events)
    virtual void start_simulation_process(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) = 0;

    // cancel the running simulation process owned by the view
    virtual void cancel_simulation_process() = 0;

    // window management
    virtual void spawn_raw_file_window(std::shared_ptr<XyceOutputFile> raw_file) = 0;
};

// event handler interface: the view calls back into these methods when the user
// interacts with the UI, without knowing who handles them (the presenter
// implements this interface). this is the other half of the MVP contract.
class MainWindowViewDefEvents
{
public:
    virtual ~MainWindowViewDefEvents() = default;

    // file operations
    virtual void on_open_xyce_file(const std::filesystem::path& path) = 0;
    virtual void on_save_netlist() = 0;

    // view switching
    virtual void on_show_netlist() = 0;
    virtual void on_show_charts() = 0;
    virtual void on_show_simulation_output() = 0;
    virtual void on_close_simulation_output() = 0;

    // simulation control
    virtual void on_run_simulation() = 0;
    virtual void on_cancel_simulation() = 0;
    virtual void on_configure_simulation() = 0;

    // plugin configuration
    virtual void on_configure_plugin() = 0;

    // accepted plugin configuration delivered by the view after the config
    // dialog closes; the slint dialog is non-modal, so the view reports the
    // result asynchronously instead of through show_plugin_config_dialog()
    virtual void on_plugin_config_dialog_result(const PluginConfig& config) = 0;

    // accepted simulation configuration delivered by the view after the
    // simulation parameters dialog closes; the slint dialog is non-modal, so
    // the view reports the result asynchronously instead of through
    // show_simulation_parameters_dialog()
    virtual void on_simulation_parameters_dialog_result(const SimulationConfig& config) = 0;

    // accepted FFT setup delivered by the view after the FFT dialog closes: the
    // selected real (time-domain filtered) expressions and the FFT parameters;
    // the slint dialog is non-modal, so the view reports the result
    // asynchronously instead of through show_fft_dialog()
    virtual void on_fft_dialog_result(std::vector<AnyExpression*> selected_expressions, const fft::FftParameters& parameters) = 0;

    // plot tabs
    virtual void on_select_plot_tab(int index) = 0;
    virtual void on_close_plot_tab(int index) = 0;

    // charts context menu
    virtual void on_chart_calculate_fft(size_t chart_index) = 0;
    virtual void on_chart_step_tool(size_t chart_index) = 0;
    virtual void on_chart_new_window(size_t chart_index) = 0;

    // chart reordering through the drag handle; the presenter mediates so a
    // future change can persist the chart order
    virtual void on_chart_moved(size_t from, size_t to) = 0;

    // simulation lifecycle events (forwarded by the view from the runner)
    virtual void on_simulation_finished(int exit_code, bool was_canceled) = 0;
    virtual void on_simulation_stdout(const std::string& line) = 0;
    virtual void on_simulation_stderr(const std::string& line) = 0;

    // editor events
    virtual void on_netlist_editor_modified() = 0;

    // kiCad schematic integration
    virtual void on_extract_schematic_netlist() = 0;
};

// refactored view interface that adds event handler wiring on top of
// MainWindowView. the view no longer owns or knows about the presenter;
// it simply forwards user interactions through the event handler interface.
// the parent creates both the view and the presenter and wires them together.
class MainWindowViewDef : public MainWindowView
{
public:
    // set the event handler that receives user-interaction callbacks
    virtual void set_event_handler(MainWindowViewDefEvents& handler) = 0;
};

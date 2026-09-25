#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <main_window.h>

#include <slint.h>

#include "../config/plugin_config.h"
#include "../netlist/netlist_lexer.h"
#include "../netlist/netlist_source.h"
#include "add_plot_dialog_view.h"
#include "charts_renderer.h"
#include "fft_dialog_view.h"
#include "main_window_view_def.h"
#include "plugin_config_dialog_view.h"
#include "simulation_parameters_dialog_view.h"
#include "simulation_runner.h"
#include "step_tool_dialog_view.h"

class SlintMainWindowView : public MainWindowViewDef
{
public:
    SlintMainWindowView(std::unique_ptr<NetlistSource> netlist_source, PluginConfig plugin_config);

    ~SlintMainWindowView() override = default;

    SlintMainWindowView(const SlintMainWindowView&) = delete;
    SlintMainWindowView& operator=(const SlintMainWindowView&) = delete;

    void set_event_handler(MainWindowViewDefEvents& handler) override;

    // access the underlying slint window for event handling
    slint::Window& window();

    // show the main window
    void show();

    // main window view interface
    void set_title(const std::string& title) override;
    void set_status_text(const std::string& text) override;
    void apply_action_enablement(const ActionStateEnablement& enablement) override;
    void set_simulation_running(bool running) override;
    void show_netlist_view() override;
    void show_charts_view() override;
    void set_netlist_editor_content(const std::string& content) override;
    std::string netlist_editor_content() const override;
    void set_netlist_editor_read_only(bool read_only) override;
    bool charts_shown() const override;
    void show_simulation_output_panel() override;
    void hide_simulation_output_panel() override;
    void clear_simulation_output() override;
    void append_simulation_output_line(const std::string& line) override;
    bool simulation_output_panel_hidden() const override;
    bool simulation_output_has_content() const override;
    void update_charts(int dataset_id, ExpressionManager& expression_manager, const StepInformation& step_information, AbscissaScale abscissa_scale, const std::vector<std::vector<std::string>>& suggested_plots, bool smith) override;
    void release_charts(int dataset_id) override;
    void release_all_charts() override;
    void set_plot_tabs(const std::vector<PlotTabItem>& tabs, int active_index) override;
    void set_active_plot_tab(int active_index) override;

    // move the chart at the given index to the given index in the vertical
    // stack; the presenter mediates so a future change can persist the order
    void move_chart(size_t from, size_t to) override;
    void show_fft_dialog(size_t chart_index) override;
    void show_step_tool_dialog(size_t chart_index) override;
    std::optional<SimulationConfig> show_simulation_parameters_dialog(const SimulationConfig& current) override;
    std::optional<PluginConfig> show_plugin_config_dialog(const PluginConfig& current) override;
    std::optional<std::filesystem::path> request_netlist_save_path() override;
    void start_simulation_process(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) override;
    void cancel_simulation_process() override;
    void spawn_raw_file_window(std::shared_ptr<XyceOutputFile> raw_file) override;

    // release the gpu context before the view is intentionally leaked at exit
    void release_gpu_resources();

private:
    // open file action (file dialog)
    void handle_open();

    // create the charts renderer the first time the charts panel is shown
    void ensure_charts_renderer();

    // copy the buffered log lines [start..end] to the platform clipboard
    void copy_simulation_selection(int start, int end);

    // re-tokenise the current editor text and update the highlight model of
    // the NetlistEditor; used on netlist loads, on user edits and on theme
    // changes so colours always match the active palette.  unchanged rows keep
    // their token models; only rows whose tokens (or the theme palette)
    // changed are rebuilt
    void rebuild_netlist_highlight_model();

    void show_add_remove_plots_dialog(float chart_position);

    // modal dialog lifecycle: dialogs are mutually exclusive (only the main
    // window opens them), so a single flag gates all of them
    bool begin_modal_dialog();

    void end_modal_dialog();

    // invoke the given callback only when no modal dialog is open; rejects the
    // interaction otherwise, as the caller cannot act behind a modal dialog
    template <typename Fn>
    void guard_modal(Fn&& fn) {
        // check another modal dialog is not open; if it is, reject the interaction
        if (m_modal_dialog_open)
            return;
        // invoke the callback, which may open a modal dialog and set the modal state
        std::forward<Fn>(fn)();
    }

    // track whether a modal dialog is currently open
    bool m_modal_dialog_open = false;

    std::optional<std::filesystem::path> m_primed_save_as_path;

    slint::ComponentHandle<main_window::MainWindow> m_window;
    MainWindowViewDefEvents* m_event_handler = nullptr;
    bool m_netlist_read_only = false;
    std::string m_last_status_text;

    // simulation output log lines, exposed as a model to the output panel's
    // ListView; appending a line only touches the new row (virtualized list)
    std::shared_ptr<slint::VectorModel<slint::SharedString>> m_simulation_log;

    // current theme state; used when (re-)building the highlight model
    bool m_dark_mode = false;

    // live highlight model of the netlist editor; updated in place on edits so
    // unchanged rows keep their token models instead of being reallocated
    std::shared_ptr<slint::VectorModel<main_window::HighlightedLine>> m_netlist_highlight_model;
    // tokenised snapshot of the rows currently in the highlight model; the
    // diff against a fresh tokenisation decides which rows to rebuild
    std::vector<NetlistTokenLine> m_netlist_token_cache;
    // theme state the cached highlight rows were built with; a mismatch forces
    // a full row rebuild so colours follow the active palette
    bool m_netlist_model_dark_mode = false;
    slint::Color m_netlist_model_foreground;

    // platform-neutral charts renderer
    std::unique_ptr<ChartsRenderer> m_charts_renderer;

    // simulation runner for the current run; the view owns it and forwards the
    // process output and termination through the event handler
    std::unique_ptr<SimulationRunner> m_simulation_runner;

    // add plot dialog, kept alive for the lifetime of the view
    std::unique_ptr<add_plot_dialog_view::AddPlotDialogView> m_add_plot_dialog;

    // plugin config dialog, kept alive for the lifetime of the view
    std::unique_ptr<plugin_config_dialog_view::PluginConfigDialogView> m_plugin_config_dialog;

    // simulation parameters dialog, kept alive for the lifetime of the view
    std::unique_ptr<simulation_parameters_dialog_view::SimulationParametersDialogView> m_simulation_parameters_dialog;

    // FFT setup dialog, kept alive for the lifetime of the view
    std::unique_ptr<fft_dialog_view::FftDialogView> m_fft_dialog;

    // step tool dialog, kept alive for the lifetime of the view
    std::unique_ptr<step_tool_dialog_view::StepToolDialogView> m_step_tool_dialog;
};

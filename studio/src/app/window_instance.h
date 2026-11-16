#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "albor/window/window_handle.h"
#include "app/shell_window.h"
#include "charts/chart_engine.h"
#include "config/plugin_config.h"

class XyceOutputFile;

namespace albor
{
    class Application;
}

namespace studio
{
    class AlborAddPlotDialogView;
    class AlborFftDialogView;
    class AlborPluginConfigDialogView;
    class AlborSimulationParametersDialogView;
    class AlborStepToolDialogView;
    class ChartsPresenter;
    class MainWindowPresenter;
    class MainWindowView;
    class PluginConfigPresenter;
    class SimulationParametersPresenter;

    // one shell window: its netlist editor, its charts and the dialogs parented to it
    class WindowInstance final : public ShellWindow
    {
    public:
        // build a shell over the application and the configuration the window reads
        WindowInstance(albor::Application& application, PluginConfig& plugin_config);

        WindowInstance(const WindowInstance&) = delete;

        WindowInstance& operator=(const WindowInstance&) = delete;

        ~WindowInstance() override;

        // build, wire and show the shell, returning false when the window could not be created
        bool create();

        // the window handle the application names this shell by
        [[nodiscard]] albor::WindowHandle handle() const override;

        // show a dataset another window already parsed as a tab
        bool show_dataset(std::shared_ptr<XyceOutputFile> file, ChartKind kind, std::string title) override;

        // open a netlist through the same dispatch the open action uses
        void open_netlist(const std::filesystem::path& path) override;

        // open every output file the command line named, returning whether one loaded
        bool show_startup_datasets(const std::vector<std::filesystem::path>& paths) override;

        // install the sink the chart new window command reaches
        void set_on_new_window(std::function<void(std::shared_ptr<XyceOutputFile>, ChartKind, std::string)> handler) override;

        // route one closed window to the shell that owns it, returning true when it was this one
        bool on_window_closed(albor::WindowHandle closed) override;

        // report whether the shell still owns a live window
        [[nodiscard]] bool alive() const override;

        // close the window the shell owns, which the application tears down
        void request_close() override;

    private:
        // rebuild the document strip from the netlist and the open datasets
        void refresh_documents();

        // select the netlist or one dataset tab
        void select_document(int index);

        // close one dataset tab
        void close_document(int index);

        // wire the charts presenter, its dialogs and the document strip
        void wire_charts();

        // the application that owns the window
        albor::Application& m_application;

        // the configuration the window reads and the plugin dialog updates
        PluginConfig& m_plugin_config;

        // the shell window of this instance
        std::unique_ptr<MainWindowView> m_view;

        // the netlist lifecycle of this shell
        std::unique_ptr<MainWindowPresenter> m_presenter;

        // the waveform datasets this shell shows
        std::unique_ptr<ChartsPresenter> m_charts;

        // whether this shell currently replaces the editor with the charts
        bool m_charts_visible = false;

        // the add/remove plots dialog parented to this shell
        std::unique_ptr<AlborAddPlotDialogView> m_add_plot_view;

        // the step tool dialog parented to this shell
        std::unique_ptr<AlborStepToolDialogView> m_step_tool_view;

        // the FFT dialog parented to this shell
        std::unique_ptr<AlborFftDialogView> m_fft_view;

        // the plugin configuration dialog parented to this shell
        std::unique_ptr<AlborPluginConfigDialogView> m_plugin_config_view;

        // the plugin configuration validation and persistence
        std::unique_ptr<PluginConfigPresenter> m_plugin_config_presenter;

        // the simulation parameters dialog parented to this shell
        std::unique_ptr<AlborSimulationParametersDialogView> m_simulation_parameters_view;

        // the simulation parameters validation and acceptance
        std::unique_ptr<SimulationParametersPresenter> m_simulation_parameters_presenter;

        // the shell sink the chart new window command reaches
        std::function<void(std::shared_ptr<XyceOutputFile>, ChartKind, std::string)> m_on_new_window;

        // whether the shell still owns a live window
        bool m_alive = false;
    };
} // namespace studio

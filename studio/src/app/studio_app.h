#pragma once

#include <filesystem>
#include <memory>

#include "config/plugin_config.h"

namespace studio
{
    class AlborPluginConfigDialogView;
    class AlborSimulationParametersDialogView;
    class ChartsPresenter;
    class MainWindowView;
    class MainWindowPresenter;
    class PluginConfigPresenter;
    class SimulationParametersPresenter;

    // owns the shell, the dialog surfaces and the presenters, and runs the Albor event loop
    class StudioApp
    {
    public:
        explicit StudioApp(PluginConfig config);

        StudioApp(const StudioApp&) = delete;

        StudioApp& operator=(const StudioApp&) = delete;

        ~StudioApp();

        // build the windows and block until the last one closes
        int run();

        // the netlist the shell opens through the file dispatch once it is up
        void set_startup_netlist(const std::filesystem::path& path);

        // the output file the charts open once the shell is up
        void set_startup_dataset(const std::filesystem::path& path);

    private:
        // the configuration the shell starts from and keeps after a dialog accepts
        PluginConfig m_plugin_config;

        // the netlist the shell opens once it is up, empty when none was requested
        std::filesystem::path m_startup_netlist;

        // the output file the charts open once the shell is up, empty when none was requested
        std::filesystem::path m_startup_dataset;

        // the main shell window
        std::unique_ptr<MainWindowView> m_main_view;

        // the netlist lifecycle of the shell
        std::unique_ptr<MainWindowPresenter> m_main_presenter;

        // the waveform datasets and the native charts view they drive
        std::unique_ptr<ChartsPresenter> m_charts_presenter;

        // whether the waveform panel currently replaces the editor
        bool m_charts_visible = false;

        // the plugin configuration dialog surface
        std::unique_ptr<AlborPluginConfigDialogView> m_plugin_config_view;

        // the plugin configuration validation and persistence
        std::unique_ptr<PluginConfigPresenter> m_plugin_config_presenter;

        // the simulation parameters dialog surface
        std::unique_ptr<AlborSimulationParametersDialogView> m_simulation_parameters_view;

        // the simulation parameters validation and acceptance
        std::unique_ptr<SimulationParametersPresenter> m_simulation_parameters_presenter;
    };
} // namespace studio

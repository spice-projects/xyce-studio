#pragma once

#include <filesystem>
#include <memory>

#include "config/plugin_config.h"

namespace studio
{
    class AlborPluginConfigDialogView;
    class MainWindowView;
    class MainWindowPresenter;
    class PluginConfigPresenter;

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

    private:
        // the configuration the shell starts from and keeps after a dialog accepts
        PluginConfig m_plugin_config;

        // the netlist the shell opens once it is up, empty when none was requested
        std::filesystem::path m_startup_netlist;

        // the main shell window
        std::unique_ptr<MainWindowView> m_main_view;

        // the netlist lifecycle of the shell
        std::unique_ptr<MainWindowPresenter> m_main_presenter;

        // the plugin configuration dialog surface
        std::unique_ptr<AlborPluginConfigDialogView> m_plugin_config_view;

        // the plugin configuration validation and persistence
        std::unique_ptr<PluginConfigPresenter> m_plugin_config_presenter;
    };
} // namespace studio

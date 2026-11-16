#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "albor/application/lifetime_token.h"
#include "albor/window/window_handle.h"
#include "app/shell_window.h"
#include "app/window_set.h"
#include "charts/chart_engine.h"
#include "config/plugin_config.h"

class XyceOutputFile;

namespace albor
{
    class Application;
}

namespace studio
{
    // owns the shell windows and their presenters, and runs the Albor event loop
    class StudioApp
    {
    public:
        // the factory the application builds a shell with, which a test substitutes
        using ShellFactory = std::function<std::unique_ptr<ShellWindow>(PluginConfig&, albor::Application&)>;

        StudioApp(PluginConfig config, albor::Application& application, ShellFactory factory);

        StudioApp(const StudioApp&) = delete;

        StudioApp& operator=(const StudioApp&) = delete;

        ~StudioApp();

        // build the windows and block until the last one closes
        int run();

        // the netlist the shell opens through the file dispatch once it is up
        void set_startup_netlist(const std::filesystem::path& path);

        // add an output file the charts open once the shell is up
        void set_startup_dataset(const std::filesystem::path& path);

    private:
        // build and show one shell through the factory, returning null when it could not be created
        ShellWindow* build_window();

        // open a dataset in a window of its own
        void open_window(std::shared_ptr<XyceOutputFile> file, ChartKind kind, std::string title);

        // route one closed window to the shell that owns it
        void on_window_closed(albor::WindowHandle closed);

        // the guard a deferred task checks before it touches the app
        albor::LifetimeTokenSource m_lifetime;

        // the application that owns the native loop and the window registry
        albor::Application& m_application;

        // the factory every shell is built with
        ShellFactory m_factory;

        // the configuration every window reads and the plugin dialog updates
        PluginConfig m_plugin_config;

        // the netlist the shell opens once it is up, empty when none was requested
        std::filesystem::path m_startup_netlist;

        // the output files the charts open once the shell is up, empty when none was requested
        std::vector<std::filesystem::path> m_startup_datasets;

        // the shell windows the application keeps alive
        WindowSet m_windows;
    };
} // namespace studio

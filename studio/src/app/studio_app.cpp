#include "app/studio_app.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "albor/application/application.h"
#include "app/schematic_session.h"

studio::StudioApp::StudioApp(PluginConfig config, albor::Application& application, ShellFactory factory, std::shared_ptr<SchematicSession> schematic) :
    m_application(application), m_factory(std::move(factory)), m_schematic(std::move(schematic)), m_plugin_config(std::move(config)), m_windows([this](std::function<void()> task) {
        // the release waits for the close dispatch to return, and never outlives the app
        (void)m_application.post(std::move(task), m_lifetime.token());
    }) {}

studio::StudioApp::~StudioApp() {
    // the handler captures this app, so it must not outlive it
    m_application.set_closed_handler({});
}

void studio::StudioApp::set_startup_netlist(const std::filesystem::path& path) {
    // the shell opens the requested netlist once it is up
    m_startup_netlist = path;
}

void studio::StudioApp::set_startup_dataset(const std::filesystem::path& path) {
    // the charts open every requested output file once they are wired
    m_startup_datasets.push_back(path);
}

int studio::StudioApp::run() {
    // the plugin session supplies the schematic source the primary shell reads
    std::unique_ptr<NetlistSource> schematic_source = m_schematic != nullptr ? m_schematic->take_netlist_source() : nullptr;
    // whether the primary shell reads a schematic, which owns the document instead of the command line
    const bool schematic = schematic_source != nullptr;
    // the first window is the shell the command line seeds
    ShellWindow* primary = build_window(std::move(schematic_source));
    // a shell that could not be created reports the failure
    if (primary == nullptr) {
        std::fprintf(stderr, "xyce-studio: the main window could not be created\n");
        return 1;
    }
    // the schematic owns the document in plugin mode, so the command line cannot seed another one
    if (!schematic) {
        // the requested netlist reaches the shell through the same dispatch the open action uses
        if (!m_startup_netlist.empty())
            primary->open_netlist(m_startup_netlist);
        // the datasets the command line named open as tabs
        primary->show_startup_datasets(m_startup_datasets);
    }
    // every window reports its close before the shell is released
    m_application.set_closed_handler([this](const albor::WindowHandle handle) { on_window_closed(handle); });
    // block until the last window closes
    return m_application.run();
}

studio::ShellWindow* studio::StudioApp::build_window(std::unique_ptr<NetlistSource> netlist_source) {
    // build the shell through the factory the application installed
    std::unique_ptr<ShellWindow> window = m_factory(m_plugin_config, m_application, std::move(netlist_source));
    // a refused shell leaves no window to own
    if (window == nullptr)
        return nullptr;
    // a window spawns the windows its charts ask for
    window->set_on_new_window([this](std::shared_ptr<XyceOutputFile> file, const ChartKind kind, std::string title) { open_window(std::move(file), kind, std::move(title)); });
    // the window outlives this call
    m_windows.add(std::move(window));
    // hand back the window the caller may seed
    return m_windows.at(m_windows.size() - 1);
}

void studio::StudioApp::open_window(std::shared_ptr<XyceOutputFile> file, const ChartKind kind, std::string title) {
    // a request without a dataset opens nothing
    if (file == nullptr)
        return;
    // the shell is built before the dataset reaches it
    ShellWindow* window = build_window();
    // a refused shell leaves the dataset to the window that asked for it
    if (window == nullptr) {
        spdlog::error("Studio: the requested window could not be created");
        return;
    }
    // the shell shows the dataset it was asked to open
    if (!window->show_dataset(std::move(file), kind, std::move(title))) {
        spdlog::error("Studio: the requested dataset could not be shown in its own window");
        // an empty shell the dataset never reached leaves the screen
        window->request_close();
    }
}

void studio::StudioApp::on_window_closed(const albor::WindowHandle closed) {
    // the shell that owns the handle is released after this dispatch
    m_windows.route_close(closed);
}

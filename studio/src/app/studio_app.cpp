#include "app/studio_app.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "albor/application/application.h"
#include "albor/text/text_engine.h"
#include "albor_embedded_fonts.h"
#include "presenters/main_window_presenter.h"
#include "presenters/plugin_config_presenter.h"
#include "views/main_window/main_window_view.h"
#include "views/plugin_config/plugin_config_dialog_view.h"

studio::StudioApp::StudioApp(PluginConfig config) :
    m_plugin_config(std::move(config)) {}

studio::StudioApp::~StudioApp() {
    // the handler captures this app, so it must not outlive it
    albor::Application& application = albor::Application::instance();
    application.set_closed_handler({});
}

void studio::StudioApp::set_startup_netlist(const std::filesystem::path& path) {
    // the shell opens the requested netlist once it is up
    m_startup_netlist = path;
}

int studio::StudioApp::run() {
    // name the embedded family before the first window shapes any text
    albor::set_installed_font_family(std::string(albor::generated::DEFAULT_FAMILY));
    // the application owns the native loop and the window registry
    albor::Application& application = albor::Application::instance();
    // build and show the shell before the dialog is parented to it
    m_main_view = std::make_unique<MainWindowView>(application);
    if (!m_main_view->create()) {
        std::fprintf(stderr, "xyce-studio: the main window could not be created\n");
        return 1;
    }
    // the shell reports the netlist lifecycle to the presenter
    m_main_presenter = std::make_unique<MainWindowPresenter>(*m_main_view);
    m_main_view->set_event_handler(m_main_presenter.get());
    // the requested netlist reaches the shell through the same dispatch the open action uses
    if (!m_startup_netlist.empty())
        m_main_presenter->on_open_file(m_startup_netlist);
    // the dialog is parented to the shell and outlives individual shows
    m_plugin_config_view = std::make_unique<AlborPluginConfigDialogView>(application, m_main_view->handle());
    m_plugin_config_presenter = std::make_unique<PluginConfigPresenter>();
    // the toolbar action opens the dialog with the current configuration
    m_main_view->set_on_plugin_settings([this] {
        // a dialog that is already open owns the interaction
        if (m_plugin_config_view->is_open())
            return;
        // validate and persist through the presenter, then keep the accepted value
        m_plugin_config_presenter->show(*m_plugin_config_view, m_plugin_config, [this](const PluginConfig& saved) { m_plugin_config = saved; });
    });
    // a closed window drops the borrowed facade the shell held
    application.set_closed_handler([this](albor::WindowHandle handle) {
        // the dialog clears its presenter before the shell clears its facade
        m_plugin_config_view->on_window_closed(handle);
        m_main_view->on_window_closed(handle);
    });
    // block until the last window closes
    return application.run();
}

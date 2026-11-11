#include "app/studio_app.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/text/text_engine.h"
#include "albor_embedded_fonts.h"
#include "presenters/charts_presenter.h"
#include "presenters/main_window_presenter.h"
#include "presenters/plugin_config_presenter.h"
#include "presenters/simulation_parameters_presenter.h"
#include "views/charts/charts_view.h"
#include "views/main_window/main_window_view.h"
#include "views/plugin_config/plugin_config_dialog_view.h"
#include "views/simulation_parameters/simulation_parameters_dialog_view.h"

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

void studio::StudioApp::set_startup_dataset(const std::filesystem::path& path) {
    // the charts open the requested output file once they are wired
    m_startup_dataset = path;
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
    // the shell reports the netlist lifecycle to the presenter, which reads the Xyce path
    m_main_presenter = std::make_unique<MainWindowPresenter>(*m_main_view, m_plugin_config);
    m_main_view->set_event_handler(m_main_presenter.get());
    // the requested netlist reaches the shell through the same dispatch the open action uses
    if (!m_startup_netlist.empty())
        m_main_presenter->on_open_file(m_startup_netlist);
    // the shell owns the native charts view the charts presenter drives
    if (albor::WindowStage* stage = application.stage(m_main_view->handle()); stage != nullptr && m_main_view->charts_view() != nullptr) {
        // build the presenter over the view and the window text engine
        m_charts_presenter = std::make_unique<ChartsPresenter>(*m_main_view->charts_view(), stage->text_engine());
        // match the chart palette to the scheme the window installed
        m_charts_presenter->set_dark_mode(m_main_view->is_dark());
        // a live scheme flip repaints the charts
        m_main_view->set_on_scheme_changed([this](const bool dark) {
            if (m_charts_presenter != nullptr)
                m_charts_presenter->set_dark_mode(dark);
        });
        // render the hover readout in the status bar
        m_charts_presenter->set_on_hover([this](const std::string& text) { m_main_view->set_status_text(text.empty() ? "Ready" : text); });
        // the toolbar action toggles the body between the editor and the charts
        m_main_view->set_on_show_charts([this] {
            m_charts_visible = !m_charts_visible;
            m_main_view->set_charts_visible(m_charts_visible);
        });
        // an output file the open action resolves loads into the charts
        m_main_presenter->set_dataset_file_handler([this](const std::filesystem::path& path) {
            // a dataset the charts cannot read belongs to no other action
            if (m_charts_presenter == nullptr || !m_charts_presenter->show_file(path))
                return false;
            // reveal the waveform panel the new dataset fills
            m_charts_visible = true;
            m_main_view->set_charts_visible(true);
            // the charts action now has a dataset to show
            m_main_view->set_charts_enabled(true);
            // report the file was consumed
            return true;
        });
        // a startup dataset opens the charts over the requested file
        if (!m_startup_dataset.empty() && m_charts_presenter->show_file(m_startup_dataset)) {
            // reveal the waveform panel the startup dataset fills
            m_charts_visible = true;
            m_main_view->set_charts_visible(true);
            // the charts action now has a dataset to show
            m_main_view->set_charts_enabled(true);
        }
    }
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
    // the configure action is parented to the shell and outlives individual shows
    m_simulation_parameters_view = std::make_unique<AlborSimulationParametersDialogView>(application, m_main_view->handle());
    m_simulation_parameters_presenter = std::make_unique<SimulationParametersPresenter>();
    // the toolbar action opens the dialog over the analysis the netlist describes
    m_main_view->set_on_configure_simulation([this] {
        // a dialog that is already open owns the interaction
        if (m_simulation_parameters_view->is_open())
            return;
        // hand the parsed configuration to the dialog and keep the accepted value in the shell
        const SimulationConfig& current = m_main_presenter->prepare_simulation_parameters();
        m_simulation_parameters_presenter->show(*m_simulation_parameters_view, current, [this](const SimulationConfig& config) { m_main_presenter->on_simulation_parameters_accepted(config); });
    });
    // a closed window drops the borrowed facade the shell held
    application.set_closed_handler([this](albor::WindowHandle handle) {
        // the charts view dies with the shell, so the presenter must stop borrowing it first
        if (m_charts_presenter != nullptr && handle == m_main_view->handle()) {
            m_charts_presenter->detach();
            m_charts_presenter.reset();
        }
        // the dialog clears its presenter before the shell clears its facade
        m_plugin_config_view->on_window_closed(handle);
        m_simulation_parameters_view->on_window_closed(handle);
        m_main_view->on_window_closed(handle);
    });
    // block until the last window closes
    return application.run();
}

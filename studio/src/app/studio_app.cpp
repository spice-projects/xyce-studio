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
    // the charts open every requested output file once they are wired
    m_startup_datasets.push_back(path);
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
        // the tab set controls the charts action and the body
        m_charts_presenter->set_on_tabs_changed([this] {
            // a presenter without a dataset disables the action
            if (m_charts_presenter == nullptr)
                return;
            // the charts action only reaches a live dataset
            const bool has_dataset = m_charts_presenter->has_dataset();
            // reflect the dataset state on the toolbar
            m_main_view->set_charts_enabled(has_dataset);
            // the last closed tab returns the body to the editor
            if (!has_dataset && m_charts_visible) {
                // the waveform panel has no content to show
                m_charts_visible = false;
                // reveal the editor again
                m_main_view->set_charts_visible(false);
            }
        });
        // render the hover readout in the status bar
        m_charts_presenter->set_on_hover([this](const std::string& text) { m_main_view->set_status_text(text.empty() ? "Ready" : text); });
        // the toolbar action toggles the body between the editor and the charts
        m_main_view->set_on_show_charts([this] {
            m_charts_visible = !m_charts_visible;
            m_main_view->set_charts_visible(m_charts_visible);
        });
        // an output file the open action resolves loads into the charts
        m_main_presenter->set_dataset_file_handler([this](const std::filesystem::path& path, std::shared_ptr<TempOutputDirectory> artifacts) {
            // a dataset the charts cannot read belongs to no other action
            if (m_charts_presenter == nullptr || !m_charts_presenter->show_file(path, std::move(artifacts)))
                return false;
            // reveal the waveform panel the new dataset fills
            m_charts_visible = true;
            m_main_view->set_charts_visible(true);
            // the charts action now has a dataset to show
            m_main_view->set_charts_enabled(true);
            // report the file was consumed
            return true;
        });
        // a finished run resolves its analysis and companion files into tabs
        m_main_presenter->set_run_output_handler([this](const studio::RunOutput& output, std::shared_ptr<TempOutputDirectory> artifacts) {
            // a detached charts presenter cannot show the run results
            if (m_charts_presenter == nullptr)
                return studio::RunOutputResult{};
            // the charts resolve every produced dataset from the run configuration
            const studio::RunOutputResult result = m_charts_presenter->show_run_output(output, std::move(artifacts));
            // a shown run reveals the waveform panel and enables its action
            if (result.outcome != studio::RunOutputOutcome::None) {
                // reveal the waveform panel the run dataset fills
                m_charts_visible = true;
                m_main_view->set_charts_visible(true);
                // the charts action now has a dataset to show
                m_main_view->set_charts_enabled(true);
            }
            // report the datasets the run produced
            return result;
        });
        // a run drops the previous companions before the simulator rewrites their files
        m_main_presenter->set_run_started_handler([this] {
            // a detached charts presenter has no companion tabs to drop
            if (m_charts_presenter != nullptr)
                m_charts_presenter->discard_run_companions();
        });
        // a startup dataset opens every requested file as a tab
        bool startup_loaded = false;
        // walk the files the command line named
        for (const std::filesystem::path& dataset : m_startup_datasets) {
            // a readable dataset joins the tab set
            if (m_charts_presenter->show_file(dataset))
                startup_loaded = true;
        }
        // reveal the waveform panel when at least one startup dataset loaded
        if (startup_loaded) {
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

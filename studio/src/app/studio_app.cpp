#include "app/studio_app.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/text/text_engine.h"
#include "albor_embedded_fonts.h"
#include "presenters/charts_presenter.h"
#include "presenters/main_window_presenter.h"
#include "presenters/plugin_config_presenter.h"
#include "presenters/simulation_parameters_presenter.h"
#include "views/charts/add_plot_dialog_view.h"
#include "views/charts/chart_view.h"
#include "views/charts/fft_dialog_view.h"
#include "views/charts/step_tool_dialog_view.h"
#include "views/main_window/document_tabs.h"
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
    // a new netlist document closes the output tabs of the previous one and selects the netlist tab
    m_main_presenter->set_on_netlist_opened([this] {
        // the previous netlist's outputs no longer belong to the document
        if (m_charts_presenter != nullptr)
            m_charts_presenter->close_all_datasets();
        // the netlist tab is the first document and the body shows it
        m_charts_visible = false;
        m_main_view->set_charts_visible(false);
        // the strip lists only the netlist
        refresh_documents();
    });
    // the requested netlist reaches the shell through the same dispatch the open action uses
    if (!m_startup_netlist.empty())
        m_main_presenter->on_open_file(m_startup_netlist);
    // the shell owns the native charts view the charts presenter drives
    if (albor::WindowStage* stage = application.stage(m_main_view->handle()); stage != nullptr && m_main_view->chart_view() != nullptr) {
        // build the presenter over the view and the window text engine
        m_charts_presenter = std::make_unique<ChartsPresenter>(*m_main_view->chart_view(), stage->text_engine());
        // match the chart palette to the scheme the window installed
        m_charts_presenter->set_dark_mode(m_main_view->is_dark());
        // a live scheme flip repaints the charts
        m_main_view->set_on_scheme_changed([this](const bool dark) {
            if (m_charts_presenter != nullptr)
                m_charts_presenter->set_dark_mode(dark);
        });
        // the tab set rebuilds the document strip
        m_charts_presenter->set_on_tabs_changed([this] {
            // a detached presenter has no strip to rebuild
            if (m_charts_presenter == nullptr)
                return;
            // the last closed tab returns the body to the netlist
            if (!m_charts_presenter->has_dataset())
                m_charts_visible = false;
            // the strip follows the dataset set
            refresh_documents();
        });
        // render the hover readout in the status bar
        m_charts_presenter->set_on_hover([this](const std::string& text) { m_main_view->set_status_text(text.empty() ? "Ready" : text); });
        // the strip selects the netlist or one dataset tab
        if (DocumentTabs* tabs = m_main_view->document_tabs()) {
            // a press selects the document it names
            tabs->set_on_selected([this](const int index) { select_document(index); });
            // a close press closes the dataset it names
            tabs->set_on_closed([this](const int index) { close_document(index); });
        }
        // an output file the open action resolves loads into the charts
        m_main_presenter->set_dataset_file_handler([this](const std::filesystem::path& path, std::shared_ptr<TempOutputDirectory> artifacts) {
            // a dataset the charts cannot read belongs to no other action
            if (m_charts_presenter == nullptr)
                return false;
            // the new dataset activates while the charts are shown, so the strip resolves it once
            const bool was_visible = m_charts_visible;
            m_charts_visible = true;
            m_main_view->set_charts_visible(true);
            // a refused file restores the body it found
            if (!m_charts_presenter->show_file(path, std::move(artifacts))) {
                m_charts_visible = was_visible;
                refresh_documents();
                return false;
            }
            // the strip follows the dataset set
            refresh_documents();
            // report the file was consumed
            return true;
        });
        // a finished run resolves its analysis and companion files into tabs
        m_main_presenter->set_run_output_handler([this](const studio::RunOutput& output, std::shared_ptr<TempOutputDirectory> artifacts) {
            // a detached charts presenter cannot show the run results
            if (m_charts_presenter == nullptr)
                return studio::RunOutputResult{};
            // the run activates its dataset while the charts are shown, so the strip resolves it once
            const bool was_visible = m_charts_visible;
            m_charts_visible = true;
            m_main_view->set_charts_visible(true);
            // the charts resolve every produced dataset from the run configuration
            const studio::RunOutputResult result = m_charts_presenter->show_run_output(output, std::move(artifacts));
            // a run that produced nothing restores the body it found
            if (result.outcome == studio::RunOutputOutcome::None)
                m_charts_visible = was_visible;
            // the strip follows the dataset set
            refresh_documents();
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
        // the charts show while the requested datasets load, so the last one activates
        const bool was_visible = m_charts_visible;
        m_charts_visible = true;
        m_main_view->set_charts_visible(true);
        // walk the files the command line named
        for (const std::filesystem::path& dataset : m_startup_datasets) {
            // a readable dataset joins the tab set
            if (m_charts_presenter->show_file(dataset))
                startup_loaded = true;
        }
        // no readable startup dataset restores the netlist body
        if (!startup_loaded)
            m_charts_visible = was_visible;
        // the strip lists the netlist and every open dataset
        refresh_documents();
    }
    // the add/remove plots dialog is parented to the shell and outlives individual shows
    if (m_charts_presenter != nullptr) {
        m_add_plot_view = std::make_unique<AlborAddPlotDialogView>(application, m_main_view->handle());
        // the context menu opens the dialog over the chart it targets
        m_charts_presenter->set_on_add_remove_plots([this](const std::size_t index) {
            // a dialog that is already open owns the interaction
            if (m_add_plot_view == nullptr || m_add_plot_view->is_open() || m_charts_presenter == nullptr)
                return;
            // the chart may have been removed while the menu was open
            const std::optional<studio::PlotSelectionState> state = m_charts_presenter->plot_selection(index);
            // a chart that vanished opens nothing
            if (!state)
                return;
            // the dataset the dialog edits may change while it is open
            const int dataset_id = state->dataset_id;
            // the dialog edits the selection and hands the accepted names back to the charts
            m_add_plot_view->show(
                *state,
                [this, index, dataset_id](std::vector<std::string> names) {
                    // a shell that closed or switched datasets drops the selection
                    if (m_charts_presenter == nullptr || m_charts_presenter->active_dataset_id() != dataset_id)
                        return;
                    m_charts_presenter->apply_plot_selection(index, names);
                },
                [this, index](const std::string& expression) -> std::optional<studio::PlotExpression> {
                    // a shell that closed while the dialog ran resolves nothing
                    if (m_charts_presenter == nullptr)
                        return std::nullopt;
                    return m_charts_presenter->resolve_plot_expression(index, expression);
                });
        });
    }
    // the step tool dialog is parented to the shell and outlives individual shows
    if (m_charts_presenter != nullptr) {
        m_step_tool_view = std::make_unique<AlborStepToolDialogView>(application, m_main_view->handle());
        // the step tool command opens the dialog over the chart it targets
        m_charts_presenter->set_on_step_tool([this](const std::size_t index) {
            // a dialog that is already open owns the interaction
            if (m_step_tool_view == nullptr || m_step_tool_view->is_open() || m_charts_presenter == nullptr)
                return;
            // the chart may have been removed while the menu was open
            const std::optional<studio::StepToolState> state = m_charts_presenter->step_tool_state(index);
            // a chart that vanished or has a single step opens nothing
            if (!state)
                return;
            // the dataset the dialog edits may change while it is open
            const int dataset_id = state->dataset_id;
            // the dialog edits the selection and hands the accepted steps back to the charts
            m_step_tool_view->show(*state, [this, index, dataset_id](std::vector<std::size_t> steps) {
                // a shell that closed or switched datasets drops the selection
                if (m_charts_presenter == nullptr || m_charts_presenter->active_dataset_id() != dataset_id)
                    return;
                // the chart keeps only the accepted steps
                m_charts_presenter->apply_step_selection(index, std::set<std::size_t>(steps.begin(), steps.end()));
            });
        });
    }
    // the FFT dialog is parented to the shell and outlives individual shows
    if (m_charts_presenter != nullptr) {
        m_fft_view = std::make_unique<AlborFftDialogView>(application, m_main_view->handle());
        // the calculate FFT command opens the dialog over the chart it targets
        m_charts_presenter->set_on_calculate_fft([this](const std::size_t index) {
            // a dialog that is already open owns the interaction
            if (m_fft_view == nullptr || m_fft_view->is_open() || m_charts_presenter == nullptr)
                return;
            // the chart may have been removed while the menu was open
            const std::optional<studio::FftSelectionState> state = m_charts_presenter->fft_selection(index);
            // a chart that vanished or has no real series opens nothing
            if (!state)
                return;
            // the dataset the dialog edits may change while it is open
            const int dataset_id = state->dataset_id;
            // the dialog hands the accepted transform back to the charts
            m_fft_view->show(*state, [this, dataset_id](studio::FftRequest request) {
                // a shell that closed or switched datasets drops the transform
                if (m_charts_presenter == nullptr || m_charts_presenter->active_dataset_id() != dataset_id)
                    return;
                // the transform appends its own dataset tab
                m_charts_presenter->apply_fft(request);
            });
        });
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
            m_charts_presenter->set_on_add_remove_plots({});
            m_charts_presenter->set_on_step_tool({});
            m_charts_presenter->set_on_calculate_fft({});
            m_charts_presenter->detach();
            m_charts_presenter.reset();
        }
        // the add/remove dialog clears its state before the shell clears its facade
        if (m_add_plot_view != nullptr)
            m_add_plot_view->on_window_closed(handle);
        // the step tool clears its state the same way
        if (m_step_tool_view != nullptr)
            m_step_tool_view->on_window_closed(handle);
        // the FFT dialog clears its state the same way
        if (m_fft_view != nullptr)
            m_fft_view->on_window_closed(handle);
        // the dialog clears its presenter before the shell clears its facade
        m_plugin_config_view->on_window_closed(handle);
        m_simulation_parameters_view->on_window_closed(handle);
        m_main_view->on_window_closed(handle);
    });
    // block until the last window closes
    return application.run();
}

void studio::StudioApp::refresh_documents() {
    // a shell without the strip has nothing to list
    if (m_main_view == nullptr || m_main_view->document_tabs() == nullptr)
        return;
    // the netlist is always the first document and reserves its label
    std::vector<DocumentTabSpec> documents;
    std::set<std::string> used_titles{"Netlist"};
    documents.push_back(DocumentTabSpec{.title = "Netlist", .closable = false});
    // every open dataset follows in tab order
    if (m_charts_presenter != nullptr) {
        for (std::size_t index = 0; index < m_charts_presenter->dataset_count(); ++index) {
            // the title the dataset reports
            std::string title = m_charts_presenter->dataset_title(index);
            // an unnamed dataset still needs a label
            if (title.empty())
                title = "Dataset " + std::to_string(index + 1);
            // a title the strip already carries takes the next free suffix
            if (!used_titles.insert(title).second) {
                for (int suffix = 2;; ++suffix) {
                    std::string candidate = title + " (" + std::to_string(suffix) + ")";
                    if (used_titles.insert(candidate).second) {
                        title = candidate;
                        break;
                    }
                }
            }
            documents.push_back(DocumentTabSpec{.title = std::move(title), .closable = m_charts_presenter->dataset_closable(index)});
        }
    }
    // the active tab names the visible body
    int active = 0;
    if (m_charts_visible && m_charts_presenter != nullptr && m_charts_presenter->has_dataset())
        active = 1 + static_cast<int>(m_charts_presenter->active_dataset());
    // hand the strip the documents and the selection
    m_main_view->document_tabs()->set_documents(std::move(documents));
    m_main_view->document_tabs()->set_active(active);
    // the body follows the active document
    m_main_view->set_charts_visible(active > 0);
}

void studio::StudioApp::select_document(const int index) {
    // a shell without the chart presenter shows only the netlist
    if (m_main_view == nullptr)
        return;
    // the first tab is the netlist
    if (index <= 0 || m_charts_presenter == nullptr) {
        // a gesture the charts owned dies with the hidden canvas
        if (m_charts_presenter != nullptr)
            m_charts_presenter->cancel_interaction();
        m_charts_visible = false;
        m_main_view->set_charts_visible(false);
        refresh_documents();
        return;
    }
    // a dataset tab activates the dataset it names
    const std::size_t dataset = static_cast<std::size_t>(index - 1);
    if (dataset >= m_charts_presenter->dataset_count())
        return;
    // the body shows the charts before the activation refreshes the strip
    m_charts_visible = true;
    m_main_view->set_charts_visible(true);
    // the dataset follows the selection
    m_charts_presenter->activate_dataset(dataset);
    // the active tab follows the dataset
    refresh_documents();
}

void studio::StudioApp::close_document(const int index) {
    // the netlist tab never closes
    if (index <= 0 || m_charts_presenter == nullptr)
        return;
    // the presenter owns the dataset and republishes the remaining set
    m_charts_presenter->close_dataset(static_cast<std::size_t>(index - 1));
}

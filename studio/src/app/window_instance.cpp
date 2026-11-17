#include "app/window_instance.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/text/text_engine.h"
#include "presenters/charts_presenter.h"
#include "presenters/main_window_presenter.h"
#include "presenters/options_presenter.h"
#include "presenters/plugin_config_presenter.h"
#include "presenters/simulation_parameters_presenter.h"
#include "views/charts/add_plot_dialog_view.h"
#include "views/charts/chart_view.h"
#include "views/charts/fft_dialog_view.h"
#include "views/charts/step_tool_dialog_view.h"
#include "views/main_window/document_tabs.h"
#include "views/main_window/main_window_view.h"
#include "views/options/options_dialog_view.h"
#include "views/plugin_config/plugin_config_dialog_view.h"
#include "views/simulation_parameters/simulation_parameters_dialog_view.h"

studio::WindowInstance::WindowInstance(albor::Application& application, PluginConfig& plugin_config, std::unique_ptr<NetlistSource> netlist_source) :
    m_application(application), m_plugin_config(plugin_config), m_netlist_source(std::move(netlist_source)), m_schematic(m_netlist_source != nullptr && m_netlist_source->is_read_only()) {}

studio::WindowInstance::~WindowInstance() = default;

bool studio::WindowInstance::create() {
    // a second create would replace a live shell
    if (m_view != nullptr)
        return false;
    // build and show the shell before the dialogs are parented to it
    m_view = std::make_unique<MainWindowView>(m_application);
    // a refused window leaves the instance empty
    if (!m_view->create()) {
        m_view.reset();
        return false;
    }
    // the shell reports the netlist lifecycle to the presenter, which reads the Xyce path
    m_presenter = std::make_unique<MainWindowPresenter>(*m_view, m_plugin_config, std::move(m_netlist_source), m_schematic);
    m_view->set_event_handler(m_presenter.get());
    // a new netlist document closes the output tabs of the previous one and selects the netlist tab
    m_presenter->set_on_netlist_opened([this] {
        // the previous netlist's outputs no longer belong to the document
        if (m_charts != nullptr)
            m_charts->close_all_datasets();
        // the netlist tab is the first document and the body shows it
        m_charts_visible = false;
        m_view->set_charts_visible(false);
        // the strip lists only the netlist
        refresh_documents();
    });
    // a schematic shell seeds its editor from the exported netlist
    if (m_schematic)
        m_presenter->load_schematic();
    // the shell owns the native charts view the charts presenter drives
    wire_charts();
    // the add/remove plots dialog is parented to the shell and outlives individual shows
    if (m_charts != nullptr) {
        m_add_plot_view = std::make_unique<AlborAddPlotDialogView>(m_application, m_view->handle());
        // the context menu opens the dialog over the chart it targets
        m_charts->set_on_add_remove_plots([this](const std::size_t index) {
            // a dialog that is already open owns the interaction
            if (m_add_plot_view == nullptr || m_add_plot_view->is_open() || m_charts == nullptr)
                return;
            // the chart may have been removed while the menu was open
            const std::optional<studio::PlotSelectionState> state = m_charts->plot_selection(index);
            // a chart that vanished opens nothing
            if (!state)
                return;
            // the dataset the dialog edits may change while it is open
            const int dataset_id = state->dataset_id;
            // the charts the dialog edits may be rebuilt while it is open
            const std::uint64_t chart_revision = m_charts->chart_revision();
            // the dialog edits the selection and hands the accepted names back to the charts
            m_add_plot_view->show(
                *state,
                [this, index, dataset_id, chart_revision](std::vector<std::string> names) {
                    // a shell that closed or switched datasets drops the selection
                    if (m_charts == nullptr || m_charts->active_dataset_id() != dataset_id)
                        return;
                    // a chart rebuilt behind the dialog is no longer the chart it edited
                    if (m_charts->chart_revision() != chart_revision)
                        return;
                    m_charts->apply_plot_selection(index, names);
                },
                [this, index](const std::string& expression) -> std::optional<studio::PlotExpression> {
                    // a shell that closed while the dialog ran resolves nothing
                    if (m_charts == nullptr)
                        return std::nullopt;
                    return m_charts->resolve_plot_expression(index, expression);
                });
        });
    }
    // the step tool dialog is parented to the shell and outlives individual shows
    if (m_charts != nullptr) {
        m_step_tool_view = std::make_unique<AlborStepToolDialogView>(m_application, m_view->handle());
        // the step tool command opens the dialog over the chart it targets
        m_charts->set_on_step_tool([this](const std::size_t index) {
            // a dialog that is already open owns the interaction
            if (m_step_tool_view == nullptr || m_step_tool_view->is_open() || m_charts == nullptr)
                return;
            // the chart may have been removed while the menu was open
            const std::optional<studio::StepToolState> state = m_charts->step_tool_state(index);
            // a chart that vanished or has a single step opens nothing
            if (!state)
                return;
            // the dataset the dialog edits may change while it is open
            const int dataset_id = state->dataset_id;
            // the charts the dialog edits may be rebuilt while it is open
            const std::uint64_t chart_revision = m_charts->chart_revision();
            // the dialog edits the selection and hands the accepted steps back to the charts
            m_step_tool_view->show(*state, [this, index, dataset_id, chart_revision](std::vector<std::size_t> steps) {
                // a shell that closed or switched datasets drops the selection
                if (m_charts == nullptr || m_charts->active_dataset_id() != dataset_id)
                    return;
                // a chart rebuilt behind the dialog is no longer the chart it edited
                if (m_charts->chart_revision() != chart_revision)
                    return;
                // the chart keeps only the accepted steps
                m_charts->apply_step_selection(index, std::set<std::size_t>(steps.begin(), steps.end()));
            });
        });
    }
    // the FFT dialog is parented to the shell and outlives individual shows
    if (m_charts != nullptr) {
        m_fft_view = std::make_unique<AlborFftDialogView>(m_application, m_view->handle());
        // the calculate FFT command opens the dialog over the chart it targets
        m_charts->set_on_calculate_fft([this](const std::size_t index) {
            // a dialog that is already open owns the interaction
            if (m_fft_view == nullptr || m_fft_view->is_open() || m_charts == nullptr)
                return;
            // the chart may have been removed while the menu was open
            const std::optional<studio::FftSelectionState> state = m_charts->fft_selection(index);
            // a chart that vanished or has no real series opens nothing
            if (!state)
                return;
            // the dataset the dialog edits may change while it is open
            const int dataset_id = state->dataset_id;
            // the charts the dialog read may be rebuilt while it is open
            const std::uint64_t chart_revision = m_charts->chart_revision();
            // the dialog hands the accepted transform back to the charts
            m_fft_view->show(*state, [this, dataset_id, chart_revision](studio::FftRequest request) {
                // a shell that closed or switched datasets drops the transform
                if (m_charts == nullptr || m_charts->active_dataset_id() != dataset_id)
                    return;
                // a chart rebuilt behind the dialog no longer describes the transform it chose
                if (m_charts->chart_revision() != chart_revision)
                    return;
                // the transform appends its own dataset tab
                m_charts->apply_fft(request);
            });
        });
    }
    // a shell that built no surface leaves the datasets to the log
    else {
        // the missing surface is a shell defect rather than a refused file
        spdlog::error("Charts: the shell built no chart and Fourier result surfaces");
    }
    // the dialog is parented to the shell and outlives individual shows
    m_plugin_config_view = std::make_unique<AlborPluginConfigDialogView>(m_application, m_view->handle());
    m_plugin_config_presenter = std::make_unique<PluginConfigPresenter>();
    // the toolbar action opens the dialog with the current configuration
    m_view->set_on_plugin_settings([this] {
        // a dialog that is already open owns the interaction
        if (m_plugin_config_view->is_open())
            return;
        // validate and persist through the presenter, then keep the accepted value
        m_plugin_config_presenter->show(*m_plugin_config_view, m_plugin_config, [this](const PluginConfig& saved) { m_plugin_config = saved; });
    });
    // the configure action is parented to the shell and outlives individual shows
    m_simulation_parameters_view = std::make_unique<AlborSimulationParametersDialogView>(m_application, m_view->handle());
    m_simulation_parameters_presenter = std::make_unique<SimulationParametersPresenter>();
    // the toolbar action opens the dialog over the analysis the netlist describes
    m_view->set_on_configure_simulation([this] { open_simulation_parameters(); });
    // a run over a netlist without an analysis asks for one first
    m_presenter->set_on_run_needs_configuration([this] { open_simulation_parameters(); });
    // the options dialog is parented to the shell and outlives individual shows
    m_options_view = std::make_unique<AlborOptionsDialogView>(m_application, m_view->handle());
    m_options_presenter = std::make_unique<OptionsPresenter>();
    // the toolbar action opens the dialog over the options the netlist carries
    m_view->set_on_edit_options([this] {
        // a dialog that is already open owns the interaction
        if (m_options_view == nullptr || m_options_view->is_open() || m_presenter == nullptr)
            return;
        // seed the editor from the parsed netlist and apply the accepted options
        const OptionParameters& current = m_presenter->prepare_options();
        m_options_presenter->begin(current);
        m_options_view->show(*m_options_presenter, [this](const OptionParameters& options) { m_presenter->on_options_accepted(options); });
    });
    // the shell is wired and shown
    m_alive = true;
    return true;
}

albor::WindowHandle studio::WindowInstance::handle() const {
    // an instance without a shell names no window
    return m_view != nullptr ? m_view->handle() : albor::WindowHandle{};
}

bool studio::WindowInstance::alive() const { return m_alive; }

void studio::WindowInstance::request_close() {
    // a shell without a window has nothing to close
    if (m_view == nullptr)
        return;
    // the application owns the teardown, which drops this shell afterwards
    (void)m_application.request_close(m_view->handle());
}

void studio::WindowInstance::set_on_new_window(std::function<void(std::shared_ptr<XyceOutputFile>, ChartKind, std::string)> handler) {
    // keep the sink the chart command reaches
    m_on_new_window = std::move(handler);
    // the charts hand the dataset of the chart the menu targeted to the shell
    if (m_charts != nullptr)
        m_charts->set_on_new_window([this](std::shared_ptr<XyceOutputFile> file, const ChartKind kind, std::string title) {
            // a shell that already closed the presenter drops the request
            if (!m_alive || !m_on_new_window)
                return;
            m_on_new_window(std::move(file), kind, std::move(title));
        });
}

void studio::WindowInstance::open_netlist(const std::filesystem::path& path) {
    // a shell without a presenter has no editor to fill
    if (m_presenter == nullptr)
        return;
    m_presenter->on_open_file(path);
}

bool studio::WindowInstance::show_dataset(std::shared_ptr<XyceOutputFile> file, const ChartKind kind, std::string title) {
    // a shell without the charts has no surface for the dataset
    if (m_charts == nullptr)
        return false;
    // the dataset activates while the charts are shown, so the strip resolves it once
    const bool was_visible = m_charts_visible;
    m_charts_visible = true;
    m_view->set_charts_visible(true);
    // a dataset the charts cannot show restores the body it found
    if (!m_charts->adopt_file(std::move(file), kind, std::move(title))) {
        m_charts_visible = was_visible;
        refresh_documents();
        return false;
    }
    // the strip follows the dataset set
    refresh_documents();
    // report the dataset shown
    return true;
}

bool studio::WindowInstance::show_startup_datasets(const std::vector<std::filesystem::path>& paths) {
    // a shell without the charts refuses every dataset
    if (m_charts == nullptr)
        return false;
    // a startup dataset opens every requested file as a tab
    bool loaded = false;
    // the charts show while the requested datasets load, so the last one activates
    const bool was_visible = m_charts_visible;
    m_charts_visible = true;
    m_view->set_charts_visible(true);
    // walk the files the command line named
    for (const std::filesystem::path& dataset : paths) {
        // a readable dataset joins the tab set
        if (m_charts->show_file(dataset))
            loaded = true;
    }
    // no readable startup dataset restores the netlist body
    if (!loaded)
        m_charts_visible = was_visible;
    // the strip lists the netlist and every open dataset
    refresh_documents();
    // report whether a dataset loaded
    return loaded;
}

bool studio::WindowInstance::on_window_closed(const albor::WindowHandle closed) {
    // a closed surface belongs to no live shell
    if (!m_alive)
        return false;
    // the add/remove dialog clears its state before the shell clears its facade
    if (m_add_plot_view != nullptr)
        m_add_plot_view->on_window_closed(closed);
    // the step tool clears its state the same way
    if (m_step_tool_view != nullptr)
        m_step_tool_view->on_window_closed(closed);
    // the FFT dialog clears its state the same way
    if (m_fft_view != nullptr)
        m_fft_view->on_window_closed(closed);
    // the options dialog clears its state the same way
    if (m_options_view != nullptr)
        m_options_view->on_window_closed(closed);
    // the plugin configuration dialog clears its presenter before the shell clears its facade
    if (m_plugin_config_view != nullptr)
        m_plugin_config_view->on_window_closed(closed);
    // the simulation parameters dialog clears its presenter the same way
    if (m_simulation_parameters_view != nullptr)
        m_simulation_parameters_view->on_window_closed(closed);
    // a dialog handle leaves the shell alive
    if (!(closed == handle()))
        return false;
    // the charts view dies with the shell, so the presenter must stop borrowing it first
    if (m_charts != nullptr) {
        // the dialogs no longer reach a dying surface
        m_charts->set_on_add_remove_plots({});
        m_charts->set_on_step_tool({});
        m_charts->set_on_calculate_fft({});
        // the new window sink dies with the charts
        m_charts->set_on_new_window({});
        // the borrowed views die before the presenter does
        m_charts->detach();
        m_charts.reset();
    }
    // the runner stops before the presenter it reports to dies
    m_view->detach();
    // the netlist lifecycle borrows the shell, which is about to clear its facade
    m_presenter.reset();
    // the shell drops the borrowed facade the tree owned
    m_view->on_window_closed(closed);
    // the instance is released after the close dispatch returns
    m_alive = false;
    // report the shell the application named
    return true;
}

void studio::WindowInstance::refresh_documents() {
    // a shell without the strip has nothing to list
    if (m_view == nullptr || m_view->document_tabs() == nullptr)
        return;
    // the netlist is always the first document and reserves its label
    std::vector<DocumentTabSpec> documents;
    std::set<std::string> used_titles{"Netlist"};
    documents.push_back(DocumentTabSpec{.title = "Netlist", .closable = false});
    // every open dataset follows in tab order
    if (m_charts != nullptr) {
        for (std::size_t index = 0; index < m_charts->dataset_count(); ++index) {
            // the title the dataset reports
            std::string title = m_charts->dataset_title(index);
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
            documents.push_back(DocumentTabSpec{.title = std::move(title), .closable = m_charts->dataset_closable(index)});
        }
    }
    // the active tab names the visible body
    int active = 0;
    if (m_charts_visible && m_charts != nullptr && m_charts->has_dataset())
        active = 1 + static_cast<int>(m_charts->active_dataset());
    // hand the strip the documents and the selection
    m_view->document_tabs()->set_documents(std::move(documents));
    m_view->document_tabs()->set_active(active);
    // the body follows the active document
    m_view->set_charts_visible(active > 0);
    // the waveform body shows the Fourier results table while such a dataset is active
    m_view->set_fourier_results_visible(active > 0 && m_charts != nullptr && m_charts->fourier_active());
}

void studio::WindowInstance::select_document(const int index) {
    // a shell without the chart presenter shows only the netlist
    if (m_view == nullptr)
        return;
    // the first tab is the netlist
    if (index <= 0 || m_charts == nullptr) {
        // a gesture the charts owned dies with the hidden canvas
        if (m_charts != nullptr)
            m_charts->cancel_interaction();
        m_charts_visible = false;
        m_view->set_charts_visible(false);
        refresh_documents();
        return;
    }
    // a dataset tab activates the dataset it names
    const std::size_t dataset = static_cast<std::size_t>(index - 1);
    if (dataset >= m_charts->dataset_count())
        return;
    // the body shows the charts before the activation refreshes the strip
    m_charts_visible = true;
    m_view->set_charts_visible(true);
    // the dataset follows the selection
    m_charts->activate_dataset(dataset);
    // the active tab follows the dataset
    refresh_documents();
}

void studio::WindowInstance::close_document(const int index) {
    // the netlist tab never closes
    if (index <= 0 || m_charts == nullptr)
        return;
    // the presenter owns the dataset and republishes the remaining set
    m_charts->close_dataset(static_cast<std::size_t>(index - 1));
}

void studio::WindowInstance::open_simulation_parameters() {
    // a dialog that is already open owns the interaction
    if (m_simulation_parameters_view == nullptr || m_simulation_parameters_view->is_open() || m_presenter == nullptr)
        return;
    // hand the parsed configuration to the dialog and keep the accepted value in the shell
    const SimulationConfig& current = m_presenter->prepare_simulation_parameters();
    m_simulation_parameters_presenter->show(*m_simulation_parameters_view, current, [this](const SimulationConfig& config) { m_presenter->on_simulation_parameters_accepted(config); }, [this] { m_presenter->on_simulation_parameters_cancelled(); });
}

void studio::WindowInstance::wire_charts() {
    // the charts need the window stage, the canvas and the results table
    albor::WindowStage* stage = m_application.stage(m_view->handle());
    // a shell without its native surfaces has nothing to wire
    if (stage == nullptr || m_view->chart_view() == nullptr || m_view->fourier_results_view() == nullptr)
        return;
    // build the presenter over the canvas, the results table and the window text engine
    m_charts = std::make_unique<ChartsPresenter>(*m_view->chart_view(), *m_view->fourier_results_view(), stage->text_engine());
    // match the chart palette to the scheme the window installed
    m_charts->set_dark_mode(m_view->is_dark());
    // a live scheme flip repaints the charts
    m_view->set_on_scheme_changed([this](const bool dark) {
        // a detached presenter has no palette to flip
        if (m_charts != nullptr)
            m_charts->set_dark_mode(dark);
    });
    // the tab set rebuilds the document strip
    m_charts->set_on_tabs_changed([this] {
        // a detached presenter has no strip to rebuild
        if (m_charts == nullptr)
            return;
        // the last closed tab returns the body to the netlist
        if (!m_charts->has_dataset())
            m_charts_visible = false;
        // the strip follows the dataset set
        refresh_documents();
    });
    // render the hover readout in the status bar
    m_charts->set_on_hover([this](const std::string& text) { m_view->set_status_text(text.empty() ? "Ready" : text); });
    // the strip selects the netlist or one dataset tab
    if (DocumentTabs* tabs = m_view->document_tabs()) {
        // a press selects the document it names
        tabs->set_on_selected([this](const int index) { select_document(index); });
        // a close press closes the dataset it names
        tabs->set_on_closed([this](const int index) { close_document(index); });
    }
    // an output file the open action resolves loads into the charts
    m_presenter->set_dataset_file_handler([this](const std::filesystem::path& path, std::shared_ptr<TempOutputDirectory> artifacts) {
        // a dataset the charts cannot read belongs to no other action
        if (m_charts == nullptr)
            return false;
        // the new dataset activates while the charts are shown, so the strip resolves it once
        const bool was_visible = m_charts_visible;
        m_charts_visible = true;
        m_view->set_charts_visible(true);
        // a refused file restores the body it found
        if (!m_charts->show_file(path, std::move(artifacts))) {
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
    m_presenter->set_run_output_handler([this](const studio::RunOutput& output, std::shared_ptr<TempOutputDirectory> artifacts) {
        // a detached charts presenter cannot show the run results
        if (m_charts == nullptr)
            return studio::RunOutputResult{};
        // the run activates its dataset while the charts are shown, so the strip resolves it once
        const bool was_visible = m_charts_visible;
        m_charts_visible = true;
        m_view->set_charts_visible(true);
        // the charts resolve every produced dataset from the run configuration
        const studio::RunOutputResult result = m_charts->show_run_output(output, std::move(artifacts));
        // a run that produced nothing restores the body it found
        if (result.outcome == studio::RunOutputOutcome::None)
            m_charts_visible = was_visible;
        // the strip follows the dataset set
        refresh_documents();
        // report the datasets the run produced
        return result;
    });
    // a run drops the previous companions before the simulator rewrites their files
    m_presenter->set_run_started_handler([this] {
        // a detached charts presenter has no companion tabs to drop
        if (m_charts != nullptr)
            m_charts->discard_run_companions();
    });
}

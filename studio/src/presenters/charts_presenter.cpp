#include "presenters/charts_presenter.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/util.h"
#include "io/temp_output_directory.h"
#include "io/touchstone_file.h"
#include "io/xyce_csd_file.h"
#include "io/xyce_csv_file.h"
#include "io/xyce_fft_file.h"
#include "io/xyce_four_file.h"
#include "io/xyce_output_file.h"
#include "io/xyce_prn_file.h"
#include "io/xyce_raw_file.h"
#include "io/xyce_tecplot_file.h"
#include "simulation/print_parameters.h"
#include "simulation/simulation_config.h"
#include "views/charts/chart_view.h"
#include "views/charts/fourier_results_view.h"

namespace
{
    // the touchstone extensions the charts dispatch reads as smith datasets
    bool is_touchstone_extension(const std::string& extension) { return extension == ".ts" || (extension.size() == 4 && extension[1] == 's' && extension[3] == 'p'); }

    // the file names a finished run writes besides its primary analysis output
    bool is_companion_name(const std::string& name) {
        // the lower case malformed markers
        return name.find(".pce.") != std::string::npos || name.find(".fft") != std::string::npos || name.find(".four") != std::string::npos || name.find(".sens") != std::string::npos || name.find(".tradj") != std::string::npos || name.find(".hb_") != std::string::npos || name.find(".startup") != std::string::npos;
    }

    // the extensions a primary analysis output can carry, touchstone data is loaded by its companion loader
    bool is_primary_extension(const std::string& extension) { return extension == ".raw" || extension == ".prn" || extension == ".csd" || extension == ".csv" || extension == ".dat"; }

    // the dataset plot type the configured analysis print names, which a produced file name cannot carry
    PlotType plot_type_for_print(const std::string& print_type) {
        // the normalized analysis type
        const std::string type = to_upper(print_type);
        // a transient print writes a time abscissa
        if (type == "TRAN")
            return PlotType::TRANSIENT;
        // ac prints and their variants write a frequency abscissa
        if (type.rfind("AC", 0) == 0)
            return PlotType::AC;
        // a dc sweep keeps its swept abscissa
        if (type == "DC")
            return PlotType::DC;
        // a noise analysis writes a frequency abscissa
        if (type == "NOISE")
            return PlotType::NOISE;
        // an operating point carries no abscissa
        if (type == "OP")
            return PlotType::DC_OPERATING_POINT;
        // an unnamed analysis keeps the type the parser derived
        return PlotType::UNKNOWN;
    }

    // whether the dataset abscissa is time, which is the axis the FFT reads
    bool is_time_domain(XyceOutputFile& file) { return file.expression_manager().abscissa().unit() == "s"; }

    // parse a print output file with the parser its configured format selects
    std::shared_ptr<XyceOutputFile> parse_print_file(const std::filesystem::path& path, const PrintParameters& print) {
        // the normalized format
        const std::string format = to_upper(print.print_format);
        // csv files carry the delimiter separated table
        if (format == "CSV") {
            // parse the table
            const auto parsed = xyce_csv_file_parser(path);
            // hand out the parsed file, or nothing when the table is unreadable
            return parsed ? *parsed : nullptr;
        }
        // tecplot files carry the dat table
        if (format == "TECPLOT") {
            // parse the table
            const auto parsed = xyce_tecplot_file_parser(path);
            // hand out the parsed file, or nothing when the table is unreadable
            return parsed ? *parsed : nullptr;
        }
        // probe files carry the complex columns
        if (format == "PROBE") {
            // parse the probe file
            const auto parsed = xyce_csd_file_parser(path);
            // hand out the parsed file, or nothing when the table is unreadable
            return parsed ? *parsed : nullptr;
        }
        // raw files carry the binary plot blocks
        if (format == "RAW") {
            // parse the raw file
            const auto parsed = xyce_raw_file_parser(path);
            // hand out the parsed file, or nothing when the table is unreadable
            return parsed ? *parsed : nullptr;
        }
        // every other format including the unspecified default writes the prn table
        const auto parsed = xyce_prn_file_parser(path);
        // hand out the parsed file, or nothing when the table is unreadable
        return parsed ? *parsed : nullptr;
    }

    // the name an expression carries across both value types
    std::string expression_name(const AnyExpression& expression) {
        return std::visit([](const auto& value) { return value.name(); }, expression);
    }

    // the unit an expression carries across both value types
    std::string expression_unit(const AnyExpression& expression) {
        return std::visit([](const auto& value) { return value.unit(); }, expression);
    }

    // whether the fixed gamma plane can plot the expression, which needs a diagonal network parameter
    bool is_smith_expression(const AnyExpression& expression) {
        // the diagonal entry is named S11, Y11, Z11, and so on
        const std::string name = expression_name(expression);
        // only a complex reflection coefficient has a gamma plane position
        return name.size() >= 3 && std::isupper(static_cast<unsigned char>(name[0])) != 0 && name[1] == name[2] && std::holds_alternative<Expression<std::complex<double>>>(expression);
    }

    // whether a chart already plots the expression, including the derived traces a complex line chart draws
    bool is_expression_plotted(const Series& series, const AnyExpression& expression) {
        // a chart keys every series by its own name, so a complex source is selected through its derived rows
        return series.contains(expression_name(expression));
    }
    // the window name the FFT dataset title carries
    std::string fft_window_label(const fft::WindowFunction window) {
        // every window names itself in the title
        switch (window) {
        case fft::WindowFunction::RECTANGULAR:
            return "Rectangular";
        case fft::WindowFunction::HAMMING:
            return "Hamming";
        case fft::WindowFunction::HANNING:
            return "Hanning";
        case fft::WindowFunction::BLACKMAN:
            return "Blackman";
        }
        // an unknown window reads as the default
        return "Hanning";
    }
} // namespace

studio::ChartsPresenter::ChartsPresenter(ChartView& view, FourierResultsView& results, albor::TextEngine& text) :
    m_text(text), m_view(&view), m_results(&results) {
    // the view clears the borrowed pointer before the window tree frees it
    m_view->set_on_destroyed([this] { m_view = nullptr; });
    // the table clears the borrowed pointer before the window tree frees it
    m_results->set_on_destroyed([this] { m_results = nullptr; });
    // a changed arranged extent refreshes the chart geometry
    m_view->set_on_resize_settled([this](const float width, const float height) { set_viewport(width, height); });
    // install the zoom callbacks
    m_view->set_on_zoom_drag_started([this](const float x, const float y) {
        // the active dataset owns the drag
        DatasetState* state = active_state();
        // a press inside a plot starts the selection overlay
        if (state != nullptr && state->controller->zoom_drag_started(x, y))
            publish_zoom_selection();
    });
    // install the moving corner callback
    m_view->set_on_zoom_drag_moved([this](const float x, const float y) {
        // the active dataset owns the drag
        DatasetState* state = active_state();
        // a move updates the selection overlay
        if (state != nullptr && state->controller->zoom_drag_moved(x, y))
            publish_zoom_selection();
    });
    // install the release callback
    m_view->set_on_zoom_drag_ended([this] {
        // the active dataset owns the drag
        DatasetState* state = active_state();
        // nothing to finish without a dataset
        if (state == nullptr)
            return;
        // apply the completed window before the overlay clears
        const bool changed = state->controller->zoom_drag_ended();
        // drop the transient selection
        m_view->set_zoom_selection(std::nullopt);
        // republish the charts when the window changed
        if (changed)
            publish_scene();
    });
    // install the cancel callback
    m_view->set_on_zoom_drag_canceled([this] {
        // the active dataset owns the drag
        DatasetState* state = active_state();
        // a dataset clears its selection without changing the charts
        if (state != nullptr)
            state->controller->zoom_drag_canceled();
        // remove the overlay from the view
        m_view->set_zoom_selection(std::nullopt);
    });
    // install the hover callback
    m_view->set_on_hover_moved([this](const float x, const float y) {
        // the active dataset owns the readout
        DatasetState* state = active_state();
        // publish the readout the pointer resolves
        if (state != nullptr && m_on_hover)
            m_on_hover(state->controller->hover_moved(x, y));
    });
    // install the hover leave callback
    m_view->set_on_hover_ended([this] {
        // the active dataset owns the readout
        DatasetState* state = active_state();
        // clear the controller state and the shell readout
        if (state != nullptr)
            state->controller->hover_ended();
        // restore the status bar text
        if (m_on_hover)
            m_on_hover({});
    });
    // install the chart command sink
    m_view->set_on_chart_command([this](const ChartCommand command, const std::size_t index) { run_chart_command(command, index); });
    // the drag handle reports the completed reorder
    m_view->set_on_chart_reordered([this](const int from, const int to) {
        // the active dataset owns the stack
        DatasetState* state = active_state();
        // nothing to move without a dataset
        if (state == nullptr)
            return;
        // a negative index names no chart
        if (from < 0 || to < 0)
            return;
        // move the chart and republish the stack in its new order
        state->controller->move_chart(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
        // the frames follow the reordered engines
        publish_scene();
    });
    // paint the palette the shell starts with
    m_view->set_palette(chart_palette(m_dark_mode));
}

studio::ChartsPresenter::~ChartsPresenter() { detach(); }

void studio::ChartsPresenter::detach() {
    // the table keeps no borrowed callback once the presenter leaves
    if (m_results != nullptr)
        m_results->set_on_destroyed({});
    // the table is no longer reachable from the presenter
    m_results = nullptr;
    // the window teardown may already have dropped the view
    if (m_view == nullptr)
        return;
    // clear the callbacks the presenter installed
    m_view->set_on_destroyed({});
    m_view->set_on_resize_settled({});
    m_view->set_on_zoom_drag_started({});
    m_view->set_on_zoom_drag_moved({});
    m_view->set_on_zoom_drag_ended({});
    m_view->set_on_zoom_drag_canceled({});
    m_view->set_on_hover_moved({});
    m_view->set_on_hover_ended({});
    m_view->set_on_chart_command({});
    m_view->set_on_chart_reordered({});
    // the view is no longer reachable from the presenter
    m_view = nullptr;
}

std::optional<std::pair<std::shared_ptr<XyceOutputFile>, ChartKind>> studio::ChartsPresenter::parse_file(const std::filesystem::path& path) const {
    // the extension decides which output parser reads the file
    const std::string extension = to_lower(path.extension().string());
    // the parsed dataset under construction
    std::optional<std::shared_ptr<XyceOutputFile>> parsed;
    // a raw binary plot file carries the analysis blocks
    if (extension == ".raw")
        parsed = xyce_raw_file_parser(path);
    // a print output file carries the column table
    else if (extension == ".prn")
        parsed = xyce_prn_file_parser(path);
    // a probe output file carries the complex columns
    else if (extension == ".csd")
        parsed = xyce_csd_file_parser(path);
    // a csv output file carries the delimiter separated table
    else if (extension == ".csv")
        parsed = xyce_csv_file_parser(path);
    // a tecplot output file carries the dat table
    else if (extension == ".dat")
        parsed = xyce_tecplot_file_parser(path);
    // a touchstone file carries s parameter data, which plots on the smith plane
    else if (is_touchstone_extension(extension))
        parsed = touchstone_file_parser(path);
    // a Fourier analysis file carries the harmonic spectra the results table presents
    else if (extension.rfind(".four", 0) == 0) {
        // the path names one file, which the pattern matches literally
        if (const auto files = xyce_four_file_parser(path); files && !files->empty())
            parsed = files->front();
    }
    // a failed or unsupported parse leaves the shell untouched
    if (!parsed || !*parsed) {
        // report the file the charts refused
        spdlog::warn("Charts: unsupported or unreadable output file {}", path.string());
        // report the failure to the caller
        return std::nullopt;
    }
    // touchstone data plots on the smith plane, every other format plots cartesian
    return std::make_pair(std::move(*parsed), is_touchstone_extension(extension) ? ChartKind::SMITH : ChartKind::XY);
}

bool studio::ChartsPresenter::show_file(const std::filesystem::path& path, std::shared_ptr<TempOutputDirectory> artifacts) {
    // a detached presenter has no surface to show the dataset on
    if (m_view == nullptr)
        return false;
    // a run lease that does not own the file would drop it while the chart still maps it
    if (artifacts != nullptr && !artifacts->owns(path)) {
        // report the rejected lease
        spdlog::warn("Charts: run directory does not own output file {}", path.string());
        // report the failure to the caller
        return false;
    }
    // parse the file into a dataset
    std::optional<std::pair<std::shared_ptr<XyceOutputFile>, ChartKind>> parsed = parse_file(path);
    // a failed parse leaves the tab set untouched
    if (!parsed)
        return false;
    // a run-owned file keeps the temporary run directory alive with it
    if (artifacts != nullptr)
        parsed->first->set_temp_output_directory(artifacts);
    // a user file carries a close affordance
    append_dataset(std::move(parsed->first), parsed->second, DatasetRole::User, {});
    // report the dataset loaded
    return true;
}

std::optional<std::filesystem::path> studio::ChartsPresenter::resolve_primary(const RunOutput& output) const {
    // the analysis print names the primary file exactly, whatever else the run produced
    if (const std::optional<PrintParameters> print = output.config.analysis_print_parameters()) {
        // the default file the run wrote for the stripped print
        const std::filesystem::path path = default_print_output_file(*print, output.netlist_path);
        // a missing file falls through to the companion and fallback resolution
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
            return path;
    }
    // no configured output survived, so fall back to a deterministic scan
    return fallback_primary(output.netlist_path.parent_path());
}

std::optional<std::filesystem::path> studio::ChartsPresenter::fallback_primary(const std::filesystem::path& directory, const std::filesystem::path* exclude) const {
    // the supported candidates the scan found
    std::vector<std::filesystem::path> candidates;
    // a missing directory has no output
    std::error_code error;
    // walk the run directory
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error)) {
        // only a regular file can be an output
        if (!entry.is_regular_file(error))
            continue;
        // the lower case file name the markers and extension are read from
        const std::string name = to_lower(entry.path().filename().string());
        // a companion file is loaded by its own loader, not as the primary analysis
        if (is_companion_name(name))
            continue;
        // the caller already refused this file, so the scan looks for another one
        if (exclude != nullptr && entry.path() == *exclude)
            continue;
        // the extension decides whether a parser can read the file
        if (!is_primary_extension(to_lower(entry.path().extension().string())))
            continue;
        // keep the candidate for the deterministic sort
        candidates.push_back(entry.path());
    }
    // a run with no supported output reports nothing
    if (candidates.empty())
        return std::nullopt;
    // sort by file name so directory iteration order cannot change the selection
    std::sort(candidates.begin(), candidates.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) { return lhs.filename() < rhs.filename(); });
    // hand out the first candidate
    return candidates.front();
}

studio::RunOutputResult studio::ChartsPresenter::show_run_output(const RunOutput& output, std::shared_ptr<TempOutputDirectory> artifacts) {
    // a detached presenter has no surface to show the datasets on
    if (m_view == nullptr)
        return {};
    // the companion datasets the run produced, in tab order
    std::vector<std::pair<std::shared_ptr<XyceOutputFile>, ChartKind>> companions;
    // the primary dataset the run produced
    std::shared_ptr<XyceOutputFile> primary;
    // the primary chart kind
    ChartKind primary_kind = ChartKind::XY;
    // the configured analysis print file, which decides whether its user copy is safe to replace
    const std::optional<PrintParameters> analysis_print = output.config.analysis_print_parameters();
    // the default file the configured analysis print produces
    const std::optional<std::filesystem::path> configured_analysis_path = analysis_print ? std::optional<std::filesystem::path>(default_print_output_file(*analysis_print, output.netlist_path)) : std::nullopt;
    // whether the configured analysis file itself was read
    bool analysis_loaded = false;
    // resolve and parse the primary analysis output
    if (const std::optional<std::filesystem::path> primary_path = resolve_primary(output)) {
        // parse the primary with the parser its extension selects
        std::optional<std::pair<std::shared_ptr<XyceOutputFile>, ChartKind>> parsed = parse_file(*primary_path);
        // the configured file itself was read successfully
        if (parsed && configured_analysis_path && *primary_path == *configured_analysis_path)
            analysis_loaded = true;
        // an unreadable configured file falls back to another supported output
        if (!parsed) {
            // scan the run directory for a readable alternative that is not the refused file
            if (const std::optional<std::filesystem::path> fallback = fallback_primary(output.netlist_path.parent_path(), &*primary_path); fallback)
                parsed = parse_file(*fallback);
        }
        // a readable primary replaces the previous run's primary
        if (parsed) {
            // keep the parsed file and its chart kind
            primary = std::move(parsed->first);
            primary_kind = parsed->second;
            // the configured print names the analysis, which the produced file name cannot carry
            const PlotType printed_type = analysis_print ? plot_type_for_print(analysis_print->print_type) : PlotType::UNKNOWN;
            // a print type the mapping does not know keeps the type the parser derived
            if (printed_type != PlotType::UNKNOWN)
                primary->set_plot_type(printed_type);
            // the netlist's .PLOT groups declare the charts the primary tab builds
            if (!output.suggested_plots.empty())
                primary->set_suggested_plots(output.suggested_plots);
        }
    }
    // the companion data the configured analysis dumps
    const SimulationConfig::ProducedMeasurements produced = output.config.produced_measurements();
    // a .LIN analysis with a touchstone format dumps one s-parameter file
    std::shared_ptr<XyceOutputFile> s_parameters;
    if (produced.s_parameters) {
        // resolve the configured touchstone path
        if (const std::optional<std::filesystem::path> configured = output.config.s_parameter_output_file_path(output.netlist_path, output.working_directory, output.num_ports)) {
            // Xyce appends the port suffix when the configured name is the netlist itself
            std::filesystem::path suffixed = *configured;
            suffixed += ".s" + std::to_string(std::max(1, output.num_ports)) + "p";
            // probe both candidates
            for (const std::filesystem::path& candidate : {*configured, suffixed}) {
                // a missing candidate has nothing to parse
                std::error_code error;
                if (!std::filesystem::is_regular_file(candidate, error))
                    continue;
                // map the analysis steps onto the touchstone slices when a primary exists
                const auto parsed = touchstone_file_parser(candidate, primary != nullptr ? &primary->step_information() : nullptr);
                // an unreadable candidate falls through to the next name
                if (!parsed || !*parsed)
                    continue;
                // keep the readable touchstone file
                s_parameters = *parsed;
                // stop at the first readable candidate
                break;
            }
        }
    }
    // a .TRAN analysis with .FFT directives dumps the FFT calculation files
    if (produced.fft) {
        // resolve the FFT file pattern
        if (const std::optional<std::filesystem::path> pattern = output.config.fft_output_file_path_pattern(output.netlist_path)) {
            // parse the files against the analysis output when the primary exists
            if (const auto files = xyce_fft_file_parser(*pattern, primary != nullptr ? &primary->step_information() : nullptr, primary != nullptr ? &primary->expression_manager() : nullptr)) {
                // every produced file becomes one companion tab
                for (const std::shared_ptr<XyceOutputFile>& file : *files)
                    companions.emplace_back(file, ChartKind::XY);
            }
        }
    }
    // a .TRAN analysis with .FOUR directives dumps the Fourier analysis files
    if (produced.fourier) {
        // resolve the Fourier file pattern
        if (const std::optional<std::filesystem::path> pattern = output.config.four_output_file_path_pattern(output.netlist_path)) {
            // parse the files against the analysis output when the primary exists
            if (const auto files = xyce_four_file_parser(*pattern, primary != nullptr ? &primary->step_information() : nullptr, primary != nullptr ? &primary->expression_manager() : nullptr)) {
                // every produced file becomes one companion tab
                for (const std::shared_ptr<XyceOutputFile>& file : *files)
                    companions.emplace_back(file, ChartKind::XY);
            }
        }
    }
    // a .DC or .TRAN analysis with .PCE parameters dumps the PCE statistics output
    if (produced.pce) {
        // resolve the produced PCE file and the print that formats it
        const std::optional<std::filesystem::path> pce_path = output.config.pce_output_file_path(output.netlist_path);
        const std::optional<PrintParameters> pce_print = output.config.pce_print_parameters();
        // parse the statistics output when both the path and print exist
        if (pce_path && pce_print) {
            // parse the file with the configured print format
            if (std::shared_ptr<XyceOutputFile> parsed = parse_print_file(*pce_path, *pce_print))
                companions.emplace_back(std::move(parsed), ChartKind::XY);
        }
    }
    // a touchstone-only run uses the line tab as its primary so a rerun keeps its state
    const bool s_parameters_is_primary = primary == nullptr && s_parameters != nullptr && (artifacts == nullptr || artifacts->owns(s_parameters->filename()));
    // the touchstone line tab carries the primary role
    if (s_parameters_is_primary) {
        // the line tab reads the produced touchstone file
        primary = s_parameters;
        // the primary is the rectangular line chart
        primary_kind = ChartKind::XY;
    }
    // a successful run with no readable output drops the stale run tabs and reports the miss
    if (primary == nullptr && s_parameters == nullptr && companions.empty()) {
        // cancel the outgoing interaction before the tabs move
        cancel_interaction();
        // the previous run's datasets no longer describe the current results
        remove_role(DatasetRole::Companion);
        // the previous primary is stale as well
        remove_role(DatasetRole::Primary);
        // tell the shell the tab set changed
        notify_tabs_changed();
        // republish the remaining, user-owned tabs
        publish_scene();
        // report that no dataset was shown
        return RunOutputResult{.outcome = RunOutputOutcome::None, .analysis_loaded = false};
    }
    // cancel the outgoing interaction before the tabs move
    cancel_interaction();
    // the previous run's companions are replaced by this run's
    remove_role(DatasetRole::Companion);
    // the primary dataset keeps its chart state and position across a rerun
    if (primary != nullptr) {
        // attach the run lease when the file belongs to the private directory
        if (artifacts != nullptr && artifacts->owns(primary->filename()))
            primary->set_temp_output_directory(artifacts);
        // adopt the primary in place
        adopt_primary(std::move(primary), primary_kind, output.title);
    }
    // the previous primary is stale when this run produced none
    else {
        // drop the old primary
        remove_role(DatasetRole::Primary);
    }
    // the first companion tab of this run, after any stale primary was removed
    const std::size_t first_companion = m_datasets.size();
    // a touchstone companion offers its rectangular plots and the smith plane
    if (s_parameters != nullptr) {
        // attach the run lease when the file belongs to the private directory
        if (artifacts != nullptr && artifacts->owns(s_parameters->filename()))
            s_parameters->set_temp_output_directory(artifacts);
        // the rectangular plots use the suggested chart groups unless the primary already owns the line tab
        if (!s_parameters_is_primary)
            append_dataset(s_parameters, ChartKind::XY, DatasetRole::Companion, s_parameters->title());
        // the smith chart plots the diagonal entries on the gamma plane
        append_dataset(std::move(s_parameters), ChartKind::SMITH, DatasetRole::Companion, "Smith Chart");
    }
    // attach the lease and append every other companion
    for (std::pair<std::shared_ptr<XyceOutputFile>, ChartKind>& companion : companions) {
        // attach the run lease when the file belongs to the private directory
        if (artifacts != nullptr && artifacts->owns(companion.first->filename()))
            companion.first->set_temp_output_directory(artifacts);
        // append the companion as a non-closable tab
        append_dataset(std::move(companion.first), companion.second, DatasetRole::Companion, {});
    }
    // select the primary when this run produced one, otherwise the first companion
    const auto primary_it = std::find_if(m_datasets.begin(), m_datasets.end(), [](const std::unique_ptr<DatasetState>& dataset) { return dataset->role == DatasetRole::Primary; });
    // the primary or the first companion of this run becomes active
    if (primary_it != m_datasets.end())
        m_active_dataset = static_cast<std::size_t>(std::distance(m_datasets.begin(), primary_it));
    else if (first_companion < m_datasets.size())
        m_active_dataset = first_companion;
    // tell the shell the tab set and selection changed
    notify_tabs_changed();
    // publish the selected scene
    publish_scene();
    // report which kind of run dataset was shown and whether the analysis file itself was read
    return RunOutputResult{.outcome = primary_it != m_datasets.end() ? RunOutputOutcome::Primary : RunOutputOutcome::CompanionOnly, .analysis_loaded = analysis_loaded};
}

void studio::ChartsPresenter::discard_run_companions() {
    // no dataset has no companion to drop
    if (m_datasets.empty())
        return;
    // cancel the interaction the outgoing companion owns
    cancel_interaction();
    // drop the previous run's companions
    remove_role(DatasetRole::Companion);
    // tell the shell the tab set changed
    notify_tabs_changed();
    // republish the remaining tabs
    publish_scene();
}

studio::ChartsPresenter::DatasetState& studio::ChartsPresenter::append_dataset(std::shared_ptr<XyceOutputFile> file, const ChartKind kind, const DatasetRole role, std::string title) {
    // the outgoing tab loses the pointer state before the new one becomes active
    if (!m_datasets.empty())
        cancel_interaction();
    // build the dataset state before it joins the tab list
    auto state = std::make_unique<DatasetState>();
    // the identity is stable for the lifetime of the tab
    state->id = m_next_dataset_id++;
    // remember the role the tab plays
    state->role = role;
    // remember the chart kind the engines are built for
    state->kind = kind;
    // the file owns the expressions and the step information
    state->file = std::move(file);
    // the controller shares the label cache and starts at the content size
    state->controller = std::make_unique<ChartsController>(m_view_width, m_view_height, [this](const std::string& label) { return m_text.measure(label); });
    // point the controller at the file expressions
    state->controller->set_dataset(state->file->expression_manager(), state->file->step_information(), state->file->abscissa_scale());
    // the requested title wins over the parser title, which falls back to the file name
    if (title.empty())
        title = state->file->title();
    // a parser that carries no title still needs a tab label
    if (title.empty())
        title = state->file->filename().stem().string();
    // a file with no usable stem still needs a tab label
    if (title.empty())
        title = "Dataset " + std::to_string(state->id);
    // remember the chart groups the dataset was built with
    state->plot_groups = state->file->suggested_plots();
    // keep the tab labels unique so duplicate files stay distinguishable
    state->title = unique_title(title);
    // only a user file carries a close affordance
    state->closable = role == DatasetRole::User;
    // a Fourier analysis output is presented as its results table, which needs no chart
    state->fourier = state->file->plot_type() == PlotType::FOURIER;
    // the table lists the blocks the file carries
    if (state->fourier) {
        // the blocks replace the ones a previous dataset carried
        state->fourier_tables = fourier::build_tables(*state->file);
        // the revision tells the table the content changed
        state->fourier_revision++;
    }
    // build the suggested or default charts when the dataset plots
    else
        build_charts(*state->controller, *state->file, kind);
    // the new tab becomes active
    m_active_dataset = m_datasets.size();
    // keep the state alive in the tab list
    DatasetState& appended = *state;
    // append the dataset after its controller is complete
    m_datasets.push_back(std::move(state));
    // tell the shell the tab set changed
    notify_tabs_changed();
    // publish the active scene
    publish_scene();
    // hand the caller the appended record
    return appended;
}

void studio::ChartsPresenter::adopt_primary(std::shared_ptr<XyceOutputFile> file, const ChartKind kind, const std::string& title) {
    // find the primary dataset of the previous run
    const auto existing = std::find_if(m_datasets.begin(), m_datasets.end(), [](const std::unique_ptr<DatasetState>& dataset) { return dataset->role == DatasetRole::Primary; });
    // a first run appends the primary after the user tabs
    if (existing == m_datasets.end()) {
        // keep the requested title, which names the user netlist instead of the private copy
        append_dataset(std::move(file), kind, DatasetRole::Primary, title);
        // the primary is installed
        return;
    }
    // remember the primary record before the file changes
    DatasetState& state = **existing;
    // a changed chart kind cannot reuse the previous engines
    const bool kind_changed = state.kind != kind;
    // a changed .PLOT group set rebuilds the charts the netlist declares
    const bool plots_changed = state.plot_groups != file->suggested_plots();
    // repoint the engines while the previous file still owns the expressions they name
    state.controller->update_dataset(file->expression_manager(), file->step_information(), file->abscissa_scale());
    // remember the chart kind the new file needs
    state.kind = kind;
    // a chart without a resolved expression cannot describe the new file
    bool all_resolved = state.controller->chart_count() > 0;
    // check every engine for a resolved series, which zoom windows do not filter
    for (std::size_t index = 0; index < state.controller->chart_count(); ++index)
        all_resolved = all_resolved && state.controller->chart(index) != nullptr && !state.controller->chart(index)->series().empty();
    // replace the file only after the engines no longer reference the previous one
    state.file = std::move(file);
    // remember the chart groups the new file carries
    state.plot_groups = state.file->suggested_plots();
    // a Fourier analysis output is presented as its results table
    state.fourier = state.file->plot_type() == PlotType::FOURIER;
    // the table lists the blocks the file carries
    state.fourier_tables = state.fourier ? fourier::build_tables(*state.file) : std::vector<fourier::SignalTable>{};
    // the revision tells the table the content changed
    state.fourier_revision++;
    // a Fourier analysis output keeps no chart the previous primary carried
    if (state.fourier) {
        // drop the previous engines
        state.controller->clear_charts();
    }
    // rebuild only when the preserved charts cannot describe the new file
    else if (kind_changed || plots_changed || !all_resolved) {
        // drop the previous engines
        state.controller->clear_charts();
        // build the charts the new file suggests
        build_charts(*state.controller, *state.file, kind);
    }
    // keep the tab label stable across a rerun of the same netlist
    if (!title.empty())
        state.title = unique_title(title, &state);
    // the primary keeps its tab position
    m_active_dataset = static_cast<std::size_t>(std::distance(m_datasets.begin(), existing));
    // tell the shell the tab set changed
    notify_tabs_changed();
    // publish the repointed scene
    publish_scene();
}

void studio::ChartsPresenter::remove_role(const DatasetRole role) {
    // the dataset the user is looking at, which an erase before it would shift
    const int active_id = active_dataset_id();
    // drop every dataset the role names
    std::erase_if(m_datasets, [role](const std::unique_ptr<DatasetState>& dataset) { return dataset->role == role; });
    // an empty tab set returns to the first index
    if (m_datasets.empty()) {
        // reset the selection
        m_active_dataset = 0;
        // the tab set is settled
        return;
    }
    // the tab the user was looking at when it survived the erase
    const auto surviving = std::find_if(m_datasets.begin(), m_datasets.end(), [active_id](const std::unique_ptr<DatasetState>& dataset) { return dataset->id == active_id; });
    // a surviving tab keeps the selection on the dataset the user was reading
    if (surviving != m_datasets.end())
        m_active_dataset = static_cast<std::size_t>(std::distance(m_datasets.begin(), surviving));
    // a removed active tab hands the selection to its neighbour
    else
        m_active_dataset = std::min(m_active_dataset, m_datasets.size() - 1);
}

std::string studio::ChartsPresenter::unique_title(const std::string& base, const DatasetState* exclude) const {
    // the first tab keeps the plain title
    std::string candidate = base;
    // another open tab carrying the candidate claims it
    const auto taken = [&candidate, exclude](const std::unique_ptr<DatasetState>& dataset) { return dataset.get() != exclude && dataset->title == candidate; };
    // the strip reserves its first label for the netlist, so a dataset cannot take it either
    for (int suffix = 2; candidate == "Netlist" || std::any_of(m_datasets.begin(), m_datasets.end(), taken); ++suffix)
        candidate = base + " (" + std::to_string(suffix) + ")";
    // hand out the unique title
    return candidate;
}

void studio::ChartsPresenter::cancel_interaction() {
    // the outgoing dataset owns the pointer state
    DatasetState* state = active_state();
    // drop a drag and readout the outgoing tab recorded
    if (state != nullptr) {
        // cancel the selection without changing the charts
        state->controller->zoom_drag_canceled();
        // clear the readout the status bar shows
        state->controller->hover_ended();
    }
    // drop the view side drag and selection
    if (m_view != nullptr) {
        // a drag capture ends with the tab switch
        m_view->cancel_drag();
        // the overlay belongs to the outgoing dataset
        m_view->set_zoom_selection(std::nullopt);
    }
    // restore the status bar text
    if (m_on_hover)
        m_on_hover({});
}

void studio::ChartsPresenter::build_charts(ChartsController& controller, XyceOutputFile& file, const ChartKind kind) {
    // read the manager the dataset expressions resolve through
    ExpressionManager& manager = file.expression_manager();
    // a smith chart plots the diagonal complex reflection entries, not the suggested db() magnitudes
    if (kind == ChartKind::SMITH) {
        // create the single smith chart the dataset describes
        ChartEngine* chart = controller.add_chart(kind);
        // a refused chart leaves the panel empty
        if (chart == nullptr)
            return;
        // the diagonal s parameter entries the smith plane plots
        std::set<AnyExpression*> expressions;
        // scan the expression names in their stored order
        for (const std::string& name : manager.expression_names()) {
            // a diagonal entry is named S11, Y11, Z11, and so on
            if (name.size() < 3 || !std::isupper(static_cast<unsigned char>(name[0])) || name[1] != name[2])
                continue;
            // resolve the entry by name
            AnyExpression* expression = manager.evaluate(name);
            // only a complex reflection coefficient belongs on the smith plane
            if (expression != nullptr && std::holds_alternative<Expression<std::complex<double>>>(*expression))
                expressions.insert(expression);
        }
        // a dataset without diagonal entries falls back to every complex network parameter
        if (expressions.empty()) {
            // scan the expression names again
            for (const std::string& name : manager.expression_names()) {
                // only a network parameter belongs on the smith plane
                if (name.size() < 3 || !std::isupper(static_cast<unsigned char>(name[0])))
                    continue;
                // resolve the entry by name
                AnyExpression* expression = manager.evaluate(name);
                // only a complex reflection coefficient belongs on the smith plane
                if (expression != nullptr && std::holds_alternative<Expression<std::complex<double>>>(*expression))
                    expressions.insert(expression);
            }
        }
        // plot the diagonal entries on the single chart
        controller.plot_expressions(0, expressions);
        // the smith chart is complete
        return;
    }
    // a dataset with suggested plots builds one chart per group
    if (!file.suggested_plots().empty()) {
        // build one chart per suggested group
        for (const std::vector<std::string>& group : file.suggested_plots()) {
            // create the chart before resolving its expressions
            ChartEngine* chart = controller.add_chart(kind);
            // a refused chart ends the build
            if (chart == nullptr)
                break;
            // the expressions the group resolves to
            std::set<AnyExpression*> expressions;
            // resolve every name the group carries
            for (const std::string& name : group) {
                // a name the manager cannot resolve is skipped
                if (AnyExpression* expression = manager.evaluate(name); expression != nullptr)
                    expressions.insert(expression);
            }
            // plot the resolved group on the new chart
            controller.plot_expressions(controller.chart_count() - 1, expressions);
        }
        // the suggested charts are complete
        return;
    }
    // no suggestion means one chart over the first few plottable expressions
    ChartEngine* chart = controller.add_chart(kind);
    // a refused chart leaves the panel empty
    if (chart == nullptr)
        return;
    // the expressions the default chart plots
    std::set<AnyExpression*> expressions;
    // scan the dataset expressions in their stored order
    for (const std::string& name : manager.expression_names()) {
        // stop when the default chart carries enough series
        if (expressions.size() >= 3)
            break;
        // the abscissa is not a plotted series
        if (name == manager.abscissa().name())
            continue;
        // resolve the expression by name
        AnyExpression* expression = manager.evaluate(name);
        // an unresolved expression is skipped
        if (expression == nullptr)
            continue;
        // a smith chart only plots complex reflection data
        if (kind == ChartKind::SMITH && !std::holds_alternative<Expression<std::complex<double>>>(*expression))
            continue;
        // keep the expression for the default plot
        expressions.insert(expression);
    }
    // plot the default expressions on the single chart
    controller.plot_expressions(0, expressions);
}

void studio::ChartsPresenter::set_viewport(const float width, const float height) {
    // ignore a degenerate panel the layout cannot use
    if (width <= 0.0f || height <= 0.0f)
        return;
    // remember the panel size the next scene records
    m_view_width = width;
    // remember the panel height the next scene records
    m_view_height = height;
    // repoint every dataset so switching tabs shows charts at the current size
    for (const std::unique_ptr<DatasetState>& dataset : m_datasets)
        dataset->controller->set_viewport(width, height);
    // republish the active scene at the new size
    if (!m_datasets.empty())
        publish_scene();
}

void studio::ChartsPresenter::set_on_hover(std::function<void(const std::string&)> handler) { m_on_hover = std::move(handler); }

void studio::ChartsPresenter::set_on_tabs_changed(std::function<void()> handler) { m_on_tabs_changed = std::move(handler); }

void studio::ChartsPresenter::set_dark_mode(const bool dark) {
    // an unchanged scheme republishes nothing
    if (m_dark_mode == dark)
        return;
    // remember the scheme the next scene records
    m_dark_mode = dark;
    // repaint the active dataset, or the empty panel, with the new palette
    if (m_view != nullptr)
        publish_scene();
}

bool studio::ChartsPresenter::fourier_active() const {
    // the active dataset decides which surface shows
    const DatasetState* state = active_state();
    // a Fourier analysis output is presented as its results table
    return state != nullptr && state->fourier;
}

bool studio::ChartsPresenter::has_dataset() const { return !m_datasets.empty(); }

std::size_t studio::ChartsPresenter::dataset_count() const { return m_datasets.size(); }

int studio::ChartsPresenter::active_dataset_id() const {
    // no dataset has no identity
    const DatasetState* state = active_state();
    // hand out the identity the dialogs capture
    return state != nullptr ? state->id : -1;
}

std::size_t studio::ChartsPresenter::active_dataset() const {
    // an empty tab set has no active dataset
    if (m_datasets.empty())
        return 0;
    // hand out an index inside the open tabs
    return std::min(m_active_dataset, m_datasets.size() - 1);
}

bool studio::ChartsPresenter::dataset_closable(const std::size_t index) const {
    // a tab past the set carries no close affordance
    if (index >= m_datasets.size())
        return false;
    // a user dataset closes, while a run output stays until the next run
    return m_datasets[index]->closable;
}

const std::string& studio::ChartsPresenter::dataset_title(const std::size_t index) const {
    // a tab index past the end has no title
    static const std::string empty;
    // a tab index past the end has no title
    if (index >= m_datasets.size())
        return empty;
    // hand out the title the tab shows
    return m_datasets[index]->title;
}

void studio::ChartsPresenter::activate_dataset(const std::size_t index) {
    // ignore a tab index past the end
    if (index >= m_datasets.size() || index == active_dataset())
        return;
    // cancel the outgoing tab interaction
    cancel_interaction();
    // select the requested dataset
    m_active_dataset = index;
    // tell the shell the selection changed
    notify_tabs_changed();
    // publish the newly selected scene
    publish_scene();
}

void studio::ChartsPresenter::close_dataset(const std::size_t index) {
    // ignore a tab index past the end
    if (index >= m_datasets.size())
        return;
    // a run-owned tab has no close affordance
    if (!m_datasets[index]->closable)
        return;
    // cancel the interaction the closing dataset owns
    cancel_interaction();
    // drop the scene before the dataset expressions it owns
    m_datasets.erase(m_datasets.begin() + static_cast<std::ptrdiff_t>(index));
    // a closed tab before the active one shifts the active index down
    if (index < m_active_dataset)
        m_active_dataset--;
    // a closed active tab selects the next neighbour, or the last tab
    else if (index == m_active_dataset && !m_datasets.empty())
        m_active_dataset = std::min(index, m_datasets.size() - 1);
    // an empty tab set returns to the empty panel
    if (m_datasets.empty())
        m_active_dataset = 0;
    // tell the shell the tab set changed
    notify_tabs_changed();
    // an empty tab set clears the view, including its retained pictures
    if (m_datasets.empty()) {
        // the empty panel drops the tabs, the frames and the overlay
        publish_scene();
        // the close is settled
        return;
    }
    // publish the remaining active dataset
    publish_scene();
}

void studio::ChartsPresenter::close_all_datasets() {
    // an empty tab set has nothing to release
    if (m_datasets.empty())
        return;
    // a live drag or readout names a chart that is about to leave
    cancel_interaction();
    // release every dataset and the files it owns
    m_datasets.clear();
    // the active index returns to the first tab
    m_active_dataset = 0;
    // tell the shell the tab set changed, which rebuilds the strip
    notify_tabs_changed();
    // the empty panel drops the retained frames
    publish_scene();
}

void studio::ChartsPresenter::run_chart_command(const ChartCommand command, const std::size_t chart_index) {
    // the active dataset owns the chart the command names
    DatasetState* state = active_state();
    // no dataset has no chart to change
    if (state == nullptr)
        return;
    // a command names a chart that must still exist
    if (chart_index >= state->controller->chart_count())
        return;
    // apply the command to the active dataset
    switch (command) {
    case ChartCommand::ZoomToFit:
        // fit the targeted chart and the shared abscissa
        state->controller->zoom_to_fit(chart_index);
        break;
    case ChartCommand::Autorange:
        // reset the targeted chart's ordinate
        state->controller->autorange(chart_index);
        break;
    case ChartCommand::ZoomAbscissaExtent:
        // reset every chart's abscissa extent
        state->controller->zoom_abscissa_extent();
        break;
    case ChartCommand::AddRemovePlots:
        // the dialog owns the selection, so the shell shows it
        if (m_on_add_remove_plots)
            m_on_add_remove_plots(chart_index);
        // a dialog opens no scene change of its own
        return;
    case ChartCommand::DeleteAllPlots:
        // drop every plotted series of the targeted chart
        state->controller->clear_chart(chart_index);
        break;
    case ChartCommand::StepTool:
        // the dialog owns the selection, so the shell shows it
        if (m_on_step_tool)
            m_on_step_tool(chart_index);
        // a dialog opens no scene change of its own
        return;
    case ChartCommand::CalculateFft:
        // the dialog owns the transform, so the shell shows it
        if (m_on_calculate_fft)
            m_on_calculate_fft(chart_index);
        // a dialog opens no scene change of its own
        return;
    case ChartCommand::AddChart:
        // insert an empty chart after the targeted one
        state->controller->insert_chart(chart_index, state->kind);
        break;
    case ChartCommand::MoveChartUp:
        // the first chart has nothing above it
        if (chart_index > 0)
            state->controller->move_chart(chart_index, chart_index - 1);
        break;
    case ChartCommand::MoveChartDown:
        // the last chart has nothing below it
        if (chart_index + 1 < state->controller->chart_count())
            state->controller->move_chart(chart_index, chart_index + 1);
        break;
    case ChartCommand::DeleteChart:
        // remove the targeted chart, keeping one replacement when it was the last
        state->controller->delete_chart(chart_index, state->kind);
        break;
    case ChartCommand::NewWindow:
        // the shell opens a window of its own over the dataset, kind and title the chart reads
        if (m_on_new_window && state->file != nullptr)
            m_on_new_window(state->file, state->kind, state->title);
        // a spawned window changes no scene of this one
        return;
    }
    // the retained scene follows the rebuilt frames
    publish_scene();
}

void studio::ChartsPresenter::set_on_add_remove_plots(std::function<void(std::size_t)> handler) { m_on_add_remove_plots = std::move(handler); }

void studio::ChartsPresenter::set_on_step_tool(std::function<void(std::size_t)> handler) { m_on_step_tool = std::move(handler); }

void studio::ChartsPresenter::set_on_calculate_fft(std::function<void(std::size_t)> handler) { m_on_calculate_fft = std::move(handler); }

void studio::ChartsPresenter::set_on_new_window(std::function<void(std::shared_ptr<XyceOutputFile>, ChartKind, std::string)> handler) { m_on_new_window = std::move(handler); }

bool studio::ChartsPresenter::adopt_file(std::shared_ptr<XyceOutputFile> file, const ChartKind kind, std::string title) {
    // a detached presenter has no surface to show the dataset on
    if (m_view == nullptr)
        return false;
    // a missing file has no dataset to show
    if (file == nullptr)
        return false;
    // the adopted dataset is a user tab the shell may close
    append_dataset(std::move(file), kind, DatasetRole::User, std::move(title));
    // report the dataset shown
    return true;
}

bool studio::ChartsPresenter::fft_available() const {
    // the active dataset owns the abscissa the transform reads
    const DatasetState* state = active_state();
    // no dataset has no time axis
    if (state == nullptr || state->file == nullptr)
        return false;
    // only a time abscissa can be transformed
    return is_time_domain(*state->file);
}

std::optional<studio::FftSelectionState> studio::ChartsPresenter::fft_selection(const std::size_t chart_index) const {
    // the active dataset owns the chart the dialog opens from
    const DatasetState* state = active_state();
    // no dataset, no file, or no time axis has no transform
    if (state == nullptr || state->file == nullptr || !is_time_domain(*state->file))
        return std::nullopt;
    // a chart index past the stack has no expressions
    if (chart_index >= state->controller->chart_count())
        return std::nullopt;
    // the chart whose series the dialog offers
    const ChartEngine* chart = state->controller->chart(chart_index);
    // a chart that vanished under the menu has nothing to transform
    if (chart == nullptr)
        return std::nullopt;
    // the dataset expressions the dialog lists
    ExpressionManager& manager = state->file->expression_manager();
    // the state the dialog opens with
    FftSelectionState result{.chart_index = chart_index, .dataset_id = state->id};
    // the dataset abscissa range
    const StepInformation& steps = state->file->step_information();
    result.all_from = steps.abscissa_left_value();
    result.all_to = steps.abscissa_right_value();
    // the chart's visible abscissa range, which the current zoom preset names
    result.zoom_from = chart->abscissa_left_value();
    result.zoom_to = chart->abscissa_right_value();
    // the zoom is a narrower range than the dataset
    result.has_zoom = result.zoom_from != result.all_from || result.zoom_to != result.all_to;
    // the abscissa is not a signal
    const std::string abscissa = manager.abscissa().name();
    // every real dataset expression is offered
    for (const std::string& name : manager.expression_names()) {
        // the abscissa never joins the transform
        if (name == abscissa)
            continue;
        // resolve the name to the expression the chart plots
        AnyExpression* expression = manager.evaluate(name);
        // an unresolved name is not offered
        if (expression == nullptr)
            continue;
        // a complex signal has no real transform
        if (!std::holds_alternative<Expression<double>>(*expression))
            continue;
        // the row starts selected when the chart already plots it
        result.expressions.push_back(PlotExpression{.name = name, .unit = expression_unit(*expression), .selected = is_expression_plotted(chart->series(), *expression)});
    }
    // a chart without a real series has no transform
    if (result.expressions.empty())
        return std::nullopt;
    // hand the state to the dialog
    return result;
}

void studio::ChartsPresenter::apply_fft(const FftRequest& request) {
    // the active dataset owns the chart the dialog named
    DatasetState* state = active_state();
    // no dataset has nothing to transform
    if (state == nullptr || state->file == nullptr)
        return;
    // the dataset expressions and steps
    ExpressionManager& manager = state->file->expression_manager();
    const StepInformation& steps = state->file->step_information();
    // the real expressions the dialog selected
    std::vector<AnyExpression*> selected;
    for (const std::string& name : request.expressions) {
        // resolve the name to the expression
        AnyExpression* expression = manager.evaluate(name);
        // an unresolved or complex expression is not transformed
        if (expression == nullptr || !std::holds_alternative<Expression<double>>(*expression))
            continue;
        // keep the expression
        selected.push_back(expression);
    }
    // a transform without a signal has nothing to show
    if (selected.empty())
        return;
    // a transform larger than the cap would exhaust memory before it finishes
    if (request.points == 0 || request.points > MAX_FFT_POINTS)
        return;
    // the frequency bins and samples of every processed step
    std::vector<std::vector<double>> frequency_chunks;
    std::vector<std::vector<std::vector<double>>> chunks(selected.size());
    std::vector<std::pair<std::size_t, std::size_t>> abscissa_slices;
    std::vector<std::pair<double, double>> abscissa_ranges;
    std::vector<std::size_t> processed_steps;
    std::size_t offset = 0;
    // one transform runs over every step's interval
    for (std::size_t step = 0; step < steps.length(); ++step) {
        // the step abscissa, which the range selects
        std::span<const double> abscissa = manager.abscissa().step_data(step);
        // the samples the requested interval covers
        const auto left = std::lower_bound(abscissa.begin(), abscissa.end(), request.from);
        const auto right = std::upper_bound(abscissa.begin(), abscissa.end(), request.to);
        // the interval bounds inside the step
        const std::size_t from_index = static_cast<std::size_t>(std::distance(abscissa.begin(), left));
        const std::size_t to_index = static_cast<std::size_t>(std::distance(abscissa.begin(), right));
        // a range with fewer than two samples has no spectrum
        if (to_index - from_index < 2)
            continue;
        // the signal matrix for this step
        std::vector<std::span<const double>> signals;
        for (AnyExpression* expression : selected) {
            // the real view of the expression
            Expression<double>& real = std::get<Expression<double>>(*expression);
            // the samples inside the interval
            signals.push_back(real.step_data(step).subspan(from_index, to_index - from_index));
        }
        // a step without signals is skipped
        if (signals.empty())
            continue;
        // the interval the transform reads
        const std::span<const double> interval = abscissa.subspan(from_index, to_index - from_index);
        // the transform for this step
        fft::FftResult result;
        try {
            result = fft::compute_fft_many(interval, signals, request.points, request.window, request.format, 0, interval.size() - 1, request.output, request.keep_dc);
        }
        catch (...) {
            // a failed transform leaves the dataset untouched
            return;
        }
        // an empty frequency axis has nothing to plot
        if (result.frequencies.empty())
            return;
        // remember the step slice and value range
        abscissa_slices.emplace_back(offset, offset + result.frequencies.size());
        // accumulate the offset
        offset += result.frequencies.size();
        // the step the transform produced
        processed_steps.push_back(step);
        // keep the bins and every signal
        abscissa_ranges.emplace_back(result.frequencies.front(), result.frequencies.back());
        frequency_chunks.push_back(std::move(result.frequencies));
        for (std::size_t index = 0; index < result.values.size(); ++index)
            chunks[index].push_back(std::move(result.values[index]));
    }
    // a transform without a processed step has nothing to show
    if (abscissa_slices.empty())
        return;
    // the flattened frequency abscissa
    std::vector<double> frequency;
    for (const std::vector<double>& chunk : frequency_chunks)
        frequency.insert(frequency.end(), chunk.begin(), chunk.end());
    // the FFT expressions, starting with the frequency axis
    std::vector<AnyExpression> expressions;
    expressions.emplace_back(Expression<double>("Frequency", std::move(frequency), abscissa_slices, "Hz"));
    // the charts the new dataset opens with
    std::vector<std::vector<std::string>> suggested;
    for (std::size_t index = 0; index < selected.size(); ++index) {
        // the real expression the transform read
        Expression<double>& real = std::get<Expression<double>>(*selected[index]);
        // the flattened spectrum
        std::vector<double> data;
        for (const std::vector<double>& chunk : chunks[index])
            data.insert(data.end(), chunk.begin(), chunk.end());
        // the output decides the unit the readout shows
        std::string unit;
        if (request.output == fft::FftOutput::PHASE)
            unit = "°";
        else if (request.output == fft::FftOutput::MAGNITUDE_DB)
            unit = "dB";
        else
            unit = real.unit();
        // the transformed expression name
        const std::string name = "FFT(" + real.name() + ")";
        // the first three spectra open as charts
        if (suggested.size() < 3)
            suggested.push_back({name});
        // append the transformed expression
        expressions.emplace_back(Expression<double>(name, std::move(data), abscissa_slices, unit));
    }
    // the keys and values of the steps the transform produced, which a skipped step must not shift
    std::vector<std::string> fft_keys;
    std::vector<std::vector<double>> fft_values;
    for (const std::size_t step : processed_steps) {
        // a carried key describes the step
        if (step < steps.keys().size())
            fft_keys.push_back(steps.keys()[step]);
        // a carried value set describes the step
        if (step < steps.values().size())
            fft_values.push_back(steps.values()[step]);
    }
    // the step information the transform produced
    StepInformation fft_steps(std::move(fft_keys), std::move(fft_values), abscissa_ranges);
    // the expression manager the dataset reads
    ExpressionManager fft_manager(expressions, abscissa_slices);
    // the frequency span the whole transform covers, which a multi-step run widens
    const auto lowest = std::min_element(abscissa_ranges.begin(), abscissa_ranges.end(), [](const std::pair<double, double>& lhs, const std::pair<double, double>& rhs) { return lhs.first < rhs.first; });
    // the highest bin the transform produced
    const auto highest = std::max_element(abscissa_ranges.begin(), abscissa_ranges.end(), [](const std::pair<double, double>& lhs, const std::pair<double, double>& rhs) { return lhs.second < rhs.second; });
    // the title names the window and the frequency span
    const std::string title = "FFT: " + fft_window_label(request.window) + ", " + std::to_string(static_cast<long long>(lowest->first + 0.5)) + "–" + std::to_string(static_cast<long long>(highest->second + 0.5)) + " Hz";
    // the synthesized dataset carries the transform
    auto file = std::make_shared<XyceOutputFile>(std::filesystem::path{}, title, false, std::move(fft_steps), PlotType::FFT, AbscissaScale::LINEAR, std::move(fft_manager), nullptr, std::move(suggested));
    // the new dataset joins the tab strip as a closable user tab
    append_dataset(std::move(file), ChartKind::XY, DatasetRole::User, title);
}

std::optional<studio::StepToolState> studio::ChartsPresenter::step_tool_state(const std::size_t chart_index) const {
    // the active dataset owns the chart the dialog edits
    const DatasetState* state = active_state();
    // no dataset, file or chart has no step tool
    if (state == nullptr || state->file == nullptr || chart_index >= state->controller->chart_count())
        return std::nullopt;
    // the chart whose selection the dialog marks
    const ChartEngine* chart = state->controller->chart(chart_index);
    // a chart that vanished under the dialog has no selection
    if (chart == nullptr)
        return std::nullopt;
    // the steps the dataset carries
    const StepInformation& steps = state->file->step_information();
    // a single step has nothing to choose
    if (steps.length() <= 1)
        return std::nullopt;
    // the state the dialog opens with
    StepToolState result{.chart_index = chart_index, .dataset_id = state->id};
    // the per-step parameter values, when the file carries one vector per step
    const std::vector<std::vector<double>>& values = steps.values();
    // a file without a value per key and step opens with the index column only
    const bool values_per_step = values.size() == steps.length() && !steps.keys().empty() && std::all_of(values.begin(), values.end(), [&](const std::vector<double>& row) { return row.size() == steps.keys().size(); });
    // the dialog names the columns only when it can fill them
    if (values_per_step)
        result.keys = steps.keys();
    // the chart's current selection
    const std::set<std::size_t>& selected = chart->selected_steps();
    // one row per parameter combination
    for (std::size_t index = 0; index < steps.length(); ++index) {
        // the row starts with its index and selection
        StepToolRow row{.index = index, .selected = selected.contains(index)};
        // the formatted parameter values follow when the file carries them
        if (values_per_step) {
            // one formatted cell per key
            for (const double value : values[index]) {
                // format the value the way the readouts do
                std::ostringstream text;
                // the default precision keeps the parameter readable
                text << value;
                // keep the formatted value
                row.values.push_back(text.str());
            }
        }
        // keep the row
        result.rows.push_back(std::move(row));
    }
    // hand the state to the dialog
    return result;
}

void studio::ChartsPresenter::apply_step_selection(const std::size_t chart_index, std::set<std::size_t> steps) {
    // the active dataset owns the chart the dialog edits
    DatasetState* state = active_state();
    // no dataset and no chart have nothing to change
    if (state == nullptr || chart_index >= state->controller->chart_count())
        return;
    // the chart keeps only the accepted steps
    state->controller->set_selected_steps(chart_index, steps);
    // the retained frames follow the rebuilt engines
    publish_scene();
}

std::optional<studio::PlotSelectionState> studio::ChartsPresenter::plot_selection(const std::size_t chart_index) const {
    // the active dataset owns the chart the dialog edits
    const DatasetState* state = active_state();
    // no dataset and no chart have no selection to offer
    if (state == nullptr || state->file == nullptr || chart_index >= state->controller->chart_count())
        return std::nullopt;
    // the chart whose series the dialog marks
    const ChartEngine* chart = state->controller->chart(chart_index);
    // a chart that vanished under the dialog has no selection
    if (chart == nullptr)
        return std::nullopt;
    // a smith chart offers the diagonal entries and no custom row
    const bool smith = state->kind == ChartKind::SMITH;
    // the state the dialog opens with
    PlotSelectionState selection{.chart_index = chart_index, .dataset_id = state->id, .smith = smith, .allow_custom = !smith};
    // the expressions the dataset resolved
    ExpressionManager& manager = state->file->expression_manager();
    // the abscissa is not a plotted series
    const std::string abscissa = manager.abscissa().name();
    // list the dataset expressions in their stored order
    for (const std::string& name : manager.expression_names()) {
        // the abscissa never joins the selection
        if (name == abscissa)
            continue;
        // resolve the name to the expression the chart plots
        AnyExpression* expression = manager.evaluate(name);
        // an unresolved name is not offered
        if (expression == nullptr)
            continue;
        // a smith chart only offers the diagonal entries its plane can draw
        if (smith && !is_smith_expression(*expression))
            continue;
        // the row carries the name, the unit and the chart's current state
        selection.expressions.push_back(PlotExpression{.name = name, .unit = expression_unit(*expression), .selected = is_expression_plotted(chart->series(), *expression)});
    }
    // hand the state to the dialog
    return selection;
}

std::optional<studio::PlotExpression> studio::ChartsPresenter::resolve_plot_expression(const std::size_t chart_index, const std::string& expression) const {
    // the active dataset owns the chart the expression would plot on
    const DatasetState* state = active_state();
    // no dataset and no chart can resolve an expression
    if (state == nullptr || state->file == nullptr || chart_index >= state->controller->chart_count())
        return std::nullopt;
    // the fixed gamma plane refuses an arbitrary expression
    if (state->kind == ChartKind::SMITH)
        return std::nullopt;
    // trim the text the user typed
    const std::string text = trim(expression);
    // an empty entry resolves nothing
    if (text.empty())
        return std::nullopt;
    // the manager parses the expression, or returns nothing when it does not parse
    AnyExpression* resolved = state->file->expression_manager().evaluate(text);
    // a refused expression leaves the dialog with its message
    if (resolved == nullptr)
        return std::nullopt;
    // the dialog lists the canonical name and unit the manager produced
    return PlotExpression{.name = expression_name(*resolved), .unit = expression_unit(*resolved), .selected = true};
}

bool studio::ChartsPresenter::apply_plot_selection(const std::size_t chart_index, const std::vector<std::string>& names) {
    // the active dataset owns the chart the dialog edited
    DatasetState* state = active_state();
    // no dataset and no chart have a selection to apply
    if (state == nullptr || state->file == nullptr || chart_index >= state->controller->chart_count())
        return false;
    // the expressions the accepted names resolve to
    std::set<AnyExpression*> selected;
    // the manager every name resolves through
    ExpressionManager& manager = state->file->expression_manager();
    // resolve every accepted name
    for (const std::string& name : names) {
        // resolve the name to the expression the chart plots
        AnyExpression* expression = manager.evaluate(name);
        // a name the manager cannot resolve refuses the whole selection
        if (expression == nullptr)
            return false;
        // the expression joins the plotted set
        selected.insert(expression);
    }
    // replace the plotted series of the chart
    state->controller->plot_expressions(chart_index, selected);
    // the retained scene follows the rebuilt frame
    publish_scene();
    return true;
}

std::uint64_t studio::ChartsPresenter::chart_revision() const {
    // no dataset describes no chart
    const DatasetState* state = active_state();
    // hand out the revision the active charts were built at
    return state != nullptr && state->controller != nullptr ? state->controller->revision() : 0;
}

std::size_t studio::ChartsPresenter::chart_count() const {
    // no dataset has no chart
    const DatasetState* state = active_state();
    // no dataset has no chart
    return state != nullptr ? state->controller->chart_count() : 0;
}

const std::string& studio::ChartsPresenter::title() const {
    // no dataset has no title
    const DatasetState* state = active_state();
    // no dataset has no title
    static const std::string empty;
    // hand out the active dataset title
    return state != nullptr ? state->title : empty;
}

studio::ChartsPresenter::DatasetState* studio::ChartsPresenter::active_state() {
    // no dataset has no active record
    if (m_datasets.empty())
        return nullptr;
    // clamp the active index to the tab set
    m_active_dataset = std::min(m_active_dataset, m_datasets.size() - 1);
    // hand out the active record
    return m_datasets[m_active_dataset].get();
}

const studio::ChartsPresenter::DatasetState* studio::ChartsPresenter::active_state() const {
    // no dataset has no active record
    if (m_datasets.empty())
        return nullptr;
    // clamp the active index to the tab set
    const std::size_t index = std::min(m_active_dataset, m_datasets.size() - 1);
    // hand out the active record
    return m_datasets[index].get();
}

void studio::ChartsPresenter::notify_tabs_changed() {
    // a shell that listens updates its chart enablement and body
    if (m_on_tabs_changed)
        m_on_tabs_changed();
}

void studio::ChartsPresenter::publish_zoom_selection() {
    // a detached presenter has no view to publish to
    if (m_view == nullptr)
        return;
    // the active dataset owns the selection
    DatasetState* state = active_state();
    // no dataset has no selection
    if (state == nullptr) {
        // remove any previous overlay
        m_view->set_zoom_selection(std::nullopt);
        // nothing else to record
        return;
    }
    // the selection the controller currently reports
    const std::optional<std::tuple<float, float, float, float>> selection = state->controller->zoom_selection();
    // a drag that has not moved has no overlay
    if (!selection) {
        // remove the previous overlay from the view
        m_view->set_zoom_selection(std::nullopt);
        // nothing else to record
        return;
    }
    // read the normalized corners of the selection
    const auto [left, top, right, bottom] = *selection;
    // hand the content-relative rectangle to the view
    m_view->set_zoom_selection(albor::Rect::from_origin_size(albor::Point{.x = left, .y = top}, albor::Size{.width = right - left, .height = bottom - top}));
}

void studio::ChartsPresenter::publish_scene() {
    // a detached presenter has no view to publish to
    if (m_view == nullptr)
        return;
    // a new scene drops the selection recorded against the old one
    m_view->set_zoom_selection(std::nullopt);
    // the step tool applies only while the active dataset carries several steps
    const DatasetState* stepped = active_state();
    m_view->set_step_tool_available(stepped != nullptr && stepped->file != nullptr && stepped->file->step_information().length() > 1);
    // the FFT applies only to a time-domain dataset
    m_view->set_fft_available(fft_available());
    // no dataset leaves the panel empty
    DatasetState* state = active_state();
    // no dataset leaves the panel empty
    if (state == nullptr) {
        // the table drops the blocks of the dataset that closed
        if (m_results != nullptr)
            m_results->set_tables({});
        // no dataset is published any more
        m_published_fourier_id = 0;
        // no revision is published any more
        m_published_fourier_revision = 0;
        // the empty panel reports no chart
        m_view->set_accessible_summary("0 charts");
        // the empty panel has no chart geometry
        m_view->set_frames({});
        // the palette still follows the active scheme
        m_view->set_palette(chart_palette(m_dark_mode));
        // the empty scene is settled
        return;
    }
    // a Fourier analysis output keeps its spectra in the results table, which needs no frames
    if (state->fourier) {
        // the hidden canvas reports the surface the body shows
        m_view->set_accessible_summary("Fourier results");
        // the hidden canvas keeps no chart geometry
        m_view->set_frames({});
        // the hidden canvas follows the active scheme like the table does
        m_view->set_palette(chart_palette(m_dark_mode));
        // publish the blocks the table lists
        publish_fourier(*state);
        // the table scene is settled
        return;
    }
    // the arranged view is the authority on the panel extent once it has one
    const float arranged_width = m_view->viewport_width();
    // the arranged height is the chart canvas
    const float arranged_height = m_view->viewport_height();
    // adopt the arranged extent before the frames are built
    if (arranged_width > 0.0f && arranged_height > 0.0f) {
        // record the width the frames are built for
        m_view_width = arranged_width;
        // record the height the frames are built for
        m_view_height = arranged_height;
    }
    // resize the active controller when the view extent changed
    if (state->controller->width() != m_view_width || state->controller->height() != m_view_height)
        state->controller->set_viewport(m_view_width, m_view_height);
    // name the chart kinds and the tab count for automation and assistive technology
    const std::size_t count = state->controller->chart_count();
    // smith and cartesian scenes read out the kind of their first chart
    const bool smith = !state->controller->frames().empty() && state->controller->frames().front().smith;
    // the chart summary carries the count and the kind
    std::string summary = std::to_string(count) + (smith ? " smith chart" : " line chart") + (count == 1 ? "" : "s");
    // more than one tab names the dataset count first and the active tab so automation can follow a switch
    if (m_datasets.size() > 1)
        summary = std::to_string(m_datasets.size()) + " datasets, " + summary + ", active: " + state->title;
    // hand the summary and the palette to the view
    m_view->set_accessible_summary(std::move(summary));
    // hand the frames the plot rectangles come from
    m_view->set_frames(state->controller->frames());
    // the palette follows the active scheme
    m_view->set_palette(chart_palette(m_dark_mode));
}

void studio::ChartsPresenter::publish_fourier(DatasetState& state) {
    // a detached presenter has no table to fill
    if (m_results == nullptr)
        return;
    // the table follows the active scheme
    m_results->set_palette(chart_palette(m_dark_mode));
    // the table already shows these blocks, so the reader keeps the scroll where it was
    if (m_published_fourier_id == state.id && m_published_fourier_revision == state.fourier_revision)
        return;
    // the header names the dataset the file carries, falling back to the tab label
    m_results->set_title(state.file != nullptr && !state.file->title().empty() ? state.file->title() : state.title);
    // the table lists the blocks the file carries
    m_results->set_tables(state.fourier_tables);
    // remember the blocks the table now shows
    m_published_fourier_id = state.id;
    // remember the revision the table now shows
    m_published_fourier_revision = state.fourier_revision;
}

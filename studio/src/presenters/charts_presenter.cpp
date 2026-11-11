#include "presenters/charts_presenter.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
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
#include "views/charts/charts_view.h"

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
} // namespace

studio::ChartsPresenter::ChartsPresenter(ChartsView& view, albor::TextEngine& text) :
    m_text(text), m_builder(m_resources, m_text), m_view(&view) {
    // install the zoom callbacks the view reports
    m_view->set_on_zoom_drag_started([this](const float x, const float y) {
        // the active dataset owns the drag
        DatasetState* state = active_state();
        // a press inside a plot starts the selection overlay
        if (state != nullptr && state->controller->zoom_drag_started(x, y))
            publish_overlay();
    });
    // install the moving corner callback
    m_view->set_on_zoom_drag_moved([this](const float x, const float y) {
        // the active dataset owns the drag
        DatasetState* state = active_state();
        // a move updates the selection overlay
        if (state != nullptr && state->controller->zoom_drag_moved(x, y))
            publish_overlay();
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
        m_view->set_overlay(nullptr);
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
        m_view->set_overlay(nullptr);
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
    // install the dataset tab callbacks
    m_view->set_on_tab_selected([this](const int index) { activate_dataset(static_cast<std::size_t>(index)); });
    // install the dataset close callback
    m_view->set_on_tab_closed([this](const int index) { close_dataset(static_cast<std::size_t>(index)); });
    // install the resize settle callback
    m_view->set_on_resize_settled([this](const float width, const float height) { set_viewport(width, height); });
    // the view clears the borrowed pointer before the window tree frees it
    m_view->set_on_destroyed([this] { m_view = nullptr; });
}

studio::ChartsPresenter::~ChartsPresenter() { detach(); }

void studio::ChartsPresenter::detach() {
    // the window teardown may already have dropped the view
    if (m_view == nullptr)
        return;
    // clear every callback the presenter installed
    m_view->set_on_zoom_drag_started({});
    // clear the moving corner callback
    m_view->set_on_zoom_drag_moved({});
    // clear the release callback
    m_view->set_on_zoom_drag_ended({});
    // clear the cancel callback
    m_view->set_on_zoom_drag_canceled({});
    // clear the hover callback
    m_view->set_on_hover_moved({});
    // clear the hover leave callback
    m_view->set_on_hover_ended({});
    // clear the tab callbacks
    m_view->set_on_tab_selected({});
    // clear the tab close callback
    m_view->set_on_tab_closed({});
    // clear the resize callback
    m_view->set_on_resize_settled({});
    // clear the destructor callback
    m_view->set_on_destroyed({});
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

void studio::ChartsPresenter::load_dataset(std::shared_ptr<XyceOutputFile> file, const ChartKind kind, const bool closable) {
    // a null dataset clears every tab
    if (file == nullptr) {
        // cancel the interaction the outgoing dataset owns
        cancel_interaction();
        // release the scenes before the datasets that own their expressions
        m_datasets.clear();
        // the active index returns to the first tab
        m_active_dataset = 0;
        // a live view swaps to the empty panel
        if (m_view != nullptr) {
            m_view->set_overlay(nullptr);
            m_view->set_scene(nullptr);
            m_view->set_accessible_summary("0 charts");
        }
        // tell the shell the tabs are gone
        notify_tabs_changed();
        // the empty dataset is settled
        return;
    }
    // a user dataset carries a close affordance when the caller asks for one
    append_dataset(std::move(file), kind, closable ? DatasetRole::User : DatasetRole::Companion, {});
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
    state->controller = std::make_unique<ChartsController>(m_view_width, std::max(0.0f, m_view_height - tab_bar_height()), [this](const std::string& label) { return m_text.measure(label); });
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
    // keep the tab labels unique so duplicate files stay distinguishable
    state->title = unique_title(title);
    // only a user file carries a close affordance
    state->closable = role == DatasetRole::User;
    // build the suggested or default charts
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
    // rebuild only when the preserved charts cannot describe the new file
    if (kind_changed || !all_resolved) {
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
    // drop every dataset the role names
    std::erase_if(m_datasets, [role](const std::unique_ptr<DatasetState>& dataset) { return dataset->role == role; });
    // an empty tab set returns to the first index
    if (m_datasets.empty()) {
        // reset the selection
        m_active_dataset = 0;
        // the tab set is settled
        return;
    }
    // clamp the selection to the remaining tabs
    m_active_dataset = std::min(m_active_dataset, m_datasets.size() - 1);
}

std::string studio::ChartsPresenter::unique_title(const std::string& base, const DatasetState* exclude) const {
    // the first tab keeps the plain title
    std::string candidate = base;
    // count the suffix until no other open tab carries it
    for (int suffix = 2; std::any_of(m_datasets.begin(), m_datasets.end(), [&candidate, exclude](const std::unique_ptr<DatasetState>& dataset) { return dataset.get() != exclude && dataset->title == candidate; }); ++suffix)
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
        m_view->set_overlay(nullptr);
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
    // the strip only shows when the tabs need it
    const float strip_height = tab_bar_height();
    // the chart content is what the strip leaves
    const float content_height = std::max(0.0f, height - strip_height);
    // repoint every dataset so switching tabs shows charts at the current size
    for (const std::unique_ptr<DatasetState>& dataset : m_datasets)
        dataset->controller->set_viewport(width, content_height);
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
    // repaint the active dataset with the new palette
    if (!m_datasets.empty() && m_view != nullptr)
        publish_scene();
}

bool studio::ChartsPresenter::has_dataset() const { return !m_datasets.empty(); }

std::size_t studio::ChartsPresenter::dataset_count() const { return m_datasets.size(); }

std::size_t studio::ChartsPresenter::active_dataset() const {
    // an empty tab set has no active dataset
    if (m_datasets.empty())
        return 0;
    // hand out an index inside the open tabs
    return std::min(m_active_dataset, m_datasets.size() - 1);
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
    // publish the remaining scene, or clear the view when the last tab closed
    if (m_datasets.empty()) {
        // a live view drops the scene and the overlay
        if (m_view != nullptr) {
            m_view->set_overlay(nullptr);
            m_view->set_scene(nullptr);
            m_view->set_accessible_summary("0 charts");
        }
        // the close is settled
        return;
    }
    // publish the remaining active dataset
    publish_scene();
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

const studio::ChartScene* studio::ChartsPresenter::scene() const {
    // no dataset has no scene
    const DatasetState* state = active_state();
    // hand out the active retained scene
    return state != nullptr ? state->scene.get() : nullptr;
}

std::size_t studio::ChartsPresenter::series_count(const std::size_t chart_index) const {
    // no dataset has no series
    const DatasetState* state = active_state();
    // no dataset has no series
    if (state == nullptr)
        return 0;
    // a chart index past the frames has no series
    const std::vector<ChartFrame>& frames = state->controller->frames();
    // a chart index past the built frames has no series
    if (chart_index >= frames.size())
        return 0;
    // hand out the runs the chart frame carries
    return frames[chart_index].series.size();
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

float studio::ChartsPresenter::tab_bar_height() const {
    // a single closable tab still needs the strip so the user can close it
    const bool any_closable = std::any_of(m_datasets.begin(), m_datasets.end(), [](const std::unique_ptr<DatasetState>& dataset) { return dataset->closable; });
    // reserve the strip when it carries more than one tab or a close affordance
    return m_datasets.size() > 1 || any_closable ? CHART_TAB_HEIGHT : 0.0f;
}

std::vector<studio::ChartTabSpec> studio::ChartsPresenter::tab_specs() const {
    // the tab specs under construction
    std::vector<ChartTabSpec> specs;
    // reserve one spec per dataset
    specs.reserve(m_datasets.size());
    // record every dataset in tab order
    for (std::size_t index = 0; index < m_datasets.size(); ++index) {
        // the spec carries the title, selection and close affordance
        specs.push_back(ChartTabSpec{.title = m_datasets[index]->title, .active = index == active_dataset(), .closable = m_datasets[index]->closable});
    }
    // hand the specs to the scene builder
    return specs;
}

void studio::ChartsPresenter::notify_tabs_changed() {
    // a shell that listens updates its chart enablement and body
    if (m_on_tabs_changed)
        m_on_tabs_changed();
}

void studio::ChartsPresenter::publish_scene() {
    // a detached presenter has no view to publish to
    if (m_view == nullptr)
        return;
    // no dataset leaves the panel empty
    DatasetState* state = active_state();
    // no dataset leaves the panel empty
    if (state == nullptr) {
        // a live view drops the scene
        m_view->set_scene(nullptr);
        // the empty panel reports no chart
        m_view->set_accessible_summary("0 charts");
        // the empty scene is settled
        return;
    }
    // the strip only appears when the tabs need it
    const float strip_height = tab_bar_height();
    // the chart content is what the strip leaves
    const float content_height = std::max(0.0f, m_view_height - strip_height);
    // resize the active controller when the content extent changed
    if (state->controller->width() != m_view_width || state->controller->height() != content_height)
        state->controller->set_viewport(m_view_width, content_height);
    // record the retained scene with the tab strip
    state->scene = m_builder.build(state->controller->frames(), m_view_width, m_view_height, chart_palette(m_dark_mode), tab_specs(), strip_height);
    // name the chart kinds and the tab count for automation and assistive technology
    const std::size_t count = state->controller->chart_count();
    // smith and cartesian scenes read out the kind of their first chart
    const bool smith = !state->controller->frames().empty() && state->controller->frames().front().smith;
    // the chart summary carries the count and the kind
    std::string summary = std::to_string(count) + (smith ? " smith chart" : " line chart") + (count == 1 ? "" : "s");
    // more than one tab names the dataset count first and the active tab so automation can follow a switch
    if (m_datasets.size() > 1)
        summary = std::to_string(m_datasets.size()) + " datasets, " + summary + ", active: " + state->title;
    // hand the summary and the retained picture to the view
    m_view->set_accessible_summary(std::move(summary));
    // hand the retained picture to the view
    m_view->set_scene(state->scene);
}

void studio::ChartsPresenter::publish_overlay() {
    // a detached presenter has no view to publish to
    if (m_view == nullptr)
        return;
    // the active dataset owns the selection
    DatasetState* state = active_state();
    // no dataset has no selection
    if (state == nullptr) {
        // remove any previous overlay
        m_view->set_overlay(nullptr);
        // nothing else to record
        return;
    }
    // the selection the controller currently reports
    const std::optional<std::tuple<float, float, float, float>> selection = state->controller->zoom_selection();
    // a drag that has not moved has no overlay
    if (!selection) {
        // remove the previous overlay from the view
        m_view->set_overlay(nullptr);
        // nothing else to record
        return;
    }
    // read the normalized corners of the selection
    const auto [left, top, right, bottom] = *selection;
    // the chart content sits under the tab strip
    const float content_y = tab_bar_height();
    // build the rectangle the overlay fills in panel coordinates
    const albor::Rect rect = albor::Rect::from_origin_size(albor::Point{.x = left, .y = top + content_y}, albor::Size{.width = right - left, .height = bottom - top});
    // record the overlay and hand it to the view
    m_view->set_overlay(m_builder.build_zoom_overlay(rect));
}

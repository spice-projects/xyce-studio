#include "views/simulation_parameters/simulation_parameters_dialog_view.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "albor/application/application.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "simulation/ac_simulation_parameters.h"
#include "simulation/dc_simulation_parameters.h"
#include "simulation/transient_simulation_parameters.h"
#include "simulation_parameters_dialog.h"

namespace
{
    // the DC sweep modes in combo order, which the selected index names
    constexpr std::array<const char*, 5> DC_MODES = {"LIN", "DEC", "OCT", "LIST", "DATA"};

    // the AC sweep modes in combo order, where DATA has no range fields
    constexpr std::array<const char*, 4> AC_MODES = {"LIN", "DEC", "OCT", "DATA"};

    // the transient initial condition keywords in combo order, where the head is the default
    constexpr std::array<const char*, 3> OP_KEYWORDS = {"", "NOOP", "UIC"};

    // trim the surrounding whitespace a field may carry
    [[nodiscard]] std::string trim(std::string_view value) {
        // find the first byte that is not whitespace
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        // an all-whitespace value trims to empty
        if (first == std::string_view::npos)
            return {};
        // find the last byte that is not whitespace
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        // copy the trimmed span into an owning string
        return std::string(value.substr(first, last - first + 1));
    }

    // resolve a stored mode string to its combo index, where an unknown mode reads as the head
    template <std::size_t N>
    [[nodiscard]] int choice_index(const std::array<const char*, N>& values, std::string_view value) {
        // the stored spelling is the authoritative match
        for (std::size_t index = 0; index < N; ++index) {
            // the matching entry is the position the combo shows
            if (value == values[index])
                return static_cast<int>(index);
        }
        // an unknown or empty mode falls back to the first entry
        return 0;
    }

    // split a whitespace separated value list into its tokens
    [[nodiscard]] std::vector<std::string> split_whitespace(std::string_view value) {
        // the tokens the list carries
        std::vector<std::string> parts;
        // the cursor walks the whole run
        std::size_t cursor = 0;
        // consume leading whitespace and one token per pass
        while (cursor < value.size()) {
            // skip the separator run between tokens
            while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) != 0)
                ++cursor;
            // the token starts at the first non separator byte
            const std::size_t start = cursor;
            // the token ends at the next separator or the end
            while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) == 0)
                ++cursor;
            // an empty span at the end names no token
            if (cursor > start)
                parts.emplace_back(value.substr(start, cursor - start));
        }
        return parts;
    }

    // join a value list with the single separator the field shows
    [[nodiscard]] std::string join_values(const std::vector<std::string>& values) {
        // the joined run the field edits
        std::string joined;
        // every entry is separated from the previous one by a space
        for (const std::string& value : values) {
            // a non head entry needs the separator before it
            if (!joined.empty())
                joined.push_back(' ');
            // the entry itself follows
            joined += value;
        }
        return joined;
    }

    // map the configured analysis to the tab that edits it, where an analysis outside the slice reads as transient
    [[nodiscard]] int tab_for(const SimulationConfig& config) {
        // a DC analysis is edited on the second tab
        if (std::holds_alternative<DCSimulationParameters>(config.analysis))
            return 1;
        // an AC analysis is edited on the third tab
        if (std::holds_alternative<AcSimulationParameters>(config.analysis))
            return 2;
        // every other analysis or none opens the transient tab
        return 0;
    }
} // namespace

studio::AlborSimulationParametersDialogView::AlborSimulationParametersDialogView(albor::Application& application, albor::WindowHandle parent) :
    m_app(&application), m_parent(parent) {}

studio::AlborSimulationParametersDialogView::~AlborSimulationParametersDialogView() = default;

void studio::AlborSimulationParametersDialogView::show(const SimulationConfig& current, SimulationParametersDialogEvents& events) {
    // a second show while the window is up is ignored
    if (is_open())
        return;
    // remember the configuration and the presenter the dialog works from
    m_seed = current;
    m_events = &events;
    // the new window starts with a clean close state
    m_closing = false;
    // describe a fixed size modal dialog owned by the shell
    albor::WindowOptions options;
    options.title = "Simulation Parameters";
    options.size = albor::Size{680.0f, 600.0f};
    options.role = albor::WindowRole::Dialog;
    options.modality = albor::Modality::WindowModal;
    options.parent = m_parent;
    options.resizable = false;
    options.visible = false;
    // build the compiled dialog tree in a fresh window
    m_handle = m_app->create_window(options, m_root);
    // a refused window leaves the presenter without a surface
    if (!m_handle.valid() || m_root == nullptr) {
        m_handle = {};
        m_root = nullptr;
        m_events = nullptr;
        events.on_dialog_closed();
        return;
    }
    // theme the dialog from the same system appearance as the shell
    m_theme = install_theme(*m_app, m_handle);
    // the tab the configured analysis lives on is the one the dialog opens
    m_root->set_selected_tab(tab_for(current));
    // the replace ground flag is global, so every tab starts from the configured value
    m_root->set_tran_replace_ground(current.replace_ground);
    m_root->set_dc_replace_ground(current.replace_ground);
    m_root->set_ac_replace_ground(current.replace_ground);
    // a transient configuration seeds the transient fields
    if (const auto* transient = std::get_if<TransientSimulationParameters>(&current.analysis)) {
        m_root->set_tran_initial_step(transient->initial_step_value);
        m_root->set_tran_final_time(transient->final_time_value);
        m_root->set_tran_start_time(transient->start_time_value);
        m_root->set_tran_step_ceiling(transient->step_ceiling_value);
        m_root->set_tran_op_keyword(choice_index(OP_KEYWORDS, transient->op_keyword));
    }
    // a DC configuration seeds the DC fields and the first sweep tuple
    if (const auto* dc = std::get_if<DCSimulationParameters>(&current.analysis)) {
        m_root->set_dc_mode(choice_index(DC_MODES, dc->sweep_mode));
        m_root->set_dc_table(dc->data_table_name);
        // the slice edits one sweep tuple, which is the first when the configuration carries one
        if (!dc->sweeps.empty()) {
            const DcSweep& sweep = dc->sweeps.front();
            m_root->set_dc_variable(sweep.variable);
            m_root->set_dc_start(sweep.start);
            m_root->set_dc_stop(sweep.stop);
            m_root->set_dc_step(sweep.step);
            m_root->set_dc_points(sweep.points);
            m_root->set_dc_list(join_values(sweep.list_values));
        }
    }
    // an AC configuration seeds the AC fields
    if (const auto* ac = std::get_if<AcSimulationParameters>(&current.analysis)) {
        m_root->set_ac_mode(choice_index(AC_MODES, ac->sweep_mode));
        m_root->set_ac_points(ac->points);
        m_root->set_ac_start(ac->start);
        m_root->set_ac_end(ac->end);
        m_root->set_ac_table(ac->data_table_name);
    }
    // clear any stale validation feedback
    m_root->set_show_error(false);
    m_root->set_error_message("");
    // forward the dialog actions to this view
    m_root->set_on_accepted([this] { accept(); });
    m_root->set_on_dismissed([this] { dismiss(); });
    // show the fully seeded window, which avoids a blank first frame
    if (albor::Window* window = m_app->window(m_handle))
        window->show();
}

void studio::AlborSimulationParametersDialogView::close() {
    // a close with no open window is a no-op
    if (!m_handle.valid())
        return;
    // a second close waits for the first teardown
    if (m_closing)
        return;
    // record the close so the closed notification knows it was requested
    m_closing = true;
    // ask the application to close the window, which consults the veto and defers an input driven teardown
    (void)m_app->request_close(m_handle);
}

void studio::AlborSimulationParametersDialogView::show_error(std::string_view message) {
    // without a live facade there is nothing to update
    if (m_root == nullptr)
        return;
    // carry the message the validation produced
    m_root->set_error_message(std::string(message));
    // show the error row in the layout
    m_root->set_show_error(true);
}

bool studio::AlborSimulationParametersDialogView::is_open() const { return m_handle.valid(); }

void studio::AlborSimulationParametersDialogView::on_window_closed(albor::WindowHandle handle) {
    // ignore windows this view does not own
    if (handle != m_handle)
        return;
    // take the presenter out before clearing, because the notification may reset it
    SimulationParametersDialogEvents* events = m_events;
    // the facade and the theme bindings die with the window
    m_handle = {};
    m_root = nullptr;
    m_events = nullptr;
    m_theme.reset();
    m_closing = false;
    // tell the presenter the surface is gone
    if (events != nullptr)
        events->on_dialog_closed();
}

SimulationConfig studio::AlborSimulationParametersDialogView::build_config() const {
    // start from the configuration the dialog opened with, so the untouched sections survive the edit
    SimulationConfig config = m_seed;
    // the selected tab names the analysis the fields describe
    const int tab = m_root != nullptr ? m_root->selected_tab() : 0;
    // a DC sweep replaces the analysis variant and keeps the companion sections
    if (tab == 1) {
        // the mode is the combo position, which the model bounds
        const int mode_index = std::clamp(m_root->dc_mode(), 0, static_cast<int>(DC_MODES.size()) - 1);
        // the stored spelling is what the serializer writes
        const std::string mode = DC_MODES[static_cast<std::size_t>(mode_index)];
        // the sweep tuples the configuration carries
        std::vector<DcSweep> sweeps;
        // a DATA sweep names a table instead of a range
        std::string table;
        // the range fields only apply to the modes that sweep a variable
        if (mode == "DATA") {
            // the table the DATA sweep reads is the only input
            table = trim(m_root->dc_table());
        }
        else {
            // the tuple the fields describe
            DcSweep sweep;
            // every mode but DATA names the swept variable
            sweep.variable = trim(m_root->dc_variable());
            // the linear and logarithmic modes carry a start and a stop
            if (mode == "LIN" || mode == "DEC" || mode == "OCT") {
                // the first value the sweep walks from
                sweep.start = trim(m_root->dc_start());
                // the last value the sweep walks to
                sweep.stop = trim(m_root->dc_stop());
            }
            // only a linear sweep carries a step size
            if (mode == "LIN")
                sweep.step = trim(m_root->dc_step());
            // the logarithmic modes carry a point count
            if (mode == "DEC" || mode == "OCT")
                sweep.points = trim(m_root->dc_points());
            // a LIST sweep carries the explicit values
            if (mode == "LIST")
                sweep.list_values = split_whitespace(m_root->dc_list());
            // the tuple joins the configuration
            sweeps.push_back(std::move(sweep));
        }
        // the companion sections are not edited here, so they carry over when the previous analysis was DC
        std::optional<PrintParameters> print;
        std::vector<MeasureEntry> measure;
        std::optional<SensParameter> sensitivity;
        std::optional<PceParameters> pce;
        if (const auto* previous = std::get_if<DCSimulationParameters>(&m_seed.analysis)) {
            // the print, measures, sensitivity and PCE survive the sweep edit
            print = previous->print_parameters;
            measure = previous->measure_parameters;
            sensitivity = previous->sensitivity;
            pce = previous->pce;
        }
        // the variant carries the edited sweep and the preserved companions
        config.analysis = DCSimulationParameters(mode, std::move(sweeps), std::move(table), std::move(print), std::move(measure), std::move(sensitivity), std::move(pce));
        // the stored spelling is what the run reports
        config.analysis_type = "DC";
        // the tab owns the ground flag the dialog edits
        config.replace_ground = m_root->dc_replace_ground();
        return config;
    }
    // an AC sweep replaces the analysis variant and keeps the companion sections
    if (tab == 2) {
        // the mode is the combo position, which the model bounds
        const int mode_index = std::clamp(m_root->ac_mode(), 0, static_cast<int>(AC_MODES.size()) - 1);
        // the stored spelling is what the serializer writes
        const std::string mode = AC_MODES[static_cast<std::size_t>(mode_index)];
        // the range fields the non DATA modes carry
        std::string points;
        std::string start;
        std::string end;
        // a DATA sweep names a table instead of a range
        std::string table;
        if (mode == "DATA") {
            // the table the DATA sweep reads is the only input
            table = trim(m_root->ac_table());
        }
        else {
            // the density of the sweep
            points = trim(m_root->ac_points());
            // the first frequency the sweep walks from
            start = trim(m_root->ac_start());
            // the last frequency the sweep walks to
            end = trim(m_root->ac_end());
        }
        // the companion sections are not edited here, so they carry over when the previous analysis was AC
        std::optional<PrintParameters> print;
        std::vector<MeasureEntry> measure;
        std::optional<SensParameter> sensitivity;
        if (const auto* previous = std::get_if<AcSimulationParameters>(&m_seed.analysis)) {
            // the print, measures and sensitivity survive the sweep edit
            print = previous->print_parameters;
            measure = previous->measure_parameters;
            sensitivity = previous->sensitivity;
        }
        // the variant carries the edited sweep and the preserved companions
        config.analysis = AcSimulationParameters(mode, std::move(points), std::move(start), std::move(end), std::move(table), std::move(print), std::move(measure), std::move(sensitivity));
        // the stored spelling is what the run reports
        config.analysis_type = "AC";
        // the tab owns the ground flag the dialog edits
        config.replace_ground = m_root->ac_replace_ground();
        return config;
    }
    // every other tab builds a transient analysis, which is the first tab
    const int op_index = std::clamp(m_root->tran_op_keyword(), 0, static_cast<int>(OP_KEYWORDS.size()) - 1);
    // the keyword is empty for the default entry
    const std::string op_keyword = OP_KEYWORDS[static_cast<std::size_t>(op_index)];
    // the companion sections are not edited here, so they carry over when the previous analysis was transient
    std::vector<TransientSchedulePoint> schedule;
    std::optional<PrintParameters> print;
    std::vector<FftParameters> fft;
    std::vector<FourParameters> four;
    std::vector<MeasureEntry> measure;
    std::optional<SensParameter> sensitivity;
    std::optional<PceParameters> pce;
    if (const auto* previous = std::get_if<TransientSimulationParameters>(&m_seed.analysis)) {
        // the schedule, print, transforms, measures, sensitivity and PCE survive the timing edit
        schedule = previous->schedule_points;
        print = previous->print_parameters;
        fft = previous->fft_parameters;
        four = previous->four_parameters;
        measure = previous->measure_parameters;
        sensitivity = previous->sensitivity;
        pce = previous->pce;
    }
    // the variant carries the edited timing and the preserved companions
    config.analysis = TransientSimulationParameters(trim(m_root->tran_initial_step()), trim(m_root->tran_final_time()), trim(m_root->tran_start_time()), trim(m_root->tran_step_ceiling()), op_keyword, std::move(schedule), std::move(print), std::move(fft), std::move(four), std::move(measure), std::move(sensitivity), std::move(pce));
    // the stored spelling is what the run reports
    config.analysis_type = "TRAN";
    // the tab owns the ground flag the dialog edits
    config.replace_ground = m_root->tran_replace_ground();
    return config;
}

void studio::AlborSimulationParametersDialogView::accept() {
    // an accept without a live dialog is stale
    if (m_root == nullptr || m_events == nullptr)
        return;
    // hand the configuration the fields describe to the presenter, which owns validation
    m_events->on_accept(build_config());
}

void studio::AlborSimulationParametersDialogView::dismiss() {
    // a dismiss without a presenter is stale
    if (m_events == nullptr)
        return;
    // forward the cancel request to the presenter
    m_events->on_cancel();
}

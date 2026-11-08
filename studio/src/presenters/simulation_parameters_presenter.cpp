#include "presenters/simulation_parameters_presenter.h"

#include <string_view>
#include <utility>
#include <variant>

#include "simulation/ac_simulation_parameters.h"
#include "simulation/dc_simulation_parameters.h"
#include "simulation/transient_simulation_parameters.h"

namespace
{
    // report whether a field holds nothing but whitespace, which the required rules treat as empty
    [[nodiscard]] bool is_blank(std::string_view value) { return value.find_first_not_of(" \t\r\n") == std::string_view::npos; }
} // namespace

void studio::SimulationParametersPresenter::show(SimulationParametersDialogView& view, const SimulationConfig& current, AcceptedHandler on_accepted) {
    // a second open request while the dialog is up is ignored
    if (m_view != nullptr)
        return;
    // remember the surface and the result destination for the dialog lifetime
    m_view = &view;
    m_on_accepted = std::move(on_accepted);
    // open the dialog seeded with the configuration a run currently uses
    view.show(current, *this);
}

void studio::SimulationParametersPresenter::on_accept(SimulationConfig config) {
    // an accept without an open dialog is stale
    if (m_view == nullptr)
        return;
    // a configuration the analysis rules refuse keeps the dialog open with the reason
    if (const std::optional<std::string> error = validate(config)) {
        m_view->show_error(*error);
        return;
    }
    // take the view and the callback out before closing, so the closed notification is a no-op
    SimulationParametersDialogView* view = m_view;
    AcceptedHandler on_accepted = std::move(m_on_accepted);
    m_view = nullptr;
    m_on_accepted = nullptr;
    // close the dialog before the shell observes the new configuration
    view->close();
    // deliver the accepted configuration to the shell
    if (on_accepted)
        on_accepted(config);
}

void studio::SimulationParametersPresenter::on_cancel() {
    // a cancel without an open dialog is stale
    if (m_view == nullptr)
        return;
    // take the view out before closing, so the closed notification is a no-op
    SimulationParametersDialogView* view = m_view;
    m_view = nullptr;
    m_on_accepted = nullptr;
    // close the dialog without changing the shell configuration
    view->close();
}

void studio::SimulationParametersPresenter::on_dialog_closed() {
    // the window is gone, so the next show is free to open a new one
    m_view = nullptr;
    m_on_accepted = nullptr;
}

std::optional<std::string> studio::SimulationParametersPresenter::validate(const SimulationConfig& config) {
    // a configuration without a chosen analysis cannot be launched
    if (std::holds_alternative<std::monostate>(config.analysis))
        return "Select an analysis";
    // the transient analysis requires the two values Xyce cannot infer
    if (const auto* transient = std::get_if<TransientSimulationParameters>(&config.analysis)) {
        // the initial step shapes the first iteration, so it must be written
        if (is_blank(transient->initial_step_value))
            return "Initial step is required";
        // the final time ends the run, so it must be written
        if (is_blank(transient->final_time_value))
            return "Final time is required";
    }
    // the DC analysis validates its sweep against the Xyce reference guide
    if (const auto* dc = std::get_if<DCSimulationParameters>(&config.analysis)) {
        // a DATA sweep names its table instead of the range fields
        if (dc->sweep_mode == "DATA") {
            // a DATA sweep without a table has nothing to read
            if (is_blank(dc->data_table_name))
                return "Data table is required for a DATA sweep";
        }
        else {
            // every other mode needs at least one sweep tuple
            if (dc->sweeps.empty())
                return "A DC sweep is required";
            // the core owns the sweep range rules, so the dialog reuses them
            if (const std::optional<std::string> error = dc->validate())
                return error;
        }
    }
    // the AC analysis requires its range unless a DATA table supplies it
    if (const auto* ac = std::get_if<AcSimulationParameters>(&config.analysis)) {
        // a DATA sweep names its table instead of the range fields
        if (ac->sweep_mode == "DATA") {
            // a DATA sweep without a table has nothing to read
            if (is_blank(ac->data_table_name))
                return "Data table is required for a DATA sweep";
        }
        else {
            // the point count is how dense the sweep is
            if (is_blank(ac->points))
                return "Points is required";
            // the start frequency opens the sweep
            if (is_blank(ac->start))
                return "Start frequency is required";
            // the end frequency closes the sweep
            if (is_blank(ac->end))
                return "Stop frequency is required";
        }
    }
    // the shared rules cover the steps a configuration may carry
    return config.validate();
}

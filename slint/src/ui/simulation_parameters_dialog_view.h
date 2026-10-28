#pragma once

#include <functional>
#include <memory>

#include <slint.h>

#include <main_window.h>

#include "main_window_view_def.h"
#include "simulation/simulation_config.h"

namespace simulation_parameters_dialog_view
{
    // owns the inline simulation parameters panel rendered inside the main window
    // (replacing the separate slint Dialog window). The panel is an inline child
    // of the MainWindow, so modality is enforced by the view's guard_modal gate
    // and the panel's own dimmed backdrop — no modal_manager input blocking is
    // needed. The accepted configuration is delivered to the presenter
    // asynchronously through MainWindowViewDefEvents. The view must be kept alive
    // for the lifetime of the presenter, so it lives in its own translation unit
    // (the generated main_window.h header defines a SharedGlobals type that
    // conflicts with other generated widget headers in the same compilation unit).
    class SimulationParametersDialogView
    {
    public:
        SimulationParametersDialogView(slint::ComponentHandle<main_window::MainWindow> main_window);

        SimulationParametersDialogView(const SimulationParametersDialogView&) = delete;
        SimulationParametersDialogView& operator=(const SimulationParametersDialogView&) = delete;

        ~SimulationParametersDialogView();

        // shows the inline panel seeded with the current configuration; when the
        // user accepts, the selected configuration is delivered to the given
        // handler via on_simulation_parameters_dialog_result. The on_closed
        // callback is invoked on both accept and cancel, after the panel is
        // hidden, so the caller can release the modal state.
        void show(const SimulationConfig& current, MainWindowViewDefEvents& handler, const std::function<void()>& on_closed);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace simulation_parameters_dialog_view

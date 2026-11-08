#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "albor/window/window_handle.h"
#include "simulation/simulation_config.h"
#include "views/simulation_parameters/simulation_parameters_dialog_view_def.h"

namespace albor
{
    class Application;
    class SimulationParametersDialog;
    class ThemeController;
} // namespace albor

namespace studio
{
    // the Albor window that renders the simulation parameters dialog
    class AlborSimulationParametersDialogView final : public SimulationParametersDialogView
    {
    public:
        AlborSimulationParametersDialogView(albor::Application& application, albor::WindowHandle parent);

        AlborSimulationParametersDialogView(const AlborSimulationParametersDialogView&) = delete;

        AlborSimulationParametersDialogView& operator=(const AlborSimulationParametersDialogView&) = delete;

        ~AlborSimulationParametersDialogView() override;

        void show(const SimulationConfig& current, SimulationParametersDialogEvents& events) override;

        void close() override;

        void show_error(std::string_view message) override;

        // report whether the dialog window is currently open
        [[nodiscard]] bool is_open() const;

        // clear the borrowed facade when the application reports the window is gone
        void on_window_closed(albor::WindowHandle handle);

    private:
        // read the visible fields into the configuration the presenter validates
        [[nodiscard]] SimulationConfig build_config() const;

        // forward the field values to the presenter
        void accept();

        // forward the cancel request to the presenter
        void dismiss();

        // the application that owns the window and its engines
        albor::Application* m_app = nullptr;

        // the shell window the dialog is parented to
        albor::WindowHandle m_parent;

        // the dialog window, invalid while it is closed
        albor::WindowHandle m_handle;

        // the borrowed generated facade
        albor::SimulationParametersDialog* m_root = nullptr;

        // the presenter that receives the outcome
        SimulationParametersDialogEvents* m_events = nullptr;

        // the theme bindings installed over the dialog tree
        std::unique_ptr<albor::ThemeController> m_theme;

        // the configuration the dialog opened with, which the untouched sections are copied from
        SimulationConfig m_seed = SimulationConfig::from_xyce_directives({});

        // whether a close request is already in flight
        bool m_closing = false;
    };
} // namespace studio

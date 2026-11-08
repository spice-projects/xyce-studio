#pragma once

#include <string_view>

#include "simulation/simulation_config.h"

namespace studio
{
    // the interactions the simulation parameters dialog reports to its presenter
    class SimulationParametersDialogEvents
    {
    public:
        virtual ~SimulationParametersDialogEvents() = default;

        // the user confirmed the dialog, which hands over the configuration the fields describe
        virtual void on_accept(SimulationConfig config) = 0;

        // the user cancelled the dialog
        virtual void on_cancel() = 0;

        // the window disappeared without an accept or a cancel
        virtual void on_dialog_closed() = 0;
    };

    // the surface the simulation parameters presenter drives, abstracted from the window toolkit
    class SimulationParametersDialogView
    {
    public:
        virtual ~SimulationParametersDialogView() = default;

        // open the dialog seeded with the configuration a run currently uses
        virtual void show(const SimulationConfig& current, SimulationParametersDialogEvents& events) = 0;

        // close the dialog without continuing the interaction
        virtual void close() = 0;

        // report a validation message and keep the dialog open
        virtual void show_error(std::string_view message) = 0;
    };
} // namespace studio

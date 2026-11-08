#pragma once

#include <functional>
#include <optional>
#include <string>

#include "simulation/simulation_config.h"
#include "views/simulation_parameters/simulation_parameters_dialog_view_def.h"

namespace studio
{
    // validates the analysis a dialog describes and hands the accepted configuration to the shell
    class SimulationParametersPresenter final : public SimulationParametersDialogEvents
    {
    public:
        // notified with the configuration the user accepted
        using AcceptedHandler = std::function<void(const SimulationConfig&)>;

        // open the dialog seeded with the configuration a run currently uses; a second call while it is open is ignored
        void show(SimulationParametersDialogView& view, const SimulationConfig& current, AcceptedHandler on_accepted);

        void on_accept(SimulationConfig config) override;

        void on_cancel() override;

        void on_dialog_closed() override;

    private:
        // check the analysis the dialog produced, returning the user facing error or nullopt when valid
        [[nodiscard]] static std::optional<std::string> validate(const SimulationConfig& config);

        // the dialog being driven, null while no dialog is open
        SimulationParametersDialogView* m_view = nullptr;

        // the shell destination for an accepted configuration
        AcceptedHandler m_on_accepted;
    };
} // namespace studio

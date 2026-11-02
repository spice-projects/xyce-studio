#pragma once

#include <functional>
#include <string>

#include "config/plugin_config.h"
#include "views/plugin_config/plugin_config_dialog_view_def.h"

namespace studio
{
    // owns the validation and persistence of the plugin configuration, and drives one dialog at a time
    class PluginConfigPresenter final : public PluginConfigDialogEvents
    {
    public:
        // notified with the configuration that was accepted and saved
        using SavedHandler = std::function<void(const PluginConfig&)>;

        // open the dialog seeded with the current configuration; a second call while it is open is ignored
        void show(PluginConfigDialogView& view, const PluginConfig& current, SavedHandler on_saved);

        void on_accept(std::string path) override;

        void on_cancel() override;

        void on_dialog_closed() override;

    private:
        // the dialog being driven, null while no dialog is open
        PluginConfigDialogView* m_view = nullptr;

        // the shell destination for an accepted configuration
        SavedHandler m_on_saved;
    };
} // namespace studio

#pragma once

#include <functional>
#include <memory>

#include <slint.h>

#include <main_window.h>

#include "config/plugin_config.h"
#include "main_window_view_def.h"

namespace plugin_config_dialog_view
{
    // owns the inline plugin configuration panel rendered inside the main window.
    // seeded with the current configuration and shown on demand. Because the
    // panel is a child of the main window (not a separate Dialog / OS window),
    // modality is enforced by the view's guard_modal gate and the panel's own
    // dimmed backdrop — no modal_manager input blocking is required. The accepted
    // and validated configuration is persisted here and delivered to the
    // presenter asynchronously through MainWindowViewDefEvents. The view is kept
    // alive for the lifetime of the viewer, so it lives in its own translation
    // unit (the generated main_window.h header defines a SharedGlobals type that
    // conflicts with other generated widget headers in the same compilation unit).
    class PluginConfigDialogView
    {
    public:
        PluginConfigDialogView(slint::ComponentHandle<main_window::MainWindow> main_window);

        PluginConfigDialogView(const PluginConfigDialogView&) = delete;
        PluginConfigDialogView& operator=(const PluginConfigDialogView&) = delete;

        ~PluginConfigDialogView();

        // shows the panel seeded with the current configuration; when the user
        // accepts a valid path, the configuration is persisted and delivered to
        // the given handler via on_plugin_config_dialog_result. The on_closed
        // callback is invoked on both accept and cancel, after the panel is
        // hidden, so the caller can release the modal state.
        void show(const PluginConfig& current, MainWindowViewDefEvents& handler, const std::function<void()>& on_closed);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace plugin_config_dialog_view

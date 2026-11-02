#pragma once

#include <memory>
#include <string_view>

#include "albor/window/window_handle.h"
#include "views/plugin_config/plugin_config_dialog_view_def.h"

namespace albor
{
    class Application;
    class PluginConfigDialog;
    class ThemeController;
} // namespace albor

namespace studio
{
    // the Albor window that renders the plugin configuration dialog
    class AlborPluginConfigDialogView final : public PluginConfigDialogView
    {
    public:
        AlborPluginConfigDialogView(albor::Application& application, albor::WindowHandle parent);

        AlborPluginConfigDialogView(const AlborPluginConfigDialogView&) = delete;

        AlborPluginConfigDialogView& operator=(const AlborPluginConfigDialogView&) = delete;

        ~AlborPluginConfigDialogView() override;

        void show(std::string_view current_path, PluginConfigDialogEvents& events) override;

        void close() override;

        void show_error(std::string_view message) override;

        // report whether the dialog window is currently open
        [[nodiscard]] bool is_open() const;

        // clear the borrowed facade when the application reports the window is gone
        void on_window_closed(albor::WindowHandle handle);

    private:
        // open the native picker and push the choice into the field
        void browse();

        // forward the field value to the presenter
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
        albor::PluginConfigDialog* m_root = nullptr;

        // the presenter that receives the outcome
        PluginConfigDialogEvents* m_events = nullptr;

        // the theme bindings installed over the dialog tree
        std::unique_ptr<albor::ThemeController> m_theme;

        // whether a close request is already in flight
        bool m_closing = false;
    };
} // namespace studio

#pragma once

#include <string>
#include <string_view>

namespace studio
{
    // the outcomes a plugin configuration dialog reports to its presenter
    class PluginConfigDialogEvents
    {
    public:
        virtual ~PluginConfigDialogEvents() = default;

        // the user accepted the dialog with the raw field value
        virtual void on_accept(std::string path) = 0;

        // the user cancelled the dialog
        virtual void on_cancel() = 0;

        // the dialog window is gone, whether accepted, cancelled or dismissed natively
        virtual void on_dialog_closed() = 0;
    };

    // the surface the plugin configuration presenter drives, abstracted from the window toolkit
    class PluginConfigDialogView
    {
    public:
        virtual ~PluginConfigDialogView() = default;

        // open the dialog seeded with the current path; the events object outlives the dialog
        virtual void show(std::string_view current_path, PluginConfigDialogEvents& events) = 0;

        // close the dialog window
        virtual void close() = 0;

        // show a validation message inside the dialog
        virtual void show_error(std::string_view message) = 0;
    };
} // namespace studio

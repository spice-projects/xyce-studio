#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace studio
{
    // the interactions the shell reports to its presenter
    class MainWindowViewEvents
    {
    public:
        virtual ~MainWindowViewEvents() = default;

        // the user picked a file in the open panel, which the view resolved
        virtual void on_open_file(const std::filesystem::path& path) = 0;

        // the user asked to save the netlist
        virtual void on_save() = 0;

        // the user edited the netlist text
        virtual void on_netlist_edited() = 0;
    };

    // the surface the main window presenter drives, abstracted from the window toolkit
    class MainWindowViewDef
    {
    public:
        virtual ~MainWindowViewDef() = default;

        // install the presenter that receives the shell interactions; null clears it
        virtual void set_event_handler(MainWindowViewEvents* handler) = 0;

        // replace the netlist text the editor edits, which seeds the surface without reporting an edit
        virtual void set_netlist_text(std::string_view content) = 0;

        // hand out the netlist text the editor currently edits
        [[nodiscard]] virtual std::string netlist_text() const = 0;

        // set or clear the editor read only state
        virtual void set_netlist_read_only(bool read_only) = 0;

        // replace the one line status the shell reports
        virtual void set_status_text(std::string_view text) = 0;

        // replace the window title, which the presenter drives from the netlist state
        virtual void set_title(std::string_view title) = 0;

        // enable or disable the open action
        virtual void set_open_enabled(bool enabled) = 0;

        // enable or disable the save action
        virtual void set_save_enabled(bool enabled) = 0;

        // ask the user for the save location and filename of an untitled netlist
        [[nodiscard]] virtual std::optional<std::filesystem::path> request_netlist_save_path() = 0;
    };
} // namespace studio

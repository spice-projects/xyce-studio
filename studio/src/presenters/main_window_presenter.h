#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "netlist/netlist_source.h"
#include "views/main_window/main_window_view_def.h"

namespace studio
{
    // owns the netlist lifecycle of the shell: open, save and the unsaved
    // changes marker; the simulation and chart state arrive in later slices
    class MainWindowPresenter final : public MainWindowViewEvents
    {
    public:
        // the shell surface must outlive the presenter
        explicit MainWindowPresenter(MainWindowViewDef& view);

        MainWindowPresenter(const MainWindowPresenter&) = delete;

        MainWindowPresenter& operator=(const MainWindowPresenter&) = delete;

        // the user picked a file in the open panel, which the view resolved
        void on_open_file(const std::filesystem::path& path) override;

        // the user asked to save the netlist
        void on_save() override;

        // the user edited the netlist text
        void on_netlist_edited() override;

    private:
        // re-apply the toolbar action enablement for the current state
        void refresh_action_states();

        // store the dirty state and re-render the title with the marker
        bool set_netlist_editor_dirty(bool flag);

        // store the clean title and re-render the title
        void set_base_title(const std::string& title);

        // set the editor content and the tracked dirty flag
        bool update_netlist_editor_content(const std::string& content, bool dirty_flag);

        // the shell surface the presenter drives
        MainWindowViewDef* m_view;

        // the source the netlist loads from and saves into
        std::unique_ptr<NetlistSource> m_netlist_source;

        // the clean title without the unsaved changes marker
        std::string m_base_title;

        // whether the editor holds unsaved changes
        bool m_netlist_editor_dirty = false;

        // whether the editor holds content
        bool m_netlist_has_content = false;
    };
} // namespace studio

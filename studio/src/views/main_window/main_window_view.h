#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "albor/window/window_handle.h"
#include "views/main_window/main_window_view_def.h"

namespace albor
{
    class Application;
    class MainWindow;
    class ThemeController;
} // namespace albor

namespace studio
{
    // the Albor shell window; it implements the surface the presenter drives
    // and resolves the native file panels itself
    class MainWindowView final : public MainWindowViewDef
    {
    public:
        explicit MainWindowView(albor::Application& application);

        MainWindowView(const MainWindowView&) = delete;

        MainWindowView& operator=(const MainWindowView&) = delete;

        ~MainWindowView() override;

        // create and show the shell window, returning false when the platform refused it
        [[nodiscard]] bool create();

        // hand out the window handle the dialog parent is resolved from
        [[nodiscard]] albor::WindowHandle handle() const;

        // install the handler the plugin settings action reaches
        void set_on_plugin_settings(std::function<void()> handler);

        // clear the borrowed facade when the application reports the window is gone
        void on_window_closed(albor::WindowHandle handle);

        // install the presenter that receives the shell interactions; null clears it
        void set_event_handler(MainWindowViewEvents* handler) override;

        // replace the netlist text the editor edits, which seeds the surface without reporting an edit
        void set_netlist_text(std::string_view content) override;

        // hand out the netlist text the editor currently edits
        [[nodiscard]] std::string netlist_text() const override;

        // set or clear the editor read only state
        void set_netlist_read_only(bool read_only) override;

        // replace the one line status the shell reports
        void set_status_text(std::string_view text) override;

        // replace the window title, which the presenter drives from the netlist state
        void set_title(std::string_view title) override;

        // enable or disable the open action
        void set_open_enabled(bool enabled) override;

        // enable or disable the save action
        void set_save_enabled(bool enabled) override;

        // ask the user for the save location and filename of an untitled netlist
        [[nodiscard]] std::optional<std::filesystem::path> request_netlist_save_path() override;

    private:
        // run the open panel and hand the chosen file to the presenter
        void handle_open();

        // run the open panel over the given options, reporting the choice or nothing
        [[nodiscard]] std::optional<std::filesystem::path> run_open_panel(const std::string& title);

        // the application that owns the window and its engines
        albor::Application* m_app = nullptr;

        // the shell window, invalid while it is closed
        albor::WindowHandle m_handle;

        // the borrowed generated facade
        albor::MainWindow* m_root = nullptr;

        // the presenter that receives the shell interactions, null while none is wired
        MainWindowViewEvents* m_events = nullptr;

        // the theme bindings installed over the shell tree
        std::unique_ptr<albor::ThemeController> m_theme;
    };
} // namespace studio

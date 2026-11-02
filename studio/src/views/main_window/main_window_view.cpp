#include "views/main_window/main_window_view.h"

#include <filesystem>
#include <optional>
#include <utility>

#include "albor/application/application.h"
#include "albor/application/file_dialog.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "main_window.h"

namespace
{
    // build the filter the open panel offers, which is the netlist format plus
    // every analysis output format the presenter dispatches by extension
    std::vector<albor::FileFilter> open_filters() { return {albor::FileFilter{"Xyce Files", {"cir", "raw", "prn", "csd", "csv", "dat"}}}; }
} // namespace

studio::MainWindowView::MainWindowView(albor::Application& application) :
    m_app(&application) {}

studio::MainWindowView::~MainWindowView() = default;

bool studio::MainWindowView::create() {
    // describe the resizable shell the editor and charts will fill
    albor::WindowOptions options;
    options.title = "Xyce Studio";
    options.size = albor::Size{1200.0f, 800.0f};
    options.role = albor::WindowRole::Normal;
    options.resizable = true;
    options.visible = false;
    // build the compiled shell tree in a fresh window
    m_handle = m_app->create_window(options, m_root);
    // a refused window leaves nothing to theme or show
    if (!m_handle.valid() || m_root == nullptr) {
        m_handle = {};
        m_root = nullptr;
        return false;
    }
    // theme the tree before the first frame so the window never flashes an unthemed surface
    m_theme = install_theme(*m_app, m_handle);
    // show the fully built and themed window
    if (albor::Window* window = m_app->window(m_handle))
        window->show();
    // the shell is ready
    return true;
}

albor::WindowHandle studio::MainWindowView::handle() const { return m_handle; }

void studio::MainWindowView::set_on_plugin_settings(std::function<void()> handler) {
    // without a live facade there is no signal to bind
    if (m_root == nullptr)
        return;
    // forward the toolbar action to the shell
    m_root->set_on_plugin_settings_clicked(std::move(handler));
}

void studio::MainWindowView::on_window_closed(albor::WindowHandle handle) {
    // ignore windows this view does not own
    if (handle != m_handle)
        return;
    // the facade and its theme bindings die with the window
    m_handle = {};
    m_root = nullptr;
    m_theme.reset();
}

void studio::MainWindowView::set_event_handler(MainWindowViewEvents* handler) {
    // remember the presenter the shell reports to
    m_events = handler;
    // without a live facade there is nothing to bind
    if (m_root == nullptr)
        return;
    // the open action resolves the native panel before the presenter sees it
    m_root->set_on_open_clicked([this] { handle_open(); });
    // the save action reaches the presenter directly
    m_root->set_on_save_clicked([this] {
        if (m_events != nullptr)
            m_events->on_save();
    });
    // an edit reports through the same seam
    m_root->set_on_netlist_edited([this] {
        if (m_events != nullptr)
            m_events->on_netlist_edited();
    });
}

void studio::MainWindowView::set_netlist_text(std::string_view content) {
    // without a live facade there is nothing to update
    if (m_root == nullptr)
        return;
    // the binding seeds the editor without reporting an edit
    m_root->set_netlist_text(std::string(content));
}

std::string studio::MainWindowView::netlist_text() const {
    // a closed shell edits nothing
    if (m_root == nullptr)
        return {};
    // the facade property carries what the editor edits
    return m_root->netlist_text();
}

void studio::MainWindowView::set_netlist_read_only(bool read_only) {
    // without a live facade there is nothing to update
    if (m_root == nullptr)
        return;
    // the editor refuses edits while the state holds
    m_root->set_netlist_read_only(read_only);
}

void studio::MainWindowView::set_status_text(std::string_view text) {
    // without a live facade there is nothing to update
    if (m_root == nullptr)
        return;
    // the status row carries the one line the shell reports
    m_root->set_status(std::string(text));
}

void studio::MainWindowView::set_title(std::string_view title) {
    // a closed window has no title to replace
    if (!m_handle.valid())
        return;
    // the platform window carries the title
    if (albor::Window* window = m_app->window(m_handle))
        window->set_title(title);
}

void studio::MainWindowView::set_open_enabled(bool enabled) {
    // without a live facade there is nothing to update
    if (m_root == nullptr)
        return;
    // the toolbar button refuses interaction while disabled
    m_root->set_open_enabled(enabled);
}

void studio::MainWindowView::set_save_enabled(bool enabled) {
    // without a live facade there is nothing to update
    if (m_root == nullptr)
        return;
    // the toolbar button refuses interaction while disabled
    m_root->set_save_enabled(enabled);
}

std::optional<std::filesystem::path> studio::MainWindowView::request_netlist_save_path() {
    // a closed shell cannot present a panel
    if (!m_handle.valid())
        return std::nullopt;
    // describe the native save panel, owned by the shell so it presents as its sheet
    albor::SaveFileOptions options;
    options.parent = m_handle;
    options.title = "Save Netlist";
    options.filters = {albor::FileFilter{"Xyce Netlist", {"cir"}}};
    options.default_name = "untitled.cir";
    // the chosen path or the cancelled read
    return m_app->save_file(options);
}

std::optional<std::filesystem::path> studio::MainWindowView::run_open_panel(const std::string& title) {
    // a closed shell cannot present a panel
    if (!m_handle.valid())
        return std::nullopt;
    // describe the native open panel, owned by the shell so it presents as its sheet
    albor::OpenFileOptions options;
    options.parent = m_handle;
    options.title = title;
    options.filters = open_filters();
    // the chosen path or the cancelled read
    return m_app->open_file(options);
}

void studio::MainWindowView::handle_open() {
    // run the open panel for a Xyce file
    const std::optional<std::filesystem::path> path = run_open_panel("Open Xyce File");
    // a cancelled panel reports nothing
    if (!path.has_value() || m_events == nullptr)
        return;
    // hand the chosen file to the presenter
    m_events->on_open_file(*path);
}

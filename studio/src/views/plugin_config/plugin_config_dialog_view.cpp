#include "views/plugin_config/plugin_config_dialog_view.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "albor/application/application.h"
#include "albor/application/file_dialog.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "plugin_config_dialog.h"

namespace
{
    // convert a filesystem path to the utf8 string the field carries
    std::string to_utf8(const std::filesystem::path& path) {
        // the u8 view is the encoding every platform shares
        const std::u8string value = path.u8string();
        // reinterpret the code units as the byte string the ui stores
        return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    }
} // namespace

studio::AlborPluginConfigDialogView::AlborPluginConfigDialogView(albor::Application& application, albor::WindowHandle parent) :
    m_app(&application), m_parent(parent) {}

studio::AlborPluginConfigDialogView::~AlborPluginConfigDialogView() = default;

void studio::AlborPluginConfigDialogView::show(std::string_view current_path, PluginConfigDialogEvents& events) {
    // a second show while the window is up is ignored
    if (is_open())
        return;
    // remember the presenter that receives the outcome
    m_events = &events;
    // the new window starts with a clean close state
    m_closing = false;
    // describe a fixed size modal dialog owned by the shell
    albor::WindowOptions options;
    options.title = "Plugin Configuration";
    options.size = albor::Size{560.0f, 230.0f};
    options.role = albor::WindowRole::Dialog;
    options.modality = albor::Modality::WindowModal;
    options.parent = m_parent;
    options.resizable = false;
    options.visible = false;
    // build the compiled dialog tree in a fresh window
    m_handle = m_app->create_window(options, m_root);
    // a refused window leaves the presenter without a surface
    if (!m_handle.valid() || m_root == nullptr) {
        m_handle = {};
        m_root = nullptr;
        m_events = nullptr;
        events.on_dialog_closed();
        return;
    }
    // theme the dialog from the same system appearance as the shell
    m_theme = install_theme(*m_app, m_handle);
    // seed the field with the current configuration
    m_root->set_path(std::string(current_path));
    // clear any stale validation feedback
    m_root->set_show_error(false);
    m_root->set_error_message("");
    // forward the dialog actions to this view
    m_root->set_on_browse_clicked([this] { browse(); });
    m_root->set_on_accepted([this] { accept(); });
    m_root->set_on_dismissed([this] { dismiss(); });
    // show the fully seeded window, which avoids a blank first frame
    if (albor::Window* window = m_app->window(m_handle))
        window->show();
}

void studio::AlborPluginConfigDialogView::close() {
    // a close with no open window is a no-op
    if (!m_handle.valid())
        return;
    // a second close waits for the first teardown
    if (m_closing)
        return;
    // record the close so the closed notification knows it was requested
    m_closing = true;
    // ask the application to close the window, which consults the veto and defers an input driven teardown
    (void)m_app->request_close(m_handle);
}

void studio::AlborPluginConfigDialogView::show_error(std::string_view message) {
    // without a live facade there is nothing to update
    if (m_root == nullptr)
        return;
    // carry the message the validation produced
    m_root->set_error_message(std::string(message));
    // show the error row in the layout
    m_root->set_show_error(true);
}

bool studio::AlborPluginConfigDialogView::is_open() const { return m_handle.valid(); }

void studio::AlborPluginConfigDialogView::on_window_closed(albor::WindowHandle handle) {
    // ignore windows this view does not own
    if (handle != m_handle)
        return;
    // take the presenter out before clearing, because the notification may reset it
    PluginConfigDialogEvents* events = m_events;
    // the facade and the theme bindings die with the window
    m_handle = {};
    m_root = nullptr;
    m_events = nullptr;
    m_theme.reset();
    m_closing = false;
    // tell the presenter the surface is gone
    if (events != nullptr)
        events->on_dialog_closed();
}

void studio::AlborPluginConfigDialogView::browse() {
    // a browse without a live dialog is stale
    if (m_root == nullptr)
        return;
    // describe the native open panel, owned by the dialog so it presents as its sheet
    albor::OpenFileOptions options;
    options.parent = m_handle;
    options.title = "Select Xyce Executable";
    // open the native picker for an executable
    const std::optional<std::filesystem::path> path = m_app->open_file(options);
    // a cancelled picker leaves the field untouched
    if (!path.has_value())
        return;
    // push the selected path into the field
    m_root->set_path(to_utf8(*path));
    // a new selection clears the previous validation message
    m_root->set_show_error(false);
}

void studio::AlborPluginConfigDialogView::accept() {
    // an accept without a live dialog is stale
    if (m_root == nullptr || m_events == nullptr)
        return;
    // hand the raw field value to the presenter, which owns validation
    m_events->on_accept(m_root->path());
}

void studio::AlborPluginConfigDialogView::dismiss() {
    // a dismiss without a presenter is stale
    if (m_events == nullptr)
        return;
    // forward the cancel request to the presenter
    m_events->on_cancel();
}

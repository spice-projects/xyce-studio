#include <string>

#include <slint.h>

#include <main_window.h>

#include "config/plugin_config.h"
#include "file_dialog.h"
#include "main_window_view_def.h"
#include "plugin_config_dialog_view.h"

namespace plugin_config_dialog_view
{
    namespace
    {
        // validation feedback strings
        constexpr const char* REQUIRED_ERROR = "Xyce executable path is required";
        constexpr const char* INVALID_ERROR = "Selected path is not an executable file";
    } // namespace

    struct PluginConfigDialogView::Impl
    {
        // the main window handle; the panel is an inline child of the window,
        // so all interaction goes through the window's properties and callbacks
        slint::ComponentHandle<main_window::MainWindow> window;

        // presenter notified with the accepted configuration
        MainWindowViewDefEvents* handler = nullptr;

        // notified on both accept and cancel, after the panel is hidden;
        // the caller releases the modal state from here
        std::function<void()> on_closed;

        Impl(slint::ComponentHandle<main_window::MainWindow> w) :
            window(w) {
            // wire the forwarded callbacks from the inline panel to this view
            window->on_plugin_config_browse_clicked([this] { browse(); });
            window->on_plugin_config_accepted([this] { accept(); });
            window->on_plugin_config_dismissed([this] { dismiss(); });
        }

        void browse() {
            // open the native file picker for the Xyce executable
            const auto path = FileDialog::open_xyce_executable();
            if (!path.has_value())
                return;
            // push the selected path into the field
            window->set_plugin_config_xyce_path(slint::SharedString(path->string()));
            // clear any previous validation message after a new selection
            window->set_plugin_config_show_error(false);
        }

        void accept() {
            // read the path and trim surrounding whitespace
            const std::string raw = std::string(window->get_plugin_config_xyce_path());
            // check non-empty path
            if (const auto first = raw.find_first_not_of(" \t\r\n"); first != std::string::npos) {
                // trim
                const auto last = raw.find_last_not_of(" \t\r\n");
                const PluginConfig config(raw.substr(first, last - first + 1));
                // reject path values that are not executable files
                if (!config.is_xyce_executable_valid()) {
                    // show error message
                    window->set_plugin_config_error_message(slint::SharedString(INVALID_ERROR));
                    window->set_plugin_config_show_error(true);
                    // exit
                    return;
                }
                // persist the validated configuration
                config.save();
                // hide the panel before delivering the result
                window->set_plugin_config_visible(false);
                // release the modal state held by the caller
                if (on_closed)
                    on_closed();
                // deliver the updated configuration to the presenter
                if (handler != nullptr)
                    handler->on_plugin_config_dialog_result(config);
                // exit
                return;
            }
            // require a non-empty path so the plugin can launch Xyce
            window->set_plugin_config_error_message(slint::SharedString(REQUIRED_ERROR));
            window->set_plugin_config_show_error(true);
        }

        void dismiss() {
            // hide the panel and drop the pending state
            window->set_plugin_config_visible(false);
            // release the modal state held by the caller
            if (on_closed)
                on_closed();
        }
    };

    PluginConfigDialogView::PluginConfigDialogView(slint::ComponentHandle<main_window::MainWindow> main_window) :
        m_impl(std::make_unique<Impl>(main_window)) {}

    PluginConfigDialogView::~PluginConfigDialogView() = default;

    void PluginConfigDialogView::show(const PluginConfig& current, MainWindowViewDefEvents& handler, const std::function<void()>& on_closed) {
        // remember the result destination for the panel lifetime
        m_impl->handler = &handler;
        // remember the close notification for this show
        m_impl->on_closed = on_closed;
        // seed the path field with the current configuration
        m_impl->window->set_plugin_config_xyce_path(slint::SharedString(current.xyce_executable_path()));
        // clear any previous validation feedback
        m_impl->window->set_plugin_config_show_error(false);
        // show the inline panel
        m_impl->window->set_plugin_config_visible(true);
    }
} // namespace plugin_config_dialog_view

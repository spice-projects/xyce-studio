#include <optional>
#include <string>
#include <utility>

#include <slint.h>
#include <spdlog/spdlog.h>

#include <main_window.h>

#include "../config/capability_probe_runner.h"
#include "../config/parallel_configuration.h"
#include "../config/plugin_config.h"
#include "../config/xyce_capabilities.h"
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
        constexpr const char* LAUNCHER_REQUIRED_ERROR = "MPI launcher path is required for a parallel run";
        constexpr const char* LAUNCHER_INVALID_ERROR = "Selected MPI launcher is not an executable file";
        // the launcher ran but offered too few slots for a parallel run
        constexpr const char* LAUNCHER_TOO_FEW_SLOTS_ERROR = "The MPI launcher reports fewer than two slots, so a parallel run cannot start";
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

        // runs the capability probes off the ui thread and supersedes a round
        // whose path was replaced while it was still running
        CapabilityProbeRunner probe_runner;

        Impl(slint::ComponentHandle<main_window::MainWindow> w) :
            window(w) {
            // wire the forwarded callbacks from the inline panel to this view
            window->on_plugin_config_browse_clicked([this] { browse(); });
            window->on_plugin_config_accepted([this] { accept(); });
            window->on_plugin_config_dismissed([this] { dismiss(); });
            window->on_plugin_config_launcher_browse_clicked([this] { browse_launcher(); });
            // a completed round arrives on the ui thread and is pushed onto the panel there
            probe_runner.set_outcome_callback([this](const CapabilityProbeOutcome& outcome) { slint::invoke_from_event_loop([this, outcome] { apply_probe_result(outcome); }); });
        }

        // push a described panel state onto the window
        void apply_panel_state(const CapabilityPanelState& state) {
            // the capability line the panel displays
            window->set_plugin_config_capabilities_text(slint::SharedString(state.text));
            // a binary that reports MPI support shows the parallel section
            window->set_plugin_config_parallel_capable(state.parallel_capable);
            // an unclassifiable binary is called out separately from a serial one
            window->set_plugin_config_capabilities_unknown(state.capabilities_unknown);
            // a serial or unclassifiable binary cannot keep a parallel request
            window->set_plugin_config_parallel_enabled(state.parallel_enabled);
        }

        // ask the configured binary what it was built with, off the ui thread
        void start_capability_probe(const std::string& xyce_path, const std::string& launcher_path) {
            // show the pending state while the probe runs, carrying the current
            // request through so the probe cannot silently clear what was seeded
            apply_panel_state(describe_pending_capabilities(window->get_plugin_config_parallel_enabled()));
            // start the round, which supersedes any round still in flight
            probe_runner.start(xyce_path, launcher_path);
        }

        // push a completed probe onto the panel, ignoring a result whose path was
        // replaced while the probe was in flight
        void apply_probe_result(const CapabilityProbeOutcome& outcome) {
            // a newer probe already replaced this one
            if (outcome.generation != probe_runner.generation())
                return;
            // the panel was closed while the probe was in flight
            if (!window->get_plugin_config_visible())
                return;
            // report the classification, which also drops a request a serial binary cannot honour
            apply_panel_state(describe_capabilities(outcome.capabilities, window->get_plugin_config_parallel_enabled()));
            // the slot count only means something for a parallel-capable binary
            window->set_plugin_config_slots_known(outcome.available_slots.has_value());
            window->set_plugin_config_available_slots(outcome.available_slots.has_value() ? static_cast<int>(*outcome.available_slots) : 0);
            // the launcher banner, empty when the launcher could not identify itself
            window->set_plugin_config_launcher_version(outcome.launcher_version.has_value() ? slint::SharedString(*outcome.launcher_version) : slint::SharedString(""));
        }

        void browse() {
            // open the native file picker for the Xyce executable
            const auto path = FileDialog::open_xyce_executable();
            // a cancelled picker leaves the current binary in place
            if (!path.has_value())
                return;
            // push the selected path into the field
            window->set_plugin_config_xyce_path(slint::SharedString(path->string()));
            // clear any previous validation message after a new selection
            window->set_plugin_config_show_error(false);
            // a different binary may report different capabilities
            start_capability_probe(path->string(), std::string(window->get_plugin_config_launcher_path()));
        }

        void browse_launcher() {
            // open the native file picker for the MPI launcher
            const auto path = FileDialog::open_mpi_launcher();
            // a cancelled picker leaves the current launcher in place
            if (!path.has_value())
                return;
            // push the selected path into the field
            window->set_plugin_config_launcher_path(slint::SharedString(path->string()));
            // clear any previous validation message after a new selection
            window->set_plugin_config_show_error(false);
            // the slot count and the launcher banner belong to this launcher
            start_capability_probe(std::string(window->get_plugin_config_xyce_path()), path->string());
        }

        void accept() {
            // the configuration the submitted form describes
            PluginConfig config;
            // validate the form and build the configuration it asks for
            const auto rejection = build_form_config(std::string(window->get_plugin_config_xyce_path()), window->get_plugin_config_parallel_enabled(), window->get_plugin_config_parallel_capable(), std::string(window->get_plugin_config_launcher_path()), window->get_plugin_config_process_count(), window->get_plugin_config_slots_known(), window->get_plugin_config_available_slots(), std::string(window->get_plugin_config_launcher_arguments()), config);
            // reject a form that does not describe a usable configuration
            if (rejection != ParallelFormRejection::None) {
                // the message each rejection reports to the user
                switch (rejection) {
                case ParallelFormRejection::XyceMissing:
                    window->set_plugin_config_error_message(slint::SharedString(REQUIRED_ERROR));
                    break;
                case ParallelFormRejection::XyceInvalid:
                    window->set_plugin_config_error_message(slint::SharedString(INVALID_ERROR));
                    break;
                case ParallelFormRejection::LauncherMissing:
                    window->set_plugin_config_error_message(slint::SharedString(LAUNCHER_REQUIRED_ERROR));
                    break;
                case ParallelFormRejection::LauncherInvalid:
                    window->set_plugin_config_error_message(slint::SharedString(LAUNCHER_INVALID_ERROR));
                    break;
                case ParallelFormRejection::LauncherTooFewSlots:
                    window->set_plugin_config_error_message(slint::SharedString(LAUNCHER_TOO_FEW_SLOTS_ERROR));
                    break;
                case ParallelFormRejection::None:
                    break;
                }
                // show the error message
                window->set_plugin_config_show_error(true);
                // exit, keeping the panel open so the user can correct the form
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
        }

        void dismiss() {
            // supersede any probe still in flight for the path being abandoned
            probe_runner.cancel();
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
        // seed the parallel request from the current configuration
        m_impl->window->set_plugin_config_parallel_enabled(current.parallel_enabled());
        // fall back to the discovered launcher when the configuration holds none
        const auto launcher = current.mpi_launcher_path().empty() ? discover_mpi_launcher() : current.mpi_launcher_path();
        m_impl->window->set_plugin_config_launcher_path(slint::SharedString(launcher));
        // seed the process count, a configuration written by an older build may still hold the serial default
        m_impl->window->set_plugin_config_process_count(current.process_count() >= 2 ? static_cast<int>(current.process_count()) : static_cast<int>(DEFAULT_PROCESS_COUNT));
        // seed the additional launcher arguments
        m_impl->window->set_plugin_config_launcher_arguments(slint::SharedString(current.mpi_launcher_arguments()));
        // the slot count is unknown until the probe below reports it
        m_impl->window->set_plugin_config_slots_known(false);
        m_impl->window->set_plugin_config_available_slots(0);
        // the launcher banner arrives with the probe result
        m_impl->window->set_plugin_config_launcher_version(slint::SharedString(""));
        // clear any previous validation feedback
        m_impl->window->set_plugin_config_show_error(false);
        // show the inline panel
        m_impl->window->set_plugin_config_visible(true);
        // ask the configured binary what it was built with, off the ui thread
        m_impl->start_capability_probe(current.xyce_executable_path(), launcher);
    }
} // namespace plugin_config_dialog_view

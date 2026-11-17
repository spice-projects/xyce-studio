#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "albor/application/application.h"
#include "albor/text/text_engine.h"
#include "albor_embedded_fonts.h"
#include "app/schematic_session.h"
#include "app/startup_options.h"
#include "app/studio_app.h"
#include "app/window_instance.h"
#include "config/plugin_config.h"
#include "core/util.h"
#include "kicad/kicad_session.h"

namespace
{
    // apply the verbosity the command line named, which defaults to the info level
    void apply_log_level(const std::string& level) {
        // the debug level narrows nothing
        if (level == "debug") {
            spdlog::set_level(spdlog::level::debug);
            return;
        }
        // the warning level hides the informational lines
        if (level == "warn") {
            spdlog::set_level(spdlog::level::warn);
            return;
        }
        // the error level hides everything below a failure
        if (level == "error") {
            spdlog::set_level(spdlog::level::err);
            return;
        }
        // an unnamed or unknown level keeps the default
        spdlog::set_level(spdlog::level::info);
    }
} // namespace

int main(int argc, char** argv) {
    try {
        // name the embedded family before the first window shapes any text
        albor::set_installed_font_family(std::string(albor::generated::DEFAULT_FAMILY));
        // the overrides the command line named, without the program name
        const std::vector<std::string_view> arguments(argv + 1, argv + static_cast<std::ptrdiff_t>(argc));
        const studio::StartupOptions options = studio::parse_startup_options(arguments);
        // the requested verbosity applies before the first log line the shell writes
        apply_log_level(options.log_level);
        // load the persisted configuration before the overrides apply
        PluginConfig config = PluginConfig::load();
        // an explicit executable wins for this run without rewriting the file
        if (!options.xyce_path.empty())
            config.set_xyce_executable_path(options.xyce_path);
        // build a schematic session when the plugin launched inside KiCad
        std::optional<KiCadSession> session = KiCadSession::from_environment();
        // the shell binds the session, which keeps the API connection open for the plugin lifetime
        std::shared_ptr<studio::SchematicSession> schematic;
        if (session)
            schematic = std::make_shared<studio::KiCadSchematicSession>(std::make_unique<KiCadSession>(std::move(*session)));
#ifdef XYCE_STUDIO_AUTOMATION
        // the automation build serves a schematic netlist from disk, which drives plugin mode without KiCad
        if (const std::optional<std::string> stub = get_environment_variable("XYCE_STUDIO_SCHEMATIC"); stub.has_value() && !stub->empty())
            schematic = std::make_shared<studio::FileSchematicSession>(std::filesystem::path(*stub));
#endif
        // the shell owns the windows and the event loop
        studio::StudioApp app(
            std::move(config), albor::Application::instance(),
            [](PluginConfig& plugin_config, albor::Application& application, std::unique_ptr<NetlistSource> netlist_source) -> std::unique_ptr<studio::ShellWindow> {
                // the shell the application builds reads the configuration the app keeps
                auto window = std::make_unique<studio::WindowInstance>(application, plugin_config, std::move(netlist_source));
                // a refused shell leaves no window to own
                if (!window->create())
                    return nullptr;
                // hand the caller the created shell
                return window;
            },
            std::move(schematic));
        // a --netlist argument opens a netlist at startup in the standalone shell
        if (!options.netlist.empty())
            app.set_startup_netlist(options.netlist);
        // a --dataset or --raw argument opens an output file in the charts at startup
        for (const std::filesystem::path& dataset : options.datasets)
            app.set_startup_dataset(dataset);
        // run the application
        return app.run();
    }
    catch (const std::exception& error) {
        // a failure that escaped the shell still reports instead of ending in std::terminate
        std::fprintf(stderr, "xyce-studio: %s\n", error.what());
        return 1;
    }
}

#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "albor/application/application.h"
#include "albor/text/text_engine.h"
#include "albor_embedded_fonts.h"
#include "app/schematic_session.h"
#include "app/studio_app.h"
#include "app/window_instance.h"
#include "config/plugin_config.h"
#include "core/util.h"
#include "kicad/kicad_session.h"

namespace
{
    // apply the two spellings of the xyce override
    void apply_xyce_override(PluginConfig& config, int argc, char** argv) {
        // walk the arguments after the program name
        for (int index = 1; index < argc; ++index) {
            // read the argument as a view
            const std::string_view argument(argv[index]);
            // the separated spelling takes the next argument
            if (argument == "--xyce" && index + 1 < argc) {
                config.set_xyce_executable_path(argv[++index]);
                continue;
            }
            // the joined spelling carries the value in the same argument
            constexpr std::string_view prefix = "--xyce=";
            if (argument.starts_with(prefix))
                config.set_xyce_executable_path(std::string(argument.substr(prefix.size())));
        }
    }

    // apply the two spellings of the dataset override, which the charts open at startup
    void apply_dataset_override(studio::StudioApp& app, int argc, char** argv) {
        // walk the arguments after the program name
        for (int index = 1; index < argc; ++index) {
            // read the argument as a view
            const std::string_view argument(argv[index]);
            // the separated spelling takes the next argument
            if (argument == "--dataset" && index + 1 < argc) {
                app.set_startup_dataset(argv[++index]);
                continue;
            }
            // the joined spelling carries the value in the same argument
            constexpr std::string_view prefix = "--dataset=";
            if (argument.starts_with(prefix))
                app.set_startup_dataset(std::string(argument.substr(prefix.size())));
        }
    }

    // apply the two spellings of the netlist override, which only netlist files reach
    void apply_netlist_override(studio::StudioApp& app, int argc, char** argv) {
        // walk the arguments after the program name
        for (int index = 1; index < argc; ++index) {
            // read the argument as a view
            const std::string_view argument(argv[index]);
            // the separated spelling takes the next argument
            std::string_view value;
            if (argument == "--netlist" && index + 1 < argc)
                value = argv[++index];
            // the joined spelling carries the value in the same argument
            else {
                constexpr std::string_view prefix = "--netlist=";
                if (!argument.starts_with(prefix))
                    continue;
                value = argument.substr(prefix.size());
            }
            // only netlist files are accepted through this option
            const std::filesystem::path path(value);
            if (path.extension().string() == ".cir")
                app.set_startup_netlist(path);
        }
    }
} // namespace

int main(int argc, char** argv) {
    try {
        // name the embedded family before the first window shapes any text
        albor::set_installed_font_family(std::string(albor::generated::DEFAULT_FAMILY));
        // load the persisted configuration before the overrides apply
        PluginConfig config = PluginConfig::load();
        // an explicit executable wins for this run without rewriting the file
        apply_xyce_override(config, argc, argv);
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
        apply_netlist_override(app, argc, argv);
        // a --dataset argument opens an output file in the charts at startup
        apply_dataset_override(app, argc, argv);
        // run the application
        return app.run();
    }
    catch (const std::exception& error) {
        // a failure that escaped the shell still reports instead of ending in std::terminate
        std::fprintf(stderr, "xyce-studio: %s\n", error.what());
        return 1;
    }
}

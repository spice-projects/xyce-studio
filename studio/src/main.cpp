#include <filesystem>
#include <string>
#include <string_view>

#include "app/studio_app.h"
#include "config/plugin_config.h"

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
    // load the persisted configuration before the overrides apply
    PluginConfig config = PluginConfig::load();
    // an explicit executable wins for this run without rewriting the file
    apply_xyce_override(config, argc, argv);
    // the shell owns the windows and the event loop
    studio::StudioApp app(std::move(config));
    // a --netlist argument opens a netlist through the file dispatch at startup
    apply_netlist_override(app, argc, argv);
    // a --dataset argument opens an output file in the charts at startup
    apply_dataset_override(app, argc, argv);
    // run the application
    return app.run();
}

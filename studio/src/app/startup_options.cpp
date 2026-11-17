#include "app/startup_options.h"

#include <optional>

#include "core/util.h"

studio::StartupOptions studio::parse_startup_options(const std::vector<std::string_view>& arguments) {
    // the overrides the arguments name
    StartupOptions options;
    // every argument is one option, or the value of the one before it
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        // the argument under inspection
        const std::string_view argument = arguments[index];
        // the joined form carries its value after the equals sign
        const std::size_t equals = argument.find('=');
        // the option name without its value
        const std::string_view name = argument.substr(0, equals);
        // the value of this option, which the next argument supplies when none is joined
        const auto value = [&arguments, &argument, &index, equals]() -> std::optional<std::string_view> {
            // the joined form names its value in the same argument
            if (equals != std::string_view::npos)
                return argument.substr(equals + 1);
            // an option without a following argument has no value
            if (index + 1 >= arguments.size())
                return std::nullopt;
            // the separate form consumes the next argument
            return arguments[++index];
        };
        // the executable override wins over the persisted configuration
        if (name == "--xyce") {
            // a named path applies to this session only
            if (const std::optional<std::string_view> executable = value(); executable.has_value())
                options.xyce_path = std::string(*executable);
        }
        // the netlist override opens a document at startup
        else if (name == "--netlist") {
            // only netlist files are accepted through this option
            if (const std::optional<std::string_view> path = value(); path.has_value() && to_lower(std::filesystem::path(*path).extension().string()) == ".cir")
                options.netlist = std::filesystem::path(*path);
        }
        // the output override opens a dataset at startup, under either spelling
        else if (name == "--raw" || name == "--dataset") {
            // an empty path names no dataset
            if (const std::optional<std::string_view> path = value(); path.has_value() && !path->empty())
                options.datasets.emplace_back(*path);
        }
        // the verbosity applies to the process logger
        else if (name == "--log-level" || name == "-l") {
            // the spelling the logger resolves
            if (const std::optional<std::string_view> level = value(); level.has_value())
                options.log_level = to_lower(std::string(*level));
        }
    }
    // hand the overrides to the composition root
    return options;
}

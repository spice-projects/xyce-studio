#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace studio
{
    // the overrides the command line names for one session
    struct StartupOptions
    {
        // the netlist the standalone shell opens at startup, empty when none was named
        std::filesystem::path netlist;

        // the output files the standalone shell opens in the charts at startup
        std::vector<std::filesystem::path> datasets;

        // the Xyce executable override, empty when the configuration applies
        std::string xyce_path;

        // the log level the process applies, lowercase, empty when the default applies
        std::string log_level;
    };

    // parse the command line the process was launched with
    [[nodiscard]] StartupOptions parse_startup_options(const std::vector<std::string_view>& arguments);
} // namespace studio

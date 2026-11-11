#pragma once

#include <filesystem>
#include <string>

#include "simulation/simulation_config.h"

namespace studio
{
    // the datasets a finished run produced
    enum class RunOutputOutcome
    {
        // no produced file could be read
        None,
        // the analysis primary was read
        Primary,
        // only companion datasets were read
        CompanionOnly
    };

    // what the charts adopted from one finished run
    struct RunOutputResult
    {
        // the datasets the charts showed
        RunOutputOutcome outcome = RunOutputOutcome::None;

        // whether the configured analysis print file was read
        bool analysis_loaded = false;
    };

    // the configuration and paths of one finished run, which the charts resolve the produced datasets from
    struct RunOutput
    {
        // the configuration the run serialized, which names the analysis print and the companion outputs
        SimulationConfig config = SimulationConfig::from_xyce_directives({});

        // the private netlist inside the run directory the produced file names derive from
        std::filesystem::path netlist_path;

        // the working directory the simulator ran in
        std::filesystem::path working_directory;

        // the number of ports the .LIN devices declare
        int num_ports = 2;

        // the title the run tab shows
        std::string title;
    };
} // namespace studio

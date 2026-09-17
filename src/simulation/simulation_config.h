#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../netlist/netlist.h"
#include "data_block.h"
#include "option_parameters.h"
#include "print_parameters.h"
#include "step_parameters.h"

// forward declarations
#include "ac_simulation_parameters.h"
#include "dc_simulation_parameters.h"
#include "hb_simulation_parameters.h"
#include "lin_simulation_parameters.h"
#include "noise_simulation_parameters.h"
#include "op_simulation_parameters.h"
#include "transient_simulation_parameters.h"

using SimulationAnalysis = std::variant<std::monostate, AcSimulationParameters, DCSimulationParameters, HbSimulationParameters, LinSimulationParameters, NoiseSimulationParameters, OpSimulationParameters, TransientSimulationParameters>;

// simulation config class — aggregates all simulation parameters
// This mirrors the Python SimulationConfig which holds analysis parameters
// In C++, we use std::variant to hold any of the simulation parameter types
class SimulationConfig
{
public:
    // construct a simulation config from individual components
    // replace_ground defaults to true because KiCad netlists reference the ground node as "GND"
    // while Xyce uses node "0"; the .PREPROCESS REPLACEGROUND directive has no default per the RG
    SimulationConfig(std::string analysis_type, std::variant<std::monostate, AcSimulationParameters, DCSimulationParameters, HbSimulationParameters, LinSimulationParameters, NoiseSimulationParameters, OpSimulationParameters, TransientSimulationParameters> analysis, std::vector<StepParameters> steps, std::vector<DataBlock> data_blocks, OptionParameters options, std::vector<PrintParameters> unassociated_prints, bool replace_ground = true);

    // parse all directives into a SimulationConfig instance
    [[nodiscard]] static SimulationConfig from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // compute the produced raw output file path for the configured analysis;
    // the .PRINT FILE= option is stripped for the Xyce run, so Xyce always
    // writes the RAW file next to the netlist under its own default name and
    // the application never holds a mapped file that a re-run would rewrite
    [[nodiscard]] std::optional<std::filesystem::path> raw_output_file_path(const std::filesystem::path& netlist_file_path) const;

    // compute the user-facing raw output copy destination (the .PRINT FILE=
    // value resolved against the working directory); nullopt when no raw file
    // is produced or the analysis print carries no explicit file
    [[nodiscard]] std::optional<std::filesystem::path> raw_output_copy_destination(const std::filesystem::path& working_directory) const;

    // extract the analysis print parameters, normalizing the legacy OP
    // representation (print_dc_* fields without structured print_parameters)
    // into a PrintParameters instance; nullopt when no analysis is configured,
    // the analysis print is disabled, or no print produces raw output; the
    // single source for the analysis print so all helpers stay consistent
    [[nodiscard]] std::optional<PrintParameters> analysis_print_parameters() const;

    // serialize the analysis print directive (the only .PRINT whose output the
    // application parses and maps); nullopt when no analysis is configured or
    // the analysis print is disabled; used to scope FILE= stripping so
    // unassociated and legacy print directives keep their output files
    [[nodiscard]] std::optional<std::string> analysis_print_statement() const;

    // compute the expected FFT output file path pattern for the configured analysis
    [[nodiscard]] std::optional<std::filesystem::path> fft_output_file_path_pattern(const std::filesystem::path& netlist_file_path) const;

    // compute the touchstone output file path for the configured analysis; LIN
    // runs with a touchstone FORMAT produce a .s2p file — with FILE=/FILENAME=
    // the value is resolved against the working directory (Xyce's process cwd),
    // otherwise Xyce writes <netlist>.s2p next to the netlist; nullopt when the
    // analysis produces no touchstone output
    [[nodiscard]] std::optional<std::filesystem::path> touchstone_output_file_path(const std::filesystem::path& netlist_file_path, const std::filesystem::path& working_directory) const;

    // get the first step for backward compatibility
    [[nodiscard]] StepParameters step() const;

    // validate the entire simulation config across all components;
    // returns a user-facing error message, or nullopt when valid
    [[nodiscard]] std::optional<std::string> validate() const;

    // equality operator
    [[nodiscard]] bool operator==(const SimulationConfig& other) const;

    // analysis type identifier (e.g. "AC", "DC", "TRAN", etc.)
    std::string analysis_type;
    // variant to hold any of the analysis parameters (or monostate for none)
    SimulationAnalysis analysis;
    // step directives in order
    std::vector<StepParameters> steps;
    // data table blocks
    std::vector<DataBlock> data_blocks;
    // option directives
    OptionParameters options;
    // print directives not associated with any analysis
    std::vector<PrintParameters> unassociated_prints;
    // whether to apply the replace-ground (GND->0) preprocessing directive
    bool replace_ground;
};

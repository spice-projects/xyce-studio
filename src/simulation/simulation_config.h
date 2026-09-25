#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../netlist/netlist.h"
#include "data_block.h"
#include "ic_parameters.h"
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

// simulation config class — aggregates all simulation parameters, mirroring the Python SimulationConfig with a std::variant holding the analysis parameter types
class SimulationConfig
{
public:
    // construct a simulation config from individual components, replace_ground defaults to true because KiCad netlists reference the ground node as "GND" while Xyce uses node "0" and the .PREPROCESS REPLACEGROUND directive has no default per the RG
    SimulationConfig(std::string analysis_type, std::variant<std::monostate, AcSimulationParameters, DCSimulationParameters, HbSimulationParameters, LinSimulationParameters, NoiseSimulationParameters, OpSimulationParameters, TransientSimulationParameters> analysis, std::vector<StepParameters> steps, std::vector<DataBlock> data_blocks, OptionParameters options, std::vector<PrintParameters> unassociated_prints, bool replace_ground = true, ICParameters ic_parameters = {});

    // parse all directives into a SimulationConfig instance
    [[nodiscard]] static SimulationConfig from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // extract the analysis print parameters, normalizing the legacy OP representation (print_dc_* fields without structured print_parameters) into a PrintParameters instance, nullopt when no analysis is configured or the analysis print is disabled
    [[nodiscard]] std::optional<PrintParameters> analysis_print_parameters() const;

    // serialize the analysis print directive, nullopt when no analysis is configured or the analysis print is disabled
    [[nodiscard]] std::optional<std::string> analysis_print_statement() const;

    // compute the expected FFT output file path pattern for the configured analysis
    [[nodiscard]] std::optional<std::filesystem::path> fft_output_file_path_pattern(const std::filesystem::path& netlist_file_path) const;

    // extract the .PRINT PCE parameters of the configured analysis (the companion print of the DC or TRAN .PCE directive), nullopt when no analysis is configured or no .PCE companion print is configured
    [[nodiscard]] std::optional<PrintParameters> pce_print_parameters() const;

    // compute the produced PCE statistics output file path for the configured analysis, the FILE= option is removed for the run so Xyce writes the default PCE suffix (.PCE.prn, .PCE.csv or .PCE.dat) next to the netlist, nullopt when the run produces no PCE output
    [[nodiscard]] std::optional<std::filesystem::path> pce_output_file_path(const std::filesystem::path& netlist_file_path) const;

    // the companion measurement data a finished run produces besides the analysis output, which entries are set depends on the configured analysis type
    struct ProducedMeasurements
    {
        // a .LIN analysis with a touchstone format dumps one s-parameter file
        bool s_parameters = false;
        // a .TRAN analysis with .FFT directives dumps the FFT calculation files
        bool fft = false;
        // a .TRAN or .DC analysis with .PCE parameters and a .PCE companion print dumps the PCE statistics output
        bool pce = false;
    };

    // report the companion measurement data the configured analysis produces
    [[nodiscard]] ProducedMeasurements produced_measurements() const;

    // compute the s-parameter output file path for the configured analysis, LIN runs with a touchstone FORMAT produce a touchstone file — with FILE=/FILENAME= the value is used verbatim resolved against the working directory (Xyce's process cwd), otherwise Xyce writes <netlist>.sNp next to the netlist with N being the port count, nullopt when the analysis produces no s-parameter output
    [[nodiscard]] std::optional<std::filesystem::path> s_parameter_output_file_path(const std::filesystem::path& netlist_file_path, const std::filesystem::path& working_directory, int num_ports = 2) const;

    // collect all print parameters that produce .prn output (STD, NOINDEX, GNUPLOT, SPLOT formats), including the analysis print and any unassociated .PRINT directives, empty when no .prn output is configured
    [[nodiscard]] std::vector<PrintParameters> prn_print_parameters() const;

    // get the first step for backward compatibility
    [[nodiscard]] StepParameters step() const;

    // validate the entire simulation config across all components, returns a user-facing error message or nullopt when valid
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
    // initial condition parameters (.IC / .DCVOLT directives)
    ICParameters ic_parameters;
};

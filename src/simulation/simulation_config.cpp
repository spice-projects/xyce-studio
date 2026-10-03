#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "simulation_config.h"

#include "../core/util.h"
#include "ac_simulation_parameters.h"
#include "dc_simulation_parameters.h"
#include "hb_simulation_parameters.h"
#include "ic_parameters.h"
#include "lin_simulation_parameters.h"
#include "noise_simulation_parameters.h"
#include "op_simulation_parameters.h"
#include "transient_simulation_parameters.h"

namespace
{

    // check whether an already printed output variable covers a quantity through a wildcard, e.g. a printed V(*) covers V(N1) and V(N2,N3) while a printed I(*) does not cover the lead current IC(Q1)
    bool wildcard_covers(const std::string& printed, const std::string& quantity) {
        // a wildcard output variable carries the probe name followed by the (*) argument list
        if (!to_upper(printed).ends_with("(*)"))
            return false;
        // a quantity without an argument list carries no probe name and is never covered
        if (quantity.find('(') == std::string::npos)
            return false;
        // the wildcard covers the quantities of its own probe family
        return to_upper(printed.substr(0, printed.size() - 3)) == to_upper(quantity.substr(0, quantity.find('(')));
    }

    // merge output variables into an output variable list, a variable already present passes through once and a printed wildcard suppresses the quantities it already covers; additionally_printed names quantities the print already carries outside its variable list (the device noise operators of a .PRINT NOISE), so they are not appended twice
    std::vector<std::string> merge_output_variables(const std::vector<std::string>& printed, const std::vector<std::string>& variables, const std::vector<std::string>& additionally_printed = {}) {
        // the merged output variables
        std::vector<std::string> merged = printed;
        // names already present, compared in lowercase because the expression manager looks the names up in lowercase
        std::set<std::string> present;
        // index every already printed variable
        for (const auto& variable : printed)
            present.insert(to_lower(variable));
        // index the quantities the print carries outside its variable list
        for (const auto& variable : additionally_printed)
            present.insert(to_lower(variable));
        // append every variable that is neither printed yet nor covered by a printed wildcard
        for (const auto& variable : variables) {
            // skip a variable that is already printed
            if (!present.insert(to_lower(variable)).second)
                continue;
            // skip a variable a printed wildcard already collects
            if (std::ranges::any_of(printed, [&variable](const auto& existing) { return wildcard_covers(existing, variable); }))
                continue;
            merged.push_back(variable);
        }
        return merged;
    }

    // render the device noise operators of a .PRINT NOISE back to their output variable spelling, the noise parser stores them beside the output variables and the serialization appends them there, so a plotted operator must not be added to the variable list as well
    std::vector<std::string> device_noise_operator_variables(const std::vector<DeviceNoiseOperator>& operators) {
        // the rendered operator tokens
        std::vector<std::string> variables;
        // render every stored operator
        for (const auto& operator_entry : operators) {
            // start with the operator type and its node
            std::string variable = operator_entry.type + "(" + operator_entry.node;
            // append the noise source when the operator carries one
            if (!operator_entry.source.empty())
                variable += "," + operator_entry.source;
            // close the argument list and keep the token
            variables.push_back(variable + ")");
        }
        return variables;
    }

    // merge output variables into the analysis print of every analysis variant
    struct PrintAugmentVisitor
    {
        // variables to merge into the analysis print
        const std::vector<std::string>& variables;
        // whether a print may be created for an analysis that has none
        bool may_create;
        // print type used when a print is created
        std::string print_type;

        void operator()(std::monostate&) const {}

        template <typename T>
        void operator()(T& params) const {
            // the noise analysis carries its device noise operators beside the output variables, they must take part in the deduplication
            std::vector<std::string> carried_outside;
            if constexpr (std::is_same_v<T, NoiseSimulationParameters>)
                carried_outside = device_noise_operator_variables(params.device_noise_operators);
            // merge into the structured print of the analysis
            if (params.print_parameters.has_value()) {
                params.print_parameters->output_variables = merge_output_variables(params.print_parameters->output_variables, variables, carried_outside);
                return;
            }
            // the legacy .OP representation keeps its output variables in a separate list
            if constexpr (std::is_same_v<T, OpSimulationParameters>) {
                if (params.print_dc_enabled) {
                    params.print_dc_specific_variables = merge_output_variables(params.print_dc_specific_variables, variables);
                    return;
                }
            }
            // the analysis produces no print at all, create one carrying the variables
            if (may_create)
                params.print_parameters = PrintParameters(print_type, "", "", variables, {});
        }
    };

} // anonymous namespace

SimulationConfig::SimulationConfig(std::string analysis_type, std::variant<std::monostate, AcSimulationParameters, DCSimulationParameters, HbSimulationParameters, LinSimulationParameters, NoiseSimulationParameters, OpSimulationParameters, TransientSimulationParameters> analysis, std::vector<StepParameters> steps, std::vector<DataBlock> data_blocks, OptionParameters options, std::vector<PrintParameters> unassociated_prints, bool replace_ground, ICParameters ic_parameters, std::optional<std::string> remove_unused, std::vector<std::string> add_resistors) :
    analysis_type(std::move(analysis_type)), analysis(std::move(analysis)), steps(std::move(steps)), data_blocks(std::move(data_blocks)), options(std::move(options)), unassociated_prints(std::move(unassociated_prints)), replace_ground(replace_ground), ic_parameters(std::move(ic_parameters)), remove_unused(std::move(remove_unused)), add_resistors(std::move(add_resistors)) {}

StepParameters SimulationConfig::step() const {
    // return the first step for backward compatibility, or a disabled default
    if (!steps.empty()) {
        // return first step
        return steps[0];
    }
    // return disabled default
    return StepParameters();
}

std::optional<std::string> SimulationConfig::validate() const {
    // a .STEP directive requires a primary analysis
    for (const auto& step : steps) {
        if (step.enabled && std::holds_alternative<std::monostate>(analysis)) {
            return "STEP sweep requires a primary analysis (e.g. .DC, .TRAN, .AC)";
        }
    }
    // validate each individual step
    for (const auto& step : steps) {
        if (const auto error = step.validate()) {
            return *error;
        }
    }
    return std::nullopt;
}

SimulationConfig SimulationConfig::from_xyce_directives(const std::vector<std::string>& directives) {
    // init analysis result to none (monostate)
    std::variant<std::monostate, AcSimulationParameters, DCSimulationParameters, HbSimulationParameters, LinSimulationParameters, NoiseSimulationParameters, OpSimulationParameters, TransientSimulationParameters> analysis = std::monostate{};
    std::string analysis_type;

    // list of simulation parameter types in order of precedence, LinSimulationParameters MUST appear before AcSimulationParameters because .LIN netlists also contain a .AC directive and the Lin class embeds the AC sweep so it must claim the match first
    const std::vector<std::string> simulation_types = {"LIN", "AC", "HB", "NOISE", "DC", "OP", "TRAN"};

    // iterate all registered simulation types to find a match
    for (const auto& type : simulation_types) {
        // try to parse the directive list into a specific simulation type
        std::variant<std::monostate, AcSimulationParameters, DCSimulationParameters, HbSimulationParameters, LinSimulationParameters, NoiseSimulationParameters, OpSimulationParameters, TransientSimulationParameters> simulation_parameters = std::monostate{};

        if (type == "LIN") {
            const auto params = LinSimulationParameters::from_xyce_directives(directives);
            if (params.has_value()) {
                simulation_parameters = params.value();
            }
        }
        else if (type == "AC") {
            const auto params = AcSimulationParameters::from_xyce_directives(directives);
            if (params.has_value()) {
                simulation_parameters = params.value();
            }
        }
        else if (type == "HB") {
            const auto params = HbSimulationParameters::from_xyce_directives(directives);
            if (params.has_value()) {
                simulation_parameters = params.value();
            }
        }
        else if (type == "NOISE") {
            const auto params = NoiseSimulationParameters::from_xyce_directives(directives);
            if (params.has_value()) {
                simulation_parameters = params.value();
            }
        }
        else if (type == "DC") {
            const auto params = DCSimulationParameters::from_xyce_directives(directives);
            if (params.has_value()) {
                simulation_parameters = params.value();
            }
        }
        else if (type == "OP") {
            const auto params = OpSimulationParameters::from_xyce_directives(directives);
            if (params.has_value()) {
                simulation_parameters = params.value();
            }
        }
        else if (type == "TRAN") {
            const auto params = TransientSimulationParameters::from_xyce_directives(directives);
            if (params.has_value()) {
                simulation_parameters = params.value();
            }
        }

        // check if a match was found
        if (std::holds_alternative<std::monostate>(simulation_parameters) == false) {
            // store the analysis parameters
            analysis = std::move(simulation_parameters);
            analysis_type = type;
            // stop searching once the first valid analysis is found
            break;
        }
    }

    // parse all step directives preserving nested loop order
    const auto steps = StepParameters::all_from_xyce_directives(directives);

    // parse all .DATA table blocks
    const auto data_blocks = DataBlock::from_xyce_directives(directives);

    // parse the structured option directives
    const auto options = OptionParameters::from_xyce_directives(directives);

    // parse preprocessing directives (.PREPROCESS REPLACEGROUND, REMOVEUNUSED, ADDRESISTORS)
    bool replace_ground = true;
    std::optional<std::string> remove_unused;
    std::vector<std::string> add_resistors;
    for (const auto& directive : directives) {
        // tokenize the directive
        const auto tokens = tokenize(directive);
        // skip directives without at least directive and subcommand
        if (tokens.size() < 2)
            continue;
        // check for .PREPROCESS directive
        if (to_upper(tokens[0]) == ".PREPROCESS") {
            // extract the subcommand in uppercase
            const auto subcommand = to_upper(tokens[1]);
            // handle REPLACEGROUND
            if (subcommand == "REPLACEGROUND" && tokens.size() > 2) {
                // set replace_ground based on the third token (last statement wins)
                replace_ground = (to_upper(tokens[2]) == "TRUE");
            }
            // handle REMOVEUNUSED
            else if (subcommand == "REMOVEUNUSED") {
                // rebuild argument string from tokens after the subcommand
                std::string val;
                for (size_t i = 2; i < tokens.size(); ++i) {
                    // append whitespace separator between tokens
                    if (!val.empty())
                        val += " ";
                    // append token
                    val += std::string(tokens[i]);
                }
                // record the remove-unused configuration (last statement wins per RG)
                remove_unused = val;
            }
            // handle ADDRESISTORS
            else if (subcommand == "ADDRESISTORS" && tokens.size() > 2) {
                // rebuild argument string from tokens after the subcommand
                std::string val;
                for (size_t i = 2; i < tokens.size(); ++i) {
                    // append whitespace separator between tokens
                    if (!val.empty())
                        val += " ";
                    // append token
                    val += std::string(tokens[i]);
                }
                // record the add-resistors configuration
                add_resistors.push_back(val);
            }
        }
    }

    // init unassociated print list
    std::vector<PrintParameters> unassociated_prints;

    // identify all handled print types for the current analysis to avoid duplicates
    std::set<std::string> handled_print_types;

    // check if the analysis has print parameters already handled
    if (std::holds_alternative<std::monostate>(analysis) == false) {
        // mark the print types the analysis parser claims into its structured parameters
        if (analysis_type == "AC" || analysis_type == "LIN") {
            handled_print_types.insert("AC");
            handled_print_types.insert("AC_IC");
        }
        else if (analysis_type == "DC") {
            handled_print_types.insert("DC");
            handled_print_types.insert("HOMOTOPY");
            // the DC parser claims .PRINT PCE into its structured PCE parameters, without this the same directive would also be appended to the unassociated prints
            handled_print_types.insert("PCE");
        }
        else if (analysis_type == "TRAN") {
            handled_print_types.insert("TRAN");
            handled_print_types.insert("TRANADJOINT");
            // the TRAN parser claims .PRINT PCE into its structured PCE parameters, without this the same directive would also be appended to the unassociated prints
            handled_print_types.insert("PCE");
        }
        else if (analysis_type == "HB") {
            handled_print_types.insert("HB");
            handled_print_types.insert("HB_FD");
            handled_print_types.insert("HB_TD");
            handled_print_types.insert("HB_IC");
            handled_print_types.insert("HB_STARTUP");
        }
        else if (analysis_type == "NOISE") {
            handled_print_types.insert("NOISE");
        }
        else if (analysis_type == "OP") {
            // the OP parser claims .PRINT DC into its structured print parameters, without this the same directive would also be appended to the unassociated prints and duplicated in the Xyce netlist
            handled_print_types.insert("DC");
        }
    }

    // iterate all directives to find unassociated prints
    for (const auto& directive : directives) {
        // tokenize the directive
        std::vector<std::string> tokens;
        std::string current;
        for (const char ch : directive) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                continue;
            }
            current += ch;
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }

        // skip non-print or empty directives
        if (tokens.empty() || to_upper(tokens[0]) != ".PRINT") {
            continue;
        }

        // parse the print statement
        const auto pp = PrintParameters::from_xyce_statement(directive);
        if (!pp) {
            continue;
        }

        // check if print was successfully parsed and is not handled by the analysis
        const std::string print_type_upper = to_upper(pp->print_type);
        if (handled_print_types.find(print_type_upper) == handled_print_types.end()) {
            // add to unassociated list
            unassociated_prints.push_back(*pp);
        }
    }

    // parse the initial condition (.IC / .DCVOLT) directives
    const auto ic_parameters = ICParameters::from_xyce_directives(directives);

    // return the combined configuration container
    return SimulationConfig(analysis_type, std::move(analysis), steps, data_blocks, options, unassociated_prints, replace_ground, ic_parameters, remove_unused, add_resistors);
}

std::vector<std::string> SimulationConfig::to_xyce_directives(const NetlistTopology& topology) const {
    // init output directive list
    std::vector<std::string> directives;

    // extend with option directives
    const auto option_directives = options.to_xyce_directives(topology);
    directives.insert(directives.end(), option_directives.begin(), option_directives.end());

    // emit the replace-ground preprocessing directive at most once for the whole netlist, the state is always emitted explicitly so that a disabled replacement round-trips
    directives.push_back(replace_ground ? ".PREPROCESS REPLACEGROUND TRUE" : ".PREPROCESS REPLACEGROUND FALSE");

    // emit the remove-unused preprocessing directive when configured
    if (remove_unused.has_value()) {
        // emit remove-unused directive with or without value
        directives.push_back(remove_unused->empty() ? ".PREPROCESS REMOVEUNUSED" : ".PREPROCESS REMOVEUNUSED " + *remove_unused);
    }

    // emit all add-resistors preprocessing directives
    for (const auto& add_resistor : add_resistors) {
        // emit add-resistors directive
        directives.push_back(".PREPROCESS ADDRESISTORS " + add_resistor);
    }

    // emit the merged .IC line for every analysis type: Xyce consumes it in whichever DCOP the analysis performs, never rejects it, and documents no effect only for HB (RG .IC and .HB sections)
    const auto ic_directives = ic_parameters.to_xyce_directives(topology);
    directives.insert(directives.end(), ic_directives.begin(), ic_directives.end());

    // check if an analysis is configured and use std::visit to call to_xyce_directives on the active variant member
    struct DirectiveVisitor
    {
        const NetlistTopology& topology;
        std::vector<std::string> operator()(const std::monostate&) const { return {}; }
        std::vector<std::string> operator()(const AcSimulationParameters& params) const { return params.to_xyce_directives(topology); }
        std::vector<std::string> operator()(const DCSimulationParameters& params) const { return params.to_xyce_directives(topology); }
        std::vector<std::string> operator()(const HbSimulationParameters& params) const { return params.to_xyce_directives(topology); }
        std::vector<std::string> operator()(const LinSimulationParameters& params) const { return params.to_xyce_directives(topology); }
        std::vector<std::string> operator()(const NoiseSimulationParameters& params) const { return params.to_xyce_directives(topology); }
        std::vector<std::string> operator()(const OpSimulationParameters& params) const { return params.to_xyce_directives(topology); }
        std::vector<std::string> operator()(const TransientSimulationParameters& params) const { return params.to_xyce_directives(topology); }
    };
    DirectiveVisitor visitor{topology};

    const auto analysis_directives = std::visit(visitor, analysis);
    directives.insert(directives.end(), analysis_directives.begin(), analysis_directives.end());

    // emit all step directives preserving the original nested loop order
    for (const auto& step : steps) {
        // extend with each step directive
        const auto step_directives = step.to_xyce_directives();
        directives.insert(directives.end(), step_directives.begin(), step_directives.end());
    }

    // emit all .DATA table blocks
    for (const auto& data_block : data_blocks) {
        // extend with the data block directives
        const auto block_directives = data_block.to_xyce_directives();
        directives.insert(directives.end(), block_directives.begin(), block_directives.end());
    }

    // extend with unassociated prints (topology-aware wildcard expansion)
    for (const auto& pp : unassociated_prints) {
        // append print directive string with topology
        directives.push_back(pp.to_xyce_statement());
    }

    // return the full consolidated directive list
    return directives;
}

bool SimulationConfig::operator==(const SimulationConfig& other) const {
    // compare all fields for equality
    return analysis_type == other.analysis_type && analysis == other.analysis && steps == other.steps && data_blocks == other.data_blocks && options == other.options && unassociated_prints == other.unassociated_prints && ic_parameters == other.ic_parameters && replace_ground == other.replace_ground && remove_unused == other.remove_unused && add_resistors == other.add_resistors;
}

std::optional<PrintParameters> SimulationConfig::analysis_print_parameters() const {
    // process simulation types
    auto l = []<typename T0>(T0& a) -> std::optional<PrintParameters> {
        // actual parameter type
        using TX = std::decay_t<T0>;
        // std::monostate
        if constexpr (std::is_same_v<TX, std::monostate>) {
            // no analysis, no analysis print directive
            return std::nullopt;
        }
        else {
            // structured print parameters when set
            if (a.print_parameters.has_value())
                return a.print_parameters;
            // legacy OP representation: derive the structured print from the print_dc_* fields (de-duplicated variables, matching the legacy directive emission)
            if constexpr (std::is_same_v<TX, OpSimulationParameters>) {
                if (a.print_dc_enabled) {
                    // de-duplicate the variables preserving order, matching the legacy emission
                    std::vector<std::string> unique_vars;
                    std::set<std::string> seen;
                    for (const auto& var : a.print_dc_specific_variables) {
                        if (seen.insert(var).second)
                            unique_vars.push_back(var);
                    }
                    return PrintParameters("DC", a.print_dc_format, a.print_dc_file, std::move(unique_vars), {});
                }
            }
            // no analysis print directive
            return std::nullopt;
        }
    };
    // visit the analysis variant
    return std::visit(l, analysis);
}

std::optional<std::string> SimulationConfig::analysis_print_statement() const {
    // serialize the analysis print parameters
    const auto print_parameters = analysis_print_parameters();
    // no print configured
    if (!print_parameters.has_value())
        return std::nullopt;
    // return the serialized directive
    return print_parameters->to_xyce_statement();
}

void SimulationConfig::augment_analysis_print_variables(const std::vector<std::string>& variables, bool create_when_absent) {
    // nothing declared, nothing to merge
    if (variables.empty())
        return;
    // the print type of the configured analysis, an .OP run prints the DC operating point and a .LIN run writes its touchstone output instead of an analysis print
    std::string print_type;
    if (!std::holds_alternative<std::monostate>(analysis) && analysis_type != "LIN")
        print_type = analysis_type == "OP" ? "DC" : to_upper(analysis_type);
    // a print is only created when the caller allows it and the analysis has one
    const bool may_create = create_when_absent && !print_type.empty();
    // merge the variables into the analysis print of the configured analysis
    std::visit(PrintAugmentVisitor{variables, may_create, std::move(print_type)}, analysis);
}

std::optional<std::filesystem::path> SimulationConfig::fft_output_file_path_pattern(const std::filesystem::path& netlist_file_path) const {
    struct FftPathVisitor
    {
        const std::filesystem::path& netlist_file_path;

        std::optional<std::filesystem::path> operator()(const std::monostate&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const AcSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const DCSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const HbSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const LinSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const NoiseSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const OpSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const TransientSimulationParameters& params) const {
            if (params.fft_parameters.empty()) {
                return std::nullopt;
            }
            return netlist_file_path.string() + ".fft*";
        }
    };

    return std::visit(FftPathVisitor{netlist_file_path}, analysis);
}

std::optional<PrintParameters> SimulationConfig::pce_print_parameters() const {
    // the DC analysis carries the companion print inside its .PCE parameters
    if (const auto* dc = std::get_if<DCSimulationParameters>(&analysis); dc && dc->pce.has_value())
        return dc->pce->print_parameters;
    // the transient analysis carries the same companion print
    if (const auto* tran = std::get_if<TransientSimulationParameters>(&analysis); tran && tran->pce.has_value())
        return tran->pce->print_parameters;
    // every other analysis has no .PCE companion directive
    return std::nullopt;
}

std::optional<std::filesystem::path> SimulationConfig::pce_output_file_path(const std::filesystem::path& netlist_file_path) const {
    // no .PCE companion print, no PCE output file
    const auto pce_print = pce_print_parameters();
    if (!pce_print.has_value())
        return std::nullopt;
    // FILE= is removed from every .PRINT statement for the run, so Xyce writes the default file for the print type and format next to the netlist
    return default_print_output_file(*pce_print, netlist_file_path);
}

SimulationConfig::ProducedMeasurements SimulationConfig::produced_measurements() const {

    struct ProducedMeasurementsVisitor
    {
        SimulationConfig::ProducedMeasurements operator()(const std::monostate&) const { return {}; }
        SimulationConfig::ProducedMeasurements operator()(const AcSimulationParameters&) const { return {}; }
        SimulationConfig::ProducedMeasurements operator()(const DCSimulationParameters& params) const {
            // a .DC analysis with .PCE parameters and a companion print dumps the PCE statistics output
            return {.pce = params.pce.has_value() && params.pce->print_parameters.has_value()};
        }
        SimulationConfig::ProducedMeasurements operator()(const HbSimulationParameters&) const { return {}; }
        SimulationConfig::ProducedMeasurements operator()(const NoiseSimulationParameters&) const { return {}; }
        SimulationConfig::ProducedMeasurements operator()(const OpSimulationParameters&) const { return {}; }
        SimulationConfig::ProducedMeasurements operator()(const TransientSimulationParameters& params) const {
            // a .TRAN analysis with .FFT directives dumps the FFT calculation files and a .TRAN analysis with .PCE parameters and a companion print dumps the PCE statistics output
            return {.fft = !params.fft_parameters.empty(), .pce = params.pce.has_value() && params.pce->print_parameters.has_value()};
        }
        SimulationConfig::ProducedMeasurements operator()(const LinSimulationParameters& params) const {
            // a .LIN run with a touchstone format dumps one s-parameter file
            return {.s_parameters = params.format == "TOUCHSTONE" || params.format == "TOUCHSTONE2"};
        }
    };

    return std::visit(ProducedMeasurementsVisitor{}, analysis);
}

std::optional<std::filesystem::path> SimulationConfig::s_parameter_output_file_path(const std::filesystem::path& netlist_file_path, const std::filesystem::path& working_directory, int num_ports) const {

    struct SParameterPathVisitor
    {
        const std::filesystem::path& netlist_file_path;
        const std::filesystem::path& working_directory;
        int num_ports;

        std::optional<std::filesystem::path> operator()(const std::monostate&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const AcSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const DCSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const HbSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const NoiseSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const OpSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const TransientSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const LinSimulationParameters& params) const {
            // only touchstone output formats produce a touchstone file
            if (params.format != "TOUCHSTONE2" && params.format != "TOUCHSTONE")
                return std::nullopt;
            // no FILE=/FILENAME= given: Xyce writes <netlist>.sNp next to the netlist, N being the port count (P devices in the netlist)
            if (params.file.empty())
                return std::optional<std::filesystem::path>(netlist_file_path.string() + ".s" + std::to_string(num_ports) + "p");
            // FILE= takes precedence over FILENAME= (already enforced at parse time); strip outer quotes and resolve relative values against the working directory, which is Xyce's process cwd for the run
            const auto file = strip_outer_quotes(params.file);
            if (std::filesystem::path(file).is_absolute())
                return std::optional<std::filesystem::path>(file);
            // resolve relative paths against the working directory
            return std::optional<std::filesystem::path>(working_directory / file);
        }
    };

    return std::visit(SParameterPathVisitor{netlist_file_path, working_directory, num_ports}, analysis);
}

std::vector<PrintParameters> SimulationConfig::prn_print_parameters() const {
    // result vector for print parameters that produce .prn output
    std::vector<PrintParameters> result;
    // check the analysis print
    const auto analysis_print = analysis_print_parameters();
    // helper to check if a print format produces .prn output
    auto is_prn_format = [](const std::string& fmt) -> bool {
        // empty format means RAW output (handled separately)
        if (fmt.empty()) {
            return false;
        }
        // normalize to uppercase
        std::string upper = fmt;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        // STD, NOINDEX, GNUPLOT, and SPLOT formats produce .prn files
        return upper == "STD" || upper == "NOINDEX" || upper == "GNUPLOT" || upper == "SPLOT";
    };
    // add the analysis print if it produces .prn output
    if (analysis_print.has_value() && is_prn_format(analysis_print->print_format)) {
        result.push_back(*analysis_print);
    }
    // add unassociated prints that produce .prn output
    for (const auto& print : unassociated_prints) {
        if (is_prn_format(print.print_format)) {
            result.push_back(print);
        }
    }
    return result;
}

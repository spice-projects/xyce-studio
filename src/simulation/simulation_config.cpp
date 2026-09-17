#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "simulation_config.h"

#include "../core/util.h"
#include "ac_simulation_parameters.h"
#include "dc_simulation_parameters.h"
#include "hb_simulation_parameters.h"
#include "lin_simulation_parameters.h"
#include "noise_simulation_parameters.h"
#include "op_simulation_parameters.h"
#include "transient_simulation_parameters.h"

SimulationConfig::SimulationConfig(std::string analysis_type, std::variant<std::monostate, AcSimulationParameters, DCSimulationParameters, HbSimulationParameters, LinSimulationParameters, NoiseSimulationParameters, OpSimulationParameters, TransientSimulationParameters> analysis, std::vector<StepParameters> steps, std::vector<DataBlock> data_blocks, OptionParameters options, std::vector<PrintParameters> unassociated_prints, bool replace_ground) :
    analysis_type(std::move(analysis_type)), analysis(std::move(analysis)), steps(std::move(steps)), data_blocks(std::move(data_blocks)), options(std::move(options)), unassociated_prints(std::move(unassociated_prints)), replace_ground(replace_ground) {}

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

    // list of simulation parameter types in order of precedence
    // LinSimulationParameters MUST appear before AcSimulationParameters because
    // .LIN netlists also contain a .AC directive; the Lin class embeds the AC
    // sweep so it must claim the match first.
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

    // parse the replace-ground preprocessing directive
    // the RG gives .PREPROCESS REPLACEGROUND no default, so a netlist without the
    // statement preserves its baseline semantics on round-trip
    bool replace_ground = true;
    for (const auto& directive : directives) {
        // tokenize the directive
        const auto tokens = tokenize(directive);
        // skip empty or short directives
        if (tokens.size() < 3)
            continue;
        // check for .PREPROCESS REPLACEGROUND directive
        if (to_upper(tokens[0]) == ".PREPROCESS" && to_upper(tokens[1]) == "REPLACEGROUND") {
            // set replace_ground based on the third token (last statement wins)
            replace_ground = (to_upper(tokens[2]) == "TRUE");
        }
    }

    // init unassociated print list
    std::vector<PrintParameters> unassociated_prints;

    // identify all handled print types for the current analysis to avoid duplicates
    std::set<std::string> handled_print_types;

    // check if the analysis has print parameters already handled
    if (std::holds_alternative<std::monostate>(analysis) == false) {
        // For now, we'll check the analysis type and mark the appropriate print type
        // This is a simplified version - in the full implementation, we'd need to
        // access the analysis object's print_parameters field
        if (analysis_type == "AC" || analysis_type == "LIN") {
            handled_print_types.insert("AC");
            handled_print_types.insert("AC_IC");
        }
        else if (analysis_type == "DC") {
            handled_print_types.insert("DC");
            handled_print_types.insert("HOMOTOPY");
        }
        else if (analysis_type == "TRAN") {
            handled_print_types.insert("TRAN");
            handled_print_types.insert("TRANADJOINT");
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
            // the OP parser claims .PRINT DC into its structured print
            // parameters; without this the same directive would also be
            // appended to the unassociated prints, duplicating it in the
            // Xyce netlist and splitting its output between the produced RAW
            // file (stripped FILE=) and the user file (unstripped)
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

    // return the combined configuration container
    return SimulationConfig(analysis_type, std::move(analysis), steps, data_blocks, options, unassociated_prints, replace_ground);
}

std::vector<std::string> SimulationConfig::to_xyce_directives(const NetlistTopology& topology) const {
    // init output directive list
    std::vector<std::string> directives;

    // extend with option directives
    const auto option_directives = options.to_xyce_directives(topology);
    directives.insert(directives.end(), option_directives.begin(), option_directives.end());

    // emit the replace-ground preprocessing directive at most once for the whole netlist
    // the state is always emitted explicitly so that a disabled replacement round-trips
    directives.push_back(replace_ground ? ".PREPROCESS REPLACEGROUND TRUE" : ".PREPROCESS REPLACEGROUND FALSE");

    // check if an analysis is configured
    // Use std::visit to call to_xyce_directives on the active variant member
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
    return analysis_type == other.analysis_type && analysis == other.analysis && steps == other.steps && data_blocks == other.data_blocks && options == other.options && unassociated_prints == other.unassociated_prints && replace_ground == other.replace_ground;
}

std::optional<std::filesystem::path> SimulationConfig::raw_output_file_path(const std::filesystem::path& netlist_file_path) const {
    // no analysis, no raw output
    if (std::holds_alternative<std::monostate>(analysis))
        return std::nullopt;
    // analysis print parameters (structured or legacy-normalized)
    const auto print_parameters = analysis_print_parameters();
    // a print configured with a non-RAW format produces no raw output file
    if (print_parameters.has_value() && !print_parameters->print_format.empty() && to_upper(print_parameters->print_format) != "RAW")
        return std::nullopt;
    // the FILE= option is stripped for the Xyce run, so the RAW file is always
    // produced next to the netlist under Xyce's default name regardless of the
    // user's print file
    return std::optional<std::filesystem::path>(netlist_file_path.string() + ".raw");
}

std::optional<std::filesystem::path> SimulationConfig::raw_output_copy_destination(const std::filesystem::path& working_directory) const {
    // no analysis, no raw output
    if (std::holds_alternative<std::monostate>(analysis))
        return std::nullopt;
    // analysis print parameters (structured or legacy-normalized)
    const auto print_parameters = analysis_print_parameters();
    // no print configured or a non-RAW format produces no raw file to copy
    if (!print_parameters.has_value() || (!print_parameters->print_format.empty() && to_upper(print_parameters->print_format) != "RAW"))
        return std::nullopt;
    // no explicit file to copy to
    if (print_parameters->print_file.empty())
        return std::nullopt;
    // resolve the user's print file against the working directory (the model
    // always carries the bare filename; strip any outer quotes for consistency)
    return std::optional<std::filesystem::path>(working_directory / strip_outer_quotes(print_parameters->print_file));
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
            // legacy OP representation: derive the structured print from the
            // print_dc_* fields (de-duplicated variables, matching the
            // legacy directive emission)
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

std::optional<std::filesystem::path> SimulationConfig::touchstone_output_file_path(const std::filesystem::path& netlist_file_path, const std::filesystem::path& working_directory) const {
    struct TouchstonePathVisitor
    {
        const std::filesystem::path& netlist_file_path;
        const std::filesystem::path& working_directory;

        std::optional<std::filesystem::path> operator()(const std::monostate&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const AcSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const DCSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const HbSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const NoiseSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const OpSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const TransientSimulationParameters&) const { return std::nullopt; }
        std::optional<std::filesystem::path> operator()(const LinSimulationParameters& params) const {
            // only touchstone output formats produce a touchstone file
            if (params.format != "TOUCHSTONE2" && params.format != "TOUCHSTONE") {
                return std::nullopt;
            }
            // no FILE=/FILENAME= given: Xyce writes <netlist>.s2p next to the netlist
            if (params.file.empty()) {
                return std::optional<std::filesystem::path>(netlist_file_path.string() + ".s2p");
            }
            // FILE= takes precedence over FILENAME= (already enforced at parse
            // time); strip outer quotes and resolve relative values against the
            // working directory, which is Xyce's process cwd for the run
            const auto file = strip_outer_quotes(params.file);
            if (std::filesystem::path(file).is_absolute())
                return std::optional<std::filesystem::path>(file);
            return std::optional<std::filesystem::path>(working_directory / file);
        }
    };

    return std::visit(TouchstonePathVisitor{netlist_file_path, working_directory}, analysis);
}

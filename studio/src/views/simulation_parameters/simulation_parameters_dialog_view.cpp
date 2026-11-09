#include "views/simulation_parameters/simulation_parameters_dialog_view.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/focus/focus_manager.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "core/util.h"
#include "simulation/ac_simulation_parameters.h"
#include "simulation/dc_simulation_parameters.h"
#include "simulation/fft_parameters.h"
#include "simulation/four_parameters.h"
#include "simulation/hb_simulation_parameters.h"
#include "simulation/ic_parameters.h"
#include "simulation/lin_simulation_parameters.h"
#include "simulation/measure_parameters.h"
#include "simulation/noise_simulation_parameters.h"
#include "simulation/op_simulation_parameters.h"
#include "simulation/option_catalog.h"
#include "simulation/pce_parameters.h"
#include "simulation/print_parameters.h"
#include "simulation/transient_simulation_parameters.h"
#include "simulation_parameters_dialog.h"
#include "simulation_ui/ac_ui.h"
#include "simulation_ui/dc_sweep_row.h"
#include "simulation_ui/dc_ui.h"
#include "simulation_ui/hb_ui.h"
#include "simulation_ui/lin_ui.h"
#include "simulation_ui/noise_ui.h"
#include "simulation_ui/op_ui.h"
#include "simulation_ui/option_row.h"
#include "simulation_ui/print_ui.h"
#include "simulation_ui/tran_ui.h"

namespace
{
    // the size the simulation dialog window is created with, before it is fitted to its page
    constexpr float DIALOG_WIDTH = 820.0f;

    // the height the dialog keeps, because the page inside it takes whatever the chrome leaves
    constexpr float DIALOG_HEIGHT = 600.0f;

    // the DC sweep modes in combo order, which the selected index names
    constexpr std::array<const char*, 5> DC_MODES = {"LIN", "DEC", "OCT", "LIST", "DATA"};

    // the AC sweep modes in combo order, where DATA has no range fields
    constexpr std::array<const char*, 4> AC_MODES = {"LIN", "DEC", "OCT", "DATA"};

    // the print types each analysis offers in its combo

    // the noise sweep types in combo order
    constexpr std::array<const char*, 4> NOISE_SWEEP_TYPES = {"LIN", "DEC", "OCT", "DATA"};

    // the save types and levels the operating point panel offers
    constexpr std::array<const char*, 2> SAVE_TYPES = {"NODESET", "IC"};
    constexpr std::array<const char*, 3> SAVE_LEVELS = {"", "all", "none"};

    // the linear analysis choices in combo order
    constexpr std::array<const char*, 2> LIN_FORMATS = {"TOUCHSTONE2", "TOUCHSTONE"};
    constexpr std::array<const char*, 3> LIN_TYPES = {"S", "Y", "Z"};
    constexpr std::array<const char*, 3> LIN_DATA_FORMATS = {"RI", "MA", "DB"};

    // the transient initial condition keywords in combo order, where the head is the default
    constexpr std::array<const char*, 3> OP_KEYWORDS = {"", "NOOP", "UIC"};

    // the print formats in combo order, where the head is the default spelling
    constexpr std::array<const char*, 9> PRINT_FORMATS = {"", "STD", "NOINDEX", "PROBE", "TECPLOT", "RAW", "CSV", "GNUPLOT", "SPLOT"};

    // the transient print types in combo order

    // the tokens a print section treats as wildcards rather than variables
    constexpr std::array<const char*, 10> WILDCARD_TOKENS = {"V(*)", "I(*)", "P(*)", "W(*)", "IB(*)", "IC(*)", "IE(*)", "IS(*)", "ID(*)", "IG(*)"};

    // the ordered BJT lead current wildcards
    constexpr std::array<const char*, 4> BJT_WILDCARDS = {"IB(*)", "IC(*)", "IE(*)", "IS(*)"};

    // the ordered FET lead current wildcards
    constexpr std::array<const char*, 4> FET_WILDCARDS = {"IB(*)", "ID(*)", "IG(*)", "IS(*)"};

    // resolve a stored mode string to its combo index, where an unknown mode reads as the head
    template <std::size_t N>
    [[nodiscard]] int choice_index(const std::array<const char*, N>& values, std::string_view value) {
        // the stored spelling is the authoritative match
        for (std::size_t index = 0; index < N; ++index) {
            // the matching entry is the position the combo shows
            if (value == values[index])
                return static_cast<int>(index);
        }
        // an unknown or empty mode falls back to the first entry
        return 0;
    }

    // split a whitespace separated value list into its tokens
    [[nodiscard]] std::vector<std::string> split_whitespace(std::string_view value) {
        // the tokens the list carries
        std::vector<std::string> parts;
        // the cursor walks the whole run
        std::size_t cursor = 0;
        // consume leading whitespace and one token per pass
        while (cursor < value.size()) {
            // skip the separator run between tokens
            while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) != 0)
                ++cursor;
            // the token starts at the first non separator byte
            const std::size_t start = cursor;
            // the token ends at the next separator or the end
            while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) == 0)
                ++cursor;
            // an empty span at the end names no token
            if (cursor > start)
                parts.emplace_back(value.substr(start, cursor - start));
        }
        return parts;
    }

    // join a value list with the single separator the field shows
    [[nodiscard]] std::string join_values(const std::vector<std::string>& values) {
        // the joined run the field edits
        std::string joined;
        // every entry is separated from the previous one by a space
        for (const std::string& value : values) {
            // a non head entry needs the separator before it
            if (!joined.empty())
                joined.push_back(' ');
            // the entry itself follows
            joined += value;
        }
        return joined;
    }

    // resolve a stored spelling to its combo index without case sensitivity
    template <std::size_t N>
    [[nodiscard]] int choice_index_ci(const std::array<const char*, N>& values, std::string_view value) {
        // the upper case spelling is what every comparison uses
        const std::string upper = to_upper(value);
        // the matching entry is the position the combo shows
        for (std::size_t index = 0; index < N; ++index) {
            // a case difference does not change the meaning
            if (to_upper(values[index]) == upper)
                return static_cast<int>(index);
        }
        // an unknown or empty value falls back to the first entry
        return 0;
    }

    // resolve a stored spelling to its combo index without case sensitivity
    [[nodiscard]] int choice_index_ci(const std::vector<std::string>& values, std::string_view value) {
        // the upper case spelling is what every comparison uses
        const std::string upper = to_upper(value);
        // the matching entry is the position the combo shows
        for (std::size_t index = 0; index < values.size(); ++index) {
            // a case difference does not change the meaning
            if (to_upper(values[index]) == upper)
                return static_cast<int>(index);
        }
        // an unknown or empty value falls back to the first entry
        return 0;
    }

    // resolve a print format to its combo index, where the head is the default spelling
    [[nodiscard]] int format_index(std::string_view format) { return choice_index_ci(PRINT_FORMATS, format); }

    // join a list with the separator the text section shows
    [[nodiscard]] std::string join_with(const std::vector<std::string>& values, std::string_view separator) {
        // the joined run the field edits
        std::string joined;
        // every entry is separated from the previous one
        for (const std::string& value : values) {
            // a non head entry needs the separator before it
            if (!joined.empty())
                joined += separator;
            // the entry itself follows
            joined += value;
        }
        return joined;
    }

    // report whether a print token is a wildcard rather than a named variable
    [[nodiscard]] bool is_wildcard(const std::string& token) {
        // every wildcard the print section knows is a known token
        for (const char* wildcard : WILDCARD_TOKENS) {
            // the exact spelling is what counts
            if (token == wildcard)
                return true;
        }
        return false;
    }

    // append a wildcard once, because two lead groups may overlap
    void add_unique(std::vector<std::string>& values, const std::string& value) {
        // an already present token is not added twice
        if (std::find(values.begin(), values.end(), value) == values.end())
            values.push_back(value);
    }

    // parse the .MEASURE directives, one per line
    [[nodiscard]] std::vector<MeasureEntry> parse_measure_lines(std::string_view text) {
        // the entries the lines describe
        std::vector<MeasureEntry> measures;
        // every line is one directive
        for (const std::string_view line : split_by(text, '\n')) {
            // a blank line names no directive
            if (line.find_first_not_of(" \t\r") == std::string_view::npos)
                continue;
            // the core parser owns the directive grammar
            if (std::optional<MeasureEntry> parsed = MeasureEntry::from_xyce_statement(std::string(line)))
                measures.push_back(std::move(*parsed));
        }
        return measures;
    }

    // format the .MEASURE directives, one per line
    [[nodiscard]] std::string format_measure_lines(const std::vector<MeasureEntry>& measures) {
        // the directive spellings the editor shows
        std::vector<std::string> lines;
        // every entry is one line
        for (const MeasureEntry& measure : measures)
            lines.push_back(measure.to_xyce_statement());
        return join_with(lines, "\n");
    }

    // parse the .FFT directives, one per line
    [[nodiscard]] std::vector<FftParameters> parse_fft_lines(std::string_view text) {
        // the entries the lines describe
        std::vector<FftParameters> ffts;
        // every line is one directive
        for (const std::string_view line : split_by(text, '\n')) {
            // a blank line names no directive
            if (line.find_first_not_of(" \t\r") == std::string_view::npos)
                continue;
            // the core parser owns the directive grammar
            if (std::optional<FftParameters> parsed = FftParameters::from_xyce_statement(std::string(line)))
                ffts.push_back(std::move(*parsed));
        }
        return ffts;
    }

    // format the .FFT directives, one per line
    [[nodiscard]] std::string format_fft_lines(const std::vector<FftParameters>& ffts) {
        // the directive spellings the editor shows
        std::vector<std::string> lines;
        // every entry is one line
        for (const FftParameters& fft : ffts)
            lines.push_back(fft.to_xyce_statement());
        return join_with(lines, "\n");
    }

    // parse the .FOUR directives, one per line
    [[nodiscard]] std::vector<FourParameters> parse_four_lines(std::string_view text) {
        // the entries the lines describe
        std::vector<FourParameters> fours;
        // every line is one directive
        for (const std::string_view line : split_by(text, '\n')) {
            // a blank line names no directive
            if (line.find_first_not_of(" \t\r") == std::string_view::npos)
                continue;
            // the core parser owns the directive grammar
            if (std::optional<FourParameters> parsed = FourParameters::from_xyce_statement(std::string(line)))
                fours.push_back(std::move(*parsed));
        }
        return fours;
    }

    // format the .FOUR directives, one per line
    [[nodiscard]] std::string format_four_lines(const std::vector<FourParameters>& fours) {
        // the directive spellings the editor shows
        std::vector<std::string> lines;
        // every entry is one line
        for (const FourParameters& four : fours)
            lines.push_back(four.to_xyce_statement());
        return join_with(lines, "\n");
    }

    // parse schedule points from the "time, max_step" text
    [[nodiscard]] std::vector<TransientSchedulePoint> parse_schedule_text(std::string_view text) {
        // the points the lines describe
        std::vector<TransientSchedulePoint> points;
        // every line is one pair
        for (const std::string_view line_view : split_by(text, '\n')) {
            // the trimmed line is what the comma splits
            const std::string line = trim(line_view);
            // a blank line names no pair
            if (line.empty())
                continue;
            // the comma separates the time from the ceiling
            const std::size_t comma = line.find(',');
            // a line without a comma names no pair
            if (comma == std::string::npos)
                continue;
            // the time half is what precedes the comma
            const std::string time_part = trim(line.substr(0, comma));
            // the ceiling half is what follows the comma
            const std::string step_part = trim(line.substr(comma + 1));
            // both halves must carry a value
            if (!time_part.empty() && !step_part.empty())
                points.emplace_back(time_part, step_part);
        }
        return points;
    }

    // format schedule points as "time, max_step" lines
    [[nodiscard]] std::string format_schedule_text(const std::vector<TransientSchedulePoint>& points) {
        // the pairs the editor shows
        std::vector<std::string> lines;
        // every point is one pair
        for (const TransientSchedulePoint& point : points)
            lines.push_back(point.time_value + ", " + point.max_time_step_value);
        return join_with(lines, "\n");
    }

    // the standard print formats in combo order, where the head is the default spelling
    const std::vector<std::string>& standard_print_formats() {
        // the list is built once, because every analysis shares it
        static const std::vector<std::string> formats = {"(default)", "STD", "NOINDEX", "PROBE", "TECPLOT", "RAW", "CSV", "GNUPLOT", "SPLOT"};
        return formats;
    }

    // the PCE print formats, which omit PROBE and RAW
    const std::vector<std::string>& pce_print_formats() {
        // the list is built once, because both PCE sections share it
        static const std::vector<std::string> formats = {"(default)", "STD", "NOINDEX", "TECPLOT", "CSV", "GNUPLOT", "SPLOT"};
        return formats;
    }

    // seed the shared print card from the stored parameters
    void seed_print(const std::optional<PrintParameters>& params, bool show_power, bool has_bjt, bool has_fet, const std::vector<std::string>& type_items, albor::PrintUi& print, const std::vector<std::string>& format_items = standard_print_formats()) {
        // an absent print disables the card
        if (!params) {
            print.enabled = false;
            return;
        }
        // the stored parameters are what the card mirrors
        const PrintParameters& stored = *params;
        // the card starts enabled because a print exists
        print.enabled = true;
        // the wildcard tokens are the checkbox states
        print.all_nodes = std::find(stored.output_variables.begin(), stored.output_variables.end(), "V(*)") != stored.output_variables.end();
        print.all_currents = std::find(stored.output_variables.begin(), stored.output_variables.end(), "I(*)") != stored.output_variables.end();
        print.power = show_power && std::find(stored.output_variables.begin(), stored.output_variables.end(), "P(*)") != stored.output_variables.end();
        print.bjt_leads = has_bjt && (std::find(stored.output_variables.begin(), stored.output_variables.end(), "IC(*)") != stored.output_variables.end() || std::find(stored.output_variables.begin(), stored.output_variables.end(), "IE(*)") != stored.output_variables.end());
        print.fet_leads = has_fet && (std::find(stored.output_variables.begin(), stored.output_variables.end(), "ID(*)") != stored.output_variables.end() || std::find(stored.output_variables.begin(), stored.output_variables.end(), "IG(*)") != stored.output_variables.end());
        // every named variable is the additional-variables text
        std::vector<std::string> specific;
        // a wildcard is already a checkbox, so it is not repeated in the text
        for (const std::string& variable : stored.output_variables) {
            // a named token belongs in the text
            if (!is_wildcard(variable))
                specific.push_back(variable);
        }
        // the text the user edits
        print.specific_variables = join_with(specific, " ");
        // the stored format is the combo position
        print.format_index = choice_index_ci(format_items, stored.print_format);
        // the stored file is the text field
        print.output_file = stored.print_file;
        // the stored extra options are the text field
        print.extra_options = join_with(stored.extra_options, " ");
        // the stored type is the combo position
        print.type_index = choice_index_ci(type_items, stored.print_type);
    }

    // build the stored print parameters from the shared card
    [[nodiscard]] std::optional<PrintParameters> build_print(const albor::PrintUi& print, bool show_power, bool has_bjt, bool has_fet, std::string_view default_type, const std::vector<std::string>& type_items, const std::vector<std::string>& format_items = standard_print_formats()) {
        // a disabled card stores no print
        if (!print.enabled)
            return std::nullopt;
        // the variables the print carries
        std::vector<std::string> variables;
        // the checkbox wildcards come first
        if (print.all_nodes)
            variables.push_back("V(*)");
        // the current wildcard follows the voltages
        if (print.all_currents)
            variables.push_back("I(*)");
        // the power wildcard follows when the analysis prints it
        if (show_power && print.power)
            variables.push_back("P(*)");
        // the lead groups expand into their ordered wildcards
        if (has_bjt && print.bjt_leads) {
            // every BJT lead prints once
            for (const char* wildcard : BJT_WILDCARDS)
                add_unique(variables, wildcard);
        }
        // every FET lead prints once
        if (has_fet && print.fet_leads) {
            // the FET group follows the BJT group
            for (const char* wildcard : FET_WILDCARDS)
                add_unique(variables, wildcard);
        }
        // the named variables follow the wildcards
        for (std::string& variable : tokenize_owned(print.specific_variables))
            variables.push_back(std::move(variable));
        // the format position names the stored spelling, where the head is empty
        const std::string format = (print.format_index > 0 && print.format_index < static_cast<int>(format_items.size())) ? format_items[static_cast<std::size_t>(print.format_index)] : "";
        // the type position names the stored spelling
        const std::string type = type_items.empty() ? std::string(default_type) : type_items[static_cast<std::size_t>(std::clamp(print.type_index, 0, static_cast<int>(type_items.size()) - 1))];
        // the optional file and the extra tokens finish the print
        return PrintParameters(type, format, strip_outer_quotes(print.output_file), std::move(variables), tokenize_owned(print.extra_options));
    }

    // parse the key=value option lines into a map
    [[nodiscard]] std::map<std::string, std::string> parse_key_values(std::string_view text);

    // format the option map as the key=value lines the field shows
    [[nodiscard]] std::string format_key_values(const std::map<std::string, std::string>& options);

    // project the stored PCE parameters into the keyed row list the table edits
    [[nodiscard]] std::vector<albor::PceRow> pce_rows_from_parameters(const PceParameters& params) {
        // the rows the editor shows
        std::vector<albor::PceRow> rows;
        // the per-type positional index for the value lists
        std::size_t uniform_index = 0;
        std::size_t normal_index = 0;
        std::size_t gamma_index = 0;
        // every parameter is one row
        for (std::size_t index = 0; index < params.parameters.size(); ++index) {
            // the stored distribution type decides which value pair applies
            const std::string type = index < params.distribution_types.size() ? to_lower(params.distribution_types[index]) : "";
            // the row starts with its stable key
            albor::PceRow row;
            row.key = std::to_string(index + 1);
            row.name = params.parameters[index];
            // the distribution index is what the combo shows
            row.type_index = type == "uniform" ? 0 : (type == "normal" ? 1 : 2);
            // a uniform row reads its lower and upper bounds
            if (type == "uniform") {
                row.first_value = uniform_index < params.lower_bounds.size() ? params.lower_bounds[uniform_index] : "";
                row.second_value = uniform_index < params.upper_bounds.size() ? params.upper_bounds[uniform_index] : "";
                ++uniform_index;
            }
            // a normal row reads its mean and standard deviation
            else if (type == "normal") {
                row.first_value = normal_index < params.means.size() ? params.means[normal_index] : "";
                row.second_value = normal_index < params.std_deviations.size() ? params.std_deviations[normal_index] : "";
                ++normal_index;
            }
            // a gamma row reads its alpha and beta
            else if (type == "gamma") {
                row.first_value = gamma_index < params.alphas.size() ? params.alphas[gamma_index] : "";
                row.second_value = gamma_index < params.betas.size() ? params.betas[gamma_index] : "";
                ++gamma_index;
            }
            // the row joins the list
            rows.push_back(std::move(row));
        }
        return rows;
    }

    // seed the PCE section from the stored parameters
    void seed_pce(const std::optional<PceParameters>& params, albor::PceUi& pce, albor::PrintUi& print) {
        // an absent PCE disables the section
        if (!params) {
            pce.enabled = false;
            pce.use_expression = false;
            pce.pces_options = "";
            seed_print(std::nullopt, false, false, false, {}, print, pce_print_formats());
            return;
        }
        // the section starts enabled because a PCE exists
        pce.enabled = true;
        pce.use_expression = params->use_expression;
        pce.pces_options = format_key_values(params->pces_options);
        // the print card mirrors the PCE print
        seed_print(params->print_parameters, false, false, false, {}, print, pce_print_formats());
    }

    // build the PCE parameters from the section state
    [[nodiscard]] std::optional<PceParameters> build_pce(const albor::PceUi& pce, const std::vector<albor::PceRow>& rows, const albor::PrintUi& print) {
        // a disabled section stores no PCE
        if (!pce.enabled)
            return std::nullopt;
        // the parallel lists the core stores
        std::vector<std::string> parameters;
        std::vector<std::string> distribution_types;
        std::vector<std::string> means;
        std::vector<std::string> std_deviations;
        std::vector<std::string> lower_bounds;
        std::vector<std::string> upper_bounds;
        std::vector<std::string> alphas;
        std::vector<std::string> betas;
        // expression-based random inputs ignore the parameter table
        if (!pce.use_expression) {
            // every row is one parameter
            for (const albor::PceRow& row : rows) {
                // a named row is a parameter
                const std::string name = trim(row.name);
                // an unnamed row names no parameter
                if (name.empty())
                    continue;
                // the distribution index is what the combo shows
                const int type_index = std::clamp(row.type_index, 0, 2);
                // the stored spelling is what the core reads
                const std::string type = type_index == 0 ? "uniform" : (type_index == 1 ? "normal" : "gamma");
                // the name and type join the lists
                parameters.push_back(name);
                distribution_types.push_back(type);
                // the value pair joins the type-specific lists
                if (type == "uniform") {
                    lower_bounds.push_back(trim(row.first_value));
                    upper_bounds.push_back(trim(row.second_value));
                }
                else if (type == "normal") {
                    means.push_back(trim(row.first_value));
                    std_deviations.push_back(trim(row.second_value));
                }
                else {
                    alphas.push_back(trim(row.first_value));
                    betas.push_back(trim(row.second_value));
                }
            }
        }
        // the PCES package entries are key=value lines
        std::map<std::string, std::string> pces_options = parse_key_values(pce.pces_options);
        // the print card builds the PCE print
        std::optional<PrintParameters> print_parameters = build_print(print, false, false, false, "PCE", {}, pce_print_formats());
        // the section state builds the parameters
        return PceParameters(pce.use_expression, std::move(parameters), std::move(distribution_types), std::move(means), std::move(std_deviations), std::move(lower_bounds), std::move(upper_bounds), std::move(alphas), std::move(betas), std::move(pces_options), std::move(print_parameters));
    }

    // the combo entries of one option row, where the head clears the option and a bare flag keeps its marker
    [[nodiscard]] std::vector<std::string> option_choices(const OptionKeyInfo& info, bool bare_flag) {
        // a free key with no flag renders as a text field, which carries no entries
        std::vector<std::string> choices;
        // a documented choice list and a bare flag are what a combo exists for
        if (info.choices.empty() && !bare_flag)
            return choices;
        // the head clears the option, which is the reference's "<default>" entry
        choices.push_back("<default>");
        // a loaded bare flag keeps its marker so the user can keep or clear it
        if (bare_flag)
            choices.push_back("flag is set");
        // the documented values follow the marker
        choices.insert(choices.end(), info.choices.begin(), info.choices.end());
        return choices;
    }

    // the entry one row's current value selects, where -1 keeps the text editor
    [[nodiscard]] int option_choice_index(const std::vector<std::string>& choices, const std::string& value, bool bare_flag) {
        // a row without entries renders as text
        if (choices.empty())
            return -1;
        // a bare flag selects the marker, which sits right after the default
        if (bare_flag)
            return 1;
        // the head clears the option
        if (value.empty())
            return 0;
        // the documented value sits after the default and the possible marker
        const std::size_t offset = choices.size() > 1 && choices[1] == "flag is set" ? 2 : 1;
        for (std::size_t index = offset; index < choices.size(); ++index) {
            // the matching entry is the one the combo shows
            if (choices[index] == value)
                return static_cast<int>(index);
        }
        // a value the guide does not list falls back to the text editor, so it is never lost
        return -1;
    }

    // build the editor rows of one managed .OPTIONS package
    [[nodiscard]] std::vector<albor::OptionRow> build_option_rows(OptionPackage package, const std::map<std::string, std::string>& loaded) {
        // the rows the card shows
        std::vector<albor::OptionRow> rows;
        // every documented key comes first, so the card order is stable
        const std::vector<std::string>& catalog = option_package_catalog(package);
        // reserve the catalog and the possible netlist-only keys
        rows.reserve(catalog.size() + loaded.size());
        // one row per documented key
        for (const std::string& key : catalog) {
            // the loaded value is what the netlist carries, when it carries the key
            const auto found = loaded.find(key);
            // a present key is what the row mirrors
            const bool present = found != loaded.end();
            // the value is empty for an absent key
            const std::string value = present ? found->second : "";
            // the documentation decides the entries and the reference default
            const OptionKeyInfo info = option_key_info(package, key);
            // a present key with no value is a bare flag
            const bool bare_flag = present && value.empty();
            // the row carries the key, value, bare-flag state and reference default
            albor::OptionRow row;
            row.key = key;
            row.value = value;
            row.flag = bare_flag;
            row.default_value = info.default_value;
            // a documented choice list or a bare flag is what the combo shows
            row.choices = option_choices(info, bare_flag);
            row.choice_index = option_choice_index(row.choices, value, bare_flag);
            rows.push_back(std::move(row));
        }
        // a key the catalog does not document still shows, so no loaded option is hidden
        const std::set<std::string> placed(catalog.begin(), catalog.end());
        // every loaded key outside the catalog becomes one row
        for (const auto& [key, value] : loaded) {
            // a documented key already has a row
            if (placed.count(key) != 0)
                continue;
            // the row carries the netlist key and value
            albor::OptionRow row;
            row.key = key;
            row.value = value;
            row.flag = value.empty();
            rows.push_back(std::move(row));
        }
        return rows;
    }

    // apply the combo entry the user picked to the row it belongs to
    void select_option_choice(albor::OptionRow& row, int index) {
        // an index outside the entries describes nothing
        if (index < 0 || static_cast<std::size_t>(index) >= row.choices.size())
            return;
        // the entry the user picked
        const std::string& choice = row.choices[static_cast<std::size_t>(index)];
        // the head clears the option, which writes neither a value nor a flag
        if (index == 0) {
            row.value = "";
            row.flag = false;
        }
        // the marker keeps the key without a value
        else if (choice == "flag is set") {
            row.value = "";
            row.flag = true;
        }
        // a documented value is written verbatim
        else {
            row.value = choice;
            row.flag = false;
        }
        // the combo shows the picked entry
        row.choice_index = index;
    }

    // fold the editor rows of one package back into its option map
    [[nodiscard]] std::map<std::string, std::string> apply_option_rows(const std::vector<albor::OptionRow>& rows) {
        // the options the rows describe
        std::map<std::string, std::string> options;
        // every row is one option
        for (const albor::OptionRow& row : rows) {
            // the key is uppercased, which is how the catalog and the parser spell it
            const std::string key = to_upper(trim(row.key));
            // a row without a key names no option
            if (key.empty())
                continue;
            // the value is trimmed, because the field may carry surrounding space
            const std::string value = trim(row.value);
            // a non-empty value writes KEY=VALUE
            if (!value.empty())
                options[key] = value;
            // a bare flag keeps the key without a value
            else if (row.flag)
                options[key] = "";
        }
        return options;
    }

    // seed the transient panel from the configuration a run uses
    void seed_transient(const SimulationConfig& current, albor::TranUi& tran, albor::PrintUi& print) {
        // the ground flag and the initial conditions are global to the run
        tran.replace_ground = current.replace_ground;
        tran.initial_conditions = current.ic_parameters.to_line();
        // a transient analysis fills the timing fields and the companion sections
        if (const auto* stored = std::get_if<TransientSimulationParameters>(&current.analysis)) {
            // the timing fields the netlist carries
            tran.initial_step = stored->initial_step_value;
            tran.final_time = stored->final_time_value;
            tran.start_time = stored->start_time_value;
            tran.step_ceiling = stored->step_ceiling_value;
            // the keyword is the combo position
            tran.op_keyword_index = choice_index(OP_KEYWORDS, stored->op_keyword);
            // the companion sections are edited as text
            tran.schedule = format_schedule_text(stored->schedule_points);
            tran.fft = format_fft_lines(stored->fft_parameters);
            tran.four = format_four_lines(stored->four_parameters);
            tran.measure = format_measure_lines(stored->measure_parameters);
            // the managed option packages are edited as key=value lines
            tran.timeint_options = format_key_values(current.options.timeint);
            tran.nonlin_tran_options = format_key_values(current.options.nonlin_tran);
            tran.loca_options = format_key_values(current.options.loca);
            tran.fft_options = format_key_values(current.options.fft);
            tran.output_options = format_key_values(current.options.output);
            // the print card mirrors the stored print
            seed_print(stored->print_parameters, true, true, true, {"TRAN", "TRANADJOINT"}, print);
            return;
        }
        // an analysis without a transient print starts with the card disabled
        seed_print(std::nullopt, true, true, true, {"TRAN", "TRANADJOINT"}, print);
    }

    // build the transient analysis from the panel state
    [[nodiscard]] TransientSimulationParameters build_transient(const albor::TranUi& tran, const albor::PrintUi& print, const albor::PceUi& pce_state, const std::vector<albor::PceRow>& pce_rows, const albor::PrintUi& pce_print, const SimulationConfig& seed) {
        // the keyword is the combo position, which the model bounds
        const int op_index = std::clamp(tran.op_keyword_index, 0, static_cast<int>(OP_KEYWORDS.size()) - 1);
        // the stored keyword is what the serializer writes
        const std::string op_keyword = OP_KEYWORDS[static_cast<std::size_t>(op_index)];
        // the companion this dialog does not edit survives the timing edit
        std::optional<SensParameter> sensitivity;
        // a previous transient analysis carries the companion
        if (const auto* previous = std::get_if<TransientSimulationParameters>(&seed.analysis))
            sensitivity = previous->sensitivity;
        // the panel state builds the analysis
        return TransientSimulationParameters(trim(tran.initial_step), trim(tran.final_time), trim(tran.start_time), trim(tran.step_ceiling), op_keyword, parse_schedule_text(tran.schedule), build_print(print, true, true, true, "TRAN", {"TRAN", "TRANADJOINT"}), parse_fft_lines(tran.fft), parse_four_lines(tran.four), parse_measure_lines(tran.measure), std::move(sensitivity), build_pce(pce_state, pce_rows, pce_print));
    }

    // format a print format index as the stored spelling
    [[nodiscard]] std::string print_format_text(int index) {
        // the head is the empty default, and an out of range index names nothing
        if (index <= 0 || index >= static_cast<int>(PRINT_FORMATS.size()))
            return "";
        // the model entry is the stored spelling
        return PRINT_FORMATS[static_cast<std::size_t>(index)];
    }

    // parse the nodeset text into entries
    [[nodiscard]] std::vector<NodesetEntry> parse_nodeset_text(std::string_view text) {
        // the entries the tokens describe
        std::vector<NodesetEntry> entries;
        // the tokens are space separated V(node)=value pairs
        for (std::string& token : tokenize_owned(text)) {
            // the equals sign separates the node from the value
            const std::size_t equals = token.find('=');
            // a token without an equals sign names no pair
            if (equals == std::string::npos)
                continue;
            // the node is what precedes the equals sign
            std::string node = trim(std::string_view(token).substr(0, equals));
            // a V(node) spelling stores the inner node, which is what the serializer takes
            if (node.size() >= 3 && node.front() == 'V' && node.back() == ')')
                node = node.substr(2, node.size() - 3);
            // the value is what follows the equals sign
            const std::string value = trim(std::string_view(token).substr(equals + 1));
            // both halves must carry a value
            if (!node.empty() && !value.empty())
                entries.emplace_back(node, value);
        }
        return entries;
    }

    // format the nodeset entries as the tokens the field edits
    [[nodiscard]] std::string format_nodeset_text(const std::vector<NodesetEntry>& entries) {
        // the tokens the editor shows
        std::vector<std::string> tokens;
        // every entry is one pair
        for (const NodesetEntry& entry : entries) {
            // a bare voltage node name keeps its spelling
            if (!entry.node.empty() && (entry.node[0] == 'V' || entry.node[0] == 'v'))
                tokens.push_back(entry.node + "=" + entry.voltage);
            // every other node takes the V(node) form the directive uses
            else
                tokens.push_back("V(" + entry.node + ")=" + entry.voltage);
        }
        return join_with(tokens, " ");
    }

    // parse the device noise operators, one per line
    [[nodiscard]] std::vector<DeviceNoiseOperator> parse_device_noise(std::string_view text) {
        // the operators the lines describe
        std::vector<DeviceNoiseOperator> operators;
        // every line is one operator
        for (const std::string_view line : split_by(text, '\n')) {
            // the line is split into its three fields
            const std::vector<std::string> tokens = tokenize_owned(line);
            // a complete operator carries the type, the node and the source
            if (tokens.size() >= 3)
                operators.emplace_back(tokens[0], tokens[1], tokens[2]);
        }
        return operators;
    }

    // format the device noise operators, one per line
    [[nodiscard]] std::string format_device_noise(const std::vector<DeviceNoiseOperator>& operators) {
        // the lines the editor shows
        std::vector<std::string> lines;
        // every operator is one line
        for (const DeviceNoiseOperator& device : operators)
            lines.push_back(device.type + " " + device.node + " " + device.source);
        return join_with(lines, "\n");
    }

    // parse a comma or space separated integer list
    [[nodiscard]] std::vector<int> parse_int_list(std::string_view text) {
        // the integers the tokens describe
        std::vector<int> values;
        // a comma and a space both separate the entries
        for (std::string& token : tokenize_owned(text)) {
            // the token may carry a trailing comma
            if (!token.empty() && token.back() == ',')
                token.pop_back();
            // an empty token names no value
            if (token.empty())
                continue;
            // the parsed integer is what the analysis stores
            int value = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            // a full parse is what counts
            if (result.ec == std::errc{})
                values.push_back(value);
        }
        return values;
    }

    // format an integer list as the comma separated text the field shows
    [[nodiscard]] std::string format_int_list(const std::vector<int>& values) {
        // the tokens the editor shows
        std::vector<std::string> tokens;
        // every integer is one token
        for (int value : values)
            tokens.push_back(std::to_string(value));
        return join_with(tokens, ",");
    }

    // parse an optional integer, where blank text names nothing
    [[nodiscard]] std::optional<int> parse_optional_int(std::string_view text) {
        // the trimmed text is what the parser reads
        const std::string value = trim(text);
        // a blank field names no value
        if (value.empty())
            return std::nullopt;
        // the parsed integer is what the analysis stores
        int parsed = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        // a full parse is what counts
        if (result.ec == std::errc{})
            return parsed;
        return std::nullopt;
    }

    // parse the key=value option lines into a map
    [[nodiscard]] std::map<std::string, std::string> parse_key_values(std::string_view text) {
        // the options the lines describe
        std::map<std::string, std::string> options;
        // every line is one option
        for (const std::string_view line : split_by(text, '\n')) {
            // the equals sign separates the key from the value
            const std::size_t equals = line.find('=');
            // a line without an equals sign names no option
            if (equals == std::string_view::npos)
                continue;
            // the key is the upper case spelling the core uses
            const std::string key = to_upper(trim(line.substr(0, equals)));
            // the value is what follows the equals sign
            const std::string value = trim(line.substr(equals + 1));
            // an empty key names no option
            if (!key.empty())
                options[key] = value;
        }
        return options;
    }

    // format the option map as the key=value lines the field shows
    [[nodiscard]] std::string format_key_values(const std::map<std::string, std::string>& options) {
        // the lines the editor shows
        std::vector<std::string> lines;
        // every option is one line
        for (const auto& [key, value] : options)
            lines.push_back(key + "=" + value);
        return join_with(lines, "\n");
    }

    // seed the DC panel from the configuration a run uses
    void seed_dc(const SimulationConfig& current, albor::DcUi& dc, albor::PrintUi& print) {
        // the ground flag and the initial conditions are global to the run
        dc.replace_ground = current.replace_ground;
        dc.initial_conditions = current.ic_parameters.to_line();
        // a DC analysis fills the sweep fields and the companion sections
        if (const auto* stored = std::get_if<DCSimulationParameters>(&current.analysis)) {
            // the sweep mode is the combo position
            dc.sweep_mode_index = choice_index(DC_MODES, stored->sweep_mode);
            // the table is what a DATA sweep reads
            dc.data_table = stored->data_table_name;
            // the measure directives are edited as text
            dc.measure = format_measure_lines(stored->measure_parameters);
            // the managed option package is edited as key=value lines
            dc.nonlin_options = format_key_values(current.options.nonlin);
            // the slice edits the first sweep tuple
            if (!stored->sweeps.empty()) {
                // the tuple the fields mirror
                const DcSweep& sweep = stored->sweeps.front();
                // the tuple fields
                dc.variable = sweep.variable;
                dc.start = sweep.start;
                dc.stop = sweep.stop;
                dc.step = sweep.step;
                dc.points = sweep.points;
                dc.list = join_values(sweep.list_values);
            }
            // the print card mirrors the stored print
            seed_print(stored->print_parameters, true, true, true, {"DC", "HOMOTOPY"}, print);
            return;
        }
        // an analysis without a DC print starts with the card disabled
        seed_print(std::nullopt, true, true, true, {"DC", "HOMOTOPY"}, print);
    }

    // build the DC analysis from the panel state
    [[nodiscard]] DCSimulationParameters build_dc(const albor::DcUi& dc, const std::vector<albor::DcSweepRow>& rows, const albor::PrintUi& print, const albor::PceUi& pce_state, const std::vector<albor::PceRow>& pce_rows, const albor::PrintUi& pce_print, const SimulationConfig& seed) {
        // the mode is the combo position, which the model bounds
        const int mode_index = std::clamp(dc.sweep_mode_index, 0, static_cast<int>(DC_MODES.size()) - 1);
        // the stored spelling is what the serializer writes
        const std::string mode = DC_MODES[static_cast<std::size_t>(mode_index)];
        // the sweep tuples the configuration carries
        std::vector<DcSweep> sweeps;
        // a DATA sweep names a table instead of a range
        std::string table;
        // the range fields only apply to the modes that sweep a variable
        if (mode == "DATA") {
            // the table the DATA sweep reads is the only input
            table = trim(dc.data_table);
        }
        else {
            // every keyed row becomes one sweep tuple
            for (const albor::DcSweepRow& row : rows) {
                // the tuple the row describes
                DcSweep sweep;
                // every mode but DATA names the swept variable
                sweep.variable = trim(row.variable);
                // the linear and logarithmic modes carry a start and a stop
                if (mode == "LIN" || mode == "DEC" || mode == "OCT") {
                    // the first value the sweep walks from
                    sweep.start = trim(row.start);
                    // the last value the sweep walks to
                    sweep.stop = trim(row.stop);
                }
                // only a linear sweep carries a step size
                if (mode == "LIN")
                    sweep.step = trim(row.step);
                // the logarithmic modes carry a point count
                if (mode == "DEC" || mode == "OCT")
                    sweep.points = trim(row.points);
                // a LIST sweep carries the explicit values
                if (mode == "LIST")
                    sweep.list_values = split_whitespace(row.list_values);
                // the tuple joins the configuration
                sweeps.push_back(std::move(sweep));
            }
        }
        // the companion this dialog does not edit survives the sweep edit
        std::optional<SensParameter> sensitivity;
        // a previous DC analysis carries the companion
        if (const auto* previous = std::get_if<DCSimulationParameters>(&seed.analysis))
            sensitivity = previous->sensitivity;
        // the panel state builds the analysis
        return DCSimulationParameters(mode, std::move(sweeps), std::move(table), build_print(print, true, true, true, "DC", {"DC", "HOMOTOPY"}), parse_measure_lines(dc.measure), std::move(sensitivity), build_pce(pce_state, pce_rows, pce_print));
    }

    // seed the AC panel from the configuration a run uses
    void seed_ac(const SimulationConfig& current, albor::AcUi& ac, albor::PrintUi& print) {
        // the ground flag is global to the run
        ac.replace_ground = current.replace_ground;
        // an AC analysis fills the sweep fields and the companion sections
        if (const auto* stored = std::get_if<AcSimulationParameters>(&current.analysis)) {
            // the sweep mode is the combo position
            ac.sweep_mode_index = choice_index(AC_MODES, stored->sweep_mode);
            // the range fields
            ac.points = stored->points;
            ac.start = stored->start;
            ac.end = stored->end;
            // the table is what a DATA sweep reads
            ac.data_table = stored->data_table_name;
            // the measure directives are edited as text
            ac.measure = format_measure_lines(stored->measure_parameters);
            // the managed option package is edited as key=value lines
            ac.linsol_options = format_key_values(current.options.linsol_ac);
            // the print card mirrors the stored print
            seed_print(stored->print_parameters, false, false, false, {"AC", "AC_IC"}, print);
            return;
        }
        // an analysis without an AC print starts with the card disabled
        seed_print(std::nullopt, false, false, false, {"AC", "AC_IC"}, print);
    }

    // build the AC analysis from the panel state
    [[nodiscard]] AcSimulationParameters build_ac(const albor::AcUi& ac, const albor::PrintUi& print, const SimulationConfig& seed) {
        // the mode is the combo position, which the model bounds
        const int mode_index = std::clamp(ac.sweep_mode_index, 0, static_cast<int>(AC_MODES.size()) - 1);
        // the stored spelling is what the serializer writes
        const std::string mode = AC_MODES[static_cast<std::size_t>(mode_index)];
        // the range fields the non DATA modes carry
        std::string points;
        std::string start;
        std::string end;
        // a DATA sweep names a table instead of a range
        std::string table;
        // the range fields only apply to the modes that sweep a range
        if (mode == "DATA") {
            // the table the DATA sweep reads is the only input
            table = trim(ac.data_table);
        }
        else {
            // the density of the sweep
            points = trim(ac.points);
            // the first frequency the sweep walks from
            start = trim(ac.start);
            // the last frequency the sweep walks to
            end = trim(ac.end);
        }
        // the companions this dialog does not edit survive the sweep edit
        std::optional<SensParameter> sensitivity;
        // a previous AC analysis carries the companions
        if (const auto* previous = std::get_if<AcSimulationParameters>(&seed.analysis))
            sensitivity = previous->sensitivity;
        // the panel state builds the analysis
        return AcSimulationParameters(mode, std::move(points), std::move(start), std::move(end), std::move(table), build_print(print, false, false, false, "AC", {"AC", "AC_IC"}), parse_measure_lines(ac.measure), std::move(sensitivity));
    }

    // seed the operating point panel from the configuration a run uses
    void seed_op(const SimulationConfig& current, albor::OpUi& op, albor::PrintUi& print) {
        // the ground flag and the initial conditions are global to the run
        op.replace_ground = current.replace_ground;
        op.initial_conditions = current.ic_parameters.to_line();
        // an operating point analysis fills the save fields and the companion sections
        if (const auto* stored = std::get_if<OpSimulationParameters>(&current.analysis)) {
            // the save fields
            op.save_enabled = stored->save_enabled;
            op.save_type_index = choice_index(SAVE_TYPES, stored->save_type);
            op.save_file = stored->save_file;
            op.save_level_index = choice_index(SAVE_LEVELS, stored->save_level);
            // the convergence hints are edited as text
            op.nodeset = format_nodeset_text(stored->nodeset_entries);
            // the managed option package is edited as key=value lines
            op.nonlin_options = format_key_values(current.options.nonlin);
            // the structured print wins, and the legacy fields are the fallback
            if (stored->print_parameters) {
                // the print card mirrors the structured print
                seed_print(stored->print_parameters, true, true, true, {}, print);
            }
            else {
                // the legacy fields seed the card directly
                print.enabled = stored->print_dc_enabled;
                print.all_nodes = stored->print_dc_all_nodes;
                print.all_currents = stored->print_dc_all_currents;
                print.power = false;
                print.bjt_leads = false;
                print.fet_leads = false;
                print.specific_variables = join_with(stored->print_dc_specific_variables, " ");
                print.format_index = format_index(stored->print_dc_format);
                print.output_file = stored->print_dc_file;
                print.extra_options = "";
                print.type_index = 0;
            }
            return;
        }
        // an analysis without an OP print starts with the card disabled
        seed_print(std::nullopt, true, true, true, {}, print);
    }

    // build the operating point analysis from the panel state
    [[nodiscard]] OpSimulationParameters build_op(const albor::OpUi& op, const albor::PrintUi& print) {
        // the save type is the combo position, which the model bounds
        const int type_index = std::clamp(op.save_type_index, 0, static_cast<int>(SAVE_TYPES.size()) - 1);
        // the save level is the combo position, which the model bounds
        const int level_index = std::clamp(op.save_level_index, 0, static_cast<int>(SAVE_LEVELS.size()) - 1);
        // the print card builds the structured print
        std::optional<PrintParameters> print_parameters = build_print(print, true, true, true, "DC", {});
        // the legacy OP fields mirror the same print
        return OpSimulationParameters(print.enabled, print.all_nodes, print.all_currents, tokenize_owned(print.specific_variables), print_format_text(print.format_index), strip_outer_quotes(print.output_file), op.save_enabled, SAVE_TYPES[static_cast<std::size_t>(type_index)], op.save_file, parse_nodeset_text(op.nodeset), std::move(print_parameters), SAVE_LEVELS[static_cast<std::size_t>(level_index)]);
    }

    // seed the noise panel from the configuration a run uses
    void seed_noise(const SimulationConfig& current, albor::NoiseUi& noise, albor::PrintUi& print) {
        // the ground flag is global to the run
        noise.replace_ground = current.replace_ground;
        // a noise analysis fills the source fields and the companion sections
        if (const auto* stored = std::get_if<NoiseSimulationParameters>(&current.analysis)) {
            // the source fields
            noise.output_node = stored->output_node;
            noise.ref_node = stored->ref_node;
            noise.source_name = stored->source_name;
            noise.start_freq = stored->start_freq_value;
            noise.end_freq = stored->end_freq_value;
            noise.num_points = stored->num_points_value;
            noise.sweep_type_index = choice_index(NOISE_SWEEP_TYPES, stored->sweep_type);
            noise.data_table = stored->data_table_name;
            // the device noise operators are edited as text
            noise.device_noise = format_device_noise(stored->device_noise_operators);
            // the print card mirrors the stored print
            seed_print(stored->print_parameters, false, false, false, {"NOISE"}, print);
            return;
        }
        // an analysis without a noise print starts with the card disabled
        seed_print(std::nullopt, false, false, false, {"NOISE"}, print);
    }

    // build the noise analysis from the panel state
    [[nodiscard]] NoiseSimulationParameters build_noise(const albor::NoiseUi& noise, const albor::PrintUi& print) {
        // the sweep type is the combo position, which the model bounds
        const int type_index = std::clamp(noise.sweep_type_index, 0, static_cast<int>(NOISE_SWEEP_TYPES.size()) - 1);
        // the stored spelling is what the serializer writes
        const std::string sweep_type = NOISE_SWEEP_TYPES[static_cast<std::size_t>(type_index)];
        // the panel state builds the analysis
        return NoiseSimulationParameters(trim(noise.output_node), trim(noise.ref_node), trim(noise.source_name), trim(noise.start_freq), trim(noise.end_freq), trim(noise.num_points), sweep_type, parse_device_noise(noise.device_noise), trim(noise.data_table), build_print(print, false, false, false, "NOISE", {"NOISE"}));
    }

    // seed the harmonic balance panel from the configuration a run uses
    void seed_hb(const SimulationConfig& current, albor::HbUi& hb, albor::PrintUi& print) {
        // the ground flag is global to the run
        hb.replace_ground = current.replace_ground;
        // a harmonic balance analysis fills the frequency fields and the companion sections
        if (const auto* stored = std::get_if<HbSimulationParameters>(&current.analysis)) {
            // the frequency fields are edited as text
            hb.frequencies = join_with(stored->frequencies, " ");
            hb.harmonics = format_int_list(stored->harmonics);
            hb.tahb = stored->tahb ? std::to_string(*stored->tahb) : "";
            hb.selectharms = stored->selectharms.value_or("");
            hb.startup_periods = stored->startup_periods ? std::to_string(*stored->startup_periods) : "";
            // the solver options are edited as key=value lines
            hb.nonlin_options = format_key_values(stored->nonlin_options);
            hb.linsol_options = format_key_values(stored->linsol_options);
            // the print card mirrors the stored print
            seed_print(stored->print_parameters, false, false, false, {"HB", "HB_FD", "HB_TD"}, print);
            return;
        }
        // an analysis without an HB print starts with the card disabled
        seed_print(std::nullopt, false, false, false, {"HB", "HB_FD", "HB_TD"}, print);
    }

    // build the harmonic balance analysis from the panel state
    [[nodiscard]] HbSimulationParameters build_hb(const albor::HbUi& hb, const albor::PrintUi& print) {
        // the frequencies are space separated
        std::vector<std::string> frequencies = tokenize_owned(hb.frequencies);
        // the harmonics are comma or space separated integers
        std::vector<int> harmonics = parse_int_list(hb.harmonics);
        // the selectharms text is optional
        std::optional<std::string> selectharms;
        // a non blank text is what the analysis stores
        if (!trim(hb.selectharms).empty())
            selectharms = trim(hb.selectharms);
        // the panel state builds the analysis
        return HbSimulationParameters(std::move(frequencies), std::move(harmonics), parse_optional_int(hb.tahb), std::move(selectharms), parse_optional_int(hb.startup_periods), build_print(print, false, false, false, "HB", {"HB", "HB_FD", "HB_TD"}), parse_key_values(hb.nonlin_options), parse_key_values(hb.linsol_options));
    }

    // seed the linear analysis panel from the configuration a run uses
    void seed_lin(const SimulationConfig& current, albor::LinUi& lin, albor::PrintUi& print) {
        // the ground flag is global to the run
        lin.replace_ground = current.replace_ground;
        // a linear analysis fills the extraction fields and the companion sections
        if (const auto* stored = std::get_if<LinSimulationParameters>(&current.analysis)) {
            // the extraction fields
            lin.sparcalc = stored->sparcalc;
            lin.format_index = choice_index(LIN_FORMATS, stored->format);
            lin.lintype_index = choice_index(LIN_TYPES, stored->lintype);
            lin.dataformat_index = choice_index(LIN_DATA_FORMATS, stored->dataformat);
            lin.file = stored->file;
            lin.output_width = stored->width;
            lin.precision = stored->precision;
            // the embedded sweep fields
            lin.sweep_mode_index = choice_index(AC_MODES, stored->sweep_mode);
            lin.points = stored->points;
            lin.start = stored->start;
            lin.end = stored->end;
            lin.data_table = stored->data_table_name;
            // the print card mirrors the stored print
            seed_print(stored->print_parameters, false, false, false, {"AC"}, print);
            return;
        }
        // an analysis without a LIN print starts with the card disabled
        seed_print(std::nullopt, false, false, false, {"AC"}, print);
    }

    // build the linear analysis from the panel state
    [[nodiscard]] LinSimulationParameters build_lin(const albor::LinUi& lin, const albor::PrintUi& print) {
        // the extraction choices are the combo positions, which the models bound
        const int format_index = std::clamp(lin.format_index, 0, static_cast<int>(LIN_FORMATS.size()) - 1);
        const int type_index = std::clamp(lin.lintype_index, 0, static_cast<int>(LIN_TYPES.size()) - 1);
        const int dataformat_index = std::clamp(lin.dataformat_index, 0, static_cast<int>(LIN_DATA_FORMATS.size()) - 1);
        // the sweep mode is the combo position, which the model bounds
        const int mode_index = std::clamp(lin.sweep_mode_index, 0, static_cast<int>(AC_MODES.size()) - 1);
        // the stored spellings are what the serializer writes
        const std::string mode = AC_MODES[static_cast<std::size_t>(mode_index)];
        // the range fields the non DATA modes carry
        std::string points;
        std::string start;
        std::string end;
        // a DATA sweep names a table instead of a range
        std::string table;
        // the range fields only apply to the modes that sweep a range
        if (mode == "DATA") {
            // the table the DATA sweep reads is the only input
            table = trim(lin.data_table);
        }
        else {
            // the density of the sweep
            points = trim(lin.points);
            // the first frequency the sweep walks from
            start = trim(lin.start);
            // the last frequency the sweep walks to
            end = trim(lin.end);
        }
        // the panel state builds the analysis
        return LinSimulationParameters(lin.sparcalc, LIN_FORMATS[static_cast<std::size_t>(format_index)], LIN_TYPES[static_cast<std::size_t>(type_index)], LIN_DATA_FORMATS[static_cast<std::size_t>(dataformat_index)], trim(lin.file), trim(lin.output_width), trim(lin.precision), mode, std::move(points), std::move(start), std::move(end), std::move(table), build_print(print, false, false, false, "AC", {"AC"}));
    }

    // map the configured analysis to the tab that edits it, where none reads as operating point
    [[nodiscard]] int tab_for(const SimulationConfig& config) {
        // the operating point is the first tab
        if (std::holds_alternative<OpSimulationParameters>(config.analysis))
            return 0;
        // the DC sweep is the second tab
        if (std::holds_alternative<DCSimulationParameters>(config.analysis))
            return 1;
        // the transient analysis is the third tab
        if (std::holds_alternative<TransientSimulationParameters>(config.analysis))
            return 2;
        // the AC sweep is the fourth tab
        if (std::holds_alternative<AcSimulationParameters>(config.analysis))
            return 3;
        // the noise analysis is the fifth tab
        if (std::holds_alternative<NoiseSimulationParameters>(config.analysis))
            return 4;
        // the harmonic balance is the sixth tab
        if (std::holds_alternative<HbSimulationParameters>(config.analysis))
            return 5;
        // the linear analysis is the seventh tab
        if (std::holds_alternative<LinSimulationParameters>(config.analysis))
            return 6;
        // every other analysis or none opens the operating point tab
        return 0;
    }
    // find a node by its author id, walking the retained tree
    albor::UiNode* find_node_by_id(albor::UiNode& node, std::string_view explicit_id) {
        // the requested id is the node itself
        if (node.identity().explicit_id == explicit_id)
            return &node;
        // every child is searched, because the id belongs to exactly one node
        for (const auto& child : node.children()) {
            // a child subtree answer wins
            if (albor::UiNode* found = find_node_by_id(*child, explicit_id))
                return found;
        }
        return nullptr;
    }

    // focus the first editable field of the tab the dialog opened on
    void request_initial_focus(albor::Application& application, albor::WindowHandle handle, int tab) {
        // the stage owns the focus ring and the tree is where the field lives
        albor::WindowStage* stage = application.stage(handle);
        albor::UiNode* root = application.root(handle);
        // a missing stage or tree has nothing to focus
        if (stage == nullptr || root == nullptr)
            return;
        // the first and the fallback field depend on the tab and its mode
        const char* first = nullptr;
        const char* fallback = nullptr;
        // each tab names the field a reader would want to type into first
        switch (tab) {
        case 0:
            first = "simulation_op_nodeset";
            break;
        case 1:
            first = "simulation_dc_mode";
            fallback = "simulation_dc_table";
            break;
        case 2:
            first = "simulation_tran_initial_step";
            break;
        case 3:
            first = "simulation_ac_points";
            fallback = "simulation_ac_table";
            break;
        case 4:
            first = "simulation_noise_output_node";
            break;
        case 5:
            first = "simulation_hb_frequencies";
            break;
        default:
            first = "simulation_lin_points";
            fallback = "simulation_lin_table";
            break;
        }
        // the first visible field wins, and the fallback covers a DATA sweep
        if (albor::UiNode* node = find_node_by_id(*root, first)) {
            stage->focus().request_focus(node);
            return;
        }
        if (fallback != nullptr) {
            if (albor::UiNode* node = find_node_by_id(*root, fallback))
                stage->focus().request_focus(node);
        }
    }

    // measure the dialog free of a window, which reports the width its fixed width page needs
    [[nodiscard]] float measure_dialog_width(albor::SimulationParametersDialog& root) {
        albor::Size natural{};
        root.measure(albor::SizeConstraints::unconstrained(), natural);
        return natural.width;
    }

    // grow the scroll page so the dialog body fills the height the window leaves between its fixed rows
    void fit_page_height(albor::SimulationParametersDialog& root, float height) {
        // measuring the dialog reports the rows around the page, whatever height the page currently takes
        albor::Size natural{};
        root.measure(albor::SizeConstraints::unconstrained(), natural);
        // the header, the spacing, the error row and the footer are everything the page is not
        const float chrome = natural.height - root.body_height();
        // the page takes what the window leaves, which pins the footer to the bottom edge
        root.set_body_height(std::max(1.0f, height - chrome));
    }

} // namespace

studio::AlborSimulationParametersDialogView::AlborSimulationParametersDialogView(albor::Application& application, albor::WindowHandle parent) :
    m_app(&application), m_parent(parent) {}

studio::AlborSimulationParametersDialogView::~AlborSimulationParametersDialogView() = default;

void studio::AlborSimulationParametersDialogView::show(const SimulationConfig& current, SimulationParametersDialogEvents& events) {
    // a second show while the window is up is ignored
    if (is_open())
        return;
    // remember the configuration and the presenter the dialog works from
    m_seed = current;
    m_events = &events;
    // the new window starts with a clean close state
    m_closing = false;
    // describe a fixed size modal dialog owned by the shell
    albor::WindowOptions options;
    options.title = "Simulation Parameters";
    options.size = albor::Size{DIALOG_WIDTH, DIALOG_HEIGHT};
    options.role = albor::WindowRole::Dialog;
    options.modality = albor::Modality::WindowModal;
    options.parent = m_parent;
    options.resizable = false;
    options.visible = false;
    // build the compiled dialog tree in a fresh window
    m_handle = m_app->create_window(options, m_root);
    // a refused window leaves the presenter without a surface
    if (!m_handle.valid() || m_root == nullptr) {
        m_handle = {};
        m_root = nullptr;
        m_events = nullptr;
        events.on_dialog_closed();
        return;
    }
    // theme the dialog from the same system appearance as the shell
    m_theme = install_theme(*m_app, m_handle);
    // the tab the configured analysis lives on is the one the dialog opens
    m_root->set_selected_tab(tab_for(current));
    // every panel starts from its own record, so switching analyses never loses a value
    albor::TranUi tran;
    albor::PrintUi tran_print;
    seed_transient(current, tran, tran_print);
    m_root->set_tran(tran);
    m_root->set_tran_print(tran_print);
    // the DC record carries its own ground flag, sweep fields and companion sections
    albor::DcUi dc;
    albor::PrintUi dc_print;
    seed_dc(current, dc, dc_print);
    m_root->set_dc(dc);
    m_root->set_dc_print(dc_print);
    // the sweep table is a keyed row list the host view owns
    std::vector<albor::DcSweepRow> dc_sweeps;
    m_next_sweep_key = 1;
    if (const auto* stored_dc = std::get_if<DCSimulationParameters>(&current.analysis)) {
        // every stored sweep becomes one keyed row
        for (const DcSweep& sweep : stored_dc->sweeps) {
            albor::DcSweepRow row;
            row.key = std::to_string(m_next_sweep_key++);
            row.variable = sweep.variable;
            row.start = sweep.start;
            row.stop = sweep.stop;
            row.step = sweep.step;
            row.points = sweep.points;
            row.list_values = join_values(sweep.list_values);
            dc_sweeps.push_back(std::move(row));
        }
    }
    m_root->set_dc_sweeps(dc_sweeps);
    m_root->set_dc_sweeps_empty(dc_sweeps.empty());
    // the table actions mutate the keyed list the view owns
    m_root->set_on_add_sweep([this] {
        std::vector<albor::DcSweepRow> rows = m_root->dc_sweeps();
        albor::DcSweepRow row;
        row.key = std::to_string(m_next_sweep_key++);
        rows.push_back(std::move(row));
        m_root->set_dc_sweeps_empty(rows.empty());
        m_root->set_dc_sweeps(std::move(rows));
    });
    m_root->set_on_remove_sweep([this](std::string key) {
        std::vector<albor::DcSweepRow> rows = m_root->dc_sweeps();
        std::erase_if(rows, [&key](const albor::DcSweepRow& row) { return row.key == key; });
        m_root->set_dc_sweeps_empty(rows.empty());
        m_root->set_dc_sweeps(std::move(rows));
    });
    m_root->set_on_sweep_edited([this](std::string key, albor::DcSweepRow row) {
        std::vector<albor::DcSweepRow> rows = m_root->dc_sweeps();
        for (albor::DcSweepRow& current_row : rows) {
            if (current_row.key == key)
                current_row = row;
        }
        m_root->set_dc_sweeps(std::move(rows));
    });
    // the AC record carries its own ground flag, sweep fields and companion sections
    albor::AcUi ac;
    albor::PrintUi ac_print;
    seed_ac(current, ac, ac_print);
    m_root->set_ac(ac);
    m_root->set_ac_print(ac_print);
    // the operating point record carries the save, nodeset and print sections
    albor::OpUi op;
    albor::PrintUi op_print;
    seed_op(current, op, op_print);
    m_root->set_op(op);
    m_root->set_op_print(op_print);
    // the noise record carries the source, sweep and print sections
    albor::NoiseUi noise;
    albor::PrintUi noise_print;
    seed_noise(current, noise, noise_print);
    m_root->set_noise(noise);
    m_root->set_noise_print(noise_print);
    // the harmonic balance record carries the frequency, solver and print sections
    albor::HbUi hb;
    albor::PrintUi hb_print;
    seed_hb(current, hb, hb_print);
    m_root->set_hb(hb);
    m_root->set_hb_print(hb_print);
    // the linear analysis record carries the extraction, sweep and print sections
    albor::LinUi lin;
    albor::PrintUi lin_print;
    seed_lin(current, lin, lin_print);
    m_root->set_lin(lin);
    m_root->set_lin_print(lin_print);
    // the PCE section is shared by the transient and DC panels, so it seeds from either analysis
    albor::PceUi pce;
    albor::PrintUi pce_print;
    std::vector<albor::PceRow> pce_rows;
    m_next_pce_key = 1;
    const PceParameters* stored_pce = nullptr;
    if (const auto* transient = std::get_if<TransientSimulationParameters>(&current.analysis)) {
        if (transient->pce)
            stored_pce = &*transient->pce;
    }
    else if (const auto* dc = std::get_if<DCSimulationParameters>(&current.analysis)) {
        if (dc->pce)
            stored_pce = &*dc->pce;
    }
    if (stored_pce != nullptr) {
        seed_pce(*stored_pce, pce, pce_print);
        pce_rows = pce_rows_from_parameters(*stored_pce);
        m_next_pce_key = static_cast<int>(pce_rows.size()) + 1;
    }
    else {
        seed_pce(std::nullopt, pce, pce_print);
    }
    m_root->set_pce(pce);
    m_root->set_pce_rows(pce_rows);
    m_root->set_pce_rows_empty(pce_rows.empty());
    m_root->set_pce_print(pce_print);
    // the PCE table actions mutate the keyed list the view owns
    m_root->set_on_pce_add_row([this] {
        std::vector<albor::PceRow> rows = m_root->pce_rows();
        albor::PceRow row;
        row.key = std::to_string(m_next_pce_key++);
        row.type_index = 0;
        rows.push_back(std::move(row));
        m_root->set_pce_rows_empty(rows.empty());
        m_root->set_pce_rows(std::move(rows));
    });
    m_root->set_on_pce_remove_row([this](std::string key) {
        std::vector<albor::PceRow> rows = m_root->pce_rows();
        std::erase_if(rows, [&key](const albor::PceRow& row) { return row.key == key; });
        m_root->set_pce_rows_empty(rows.empty());
        m_root->set_pce_rows(std::move(rows));
    });
    m_root->set_on_pce_row_edited([this](std::string key, albor::PceRow row) {
        std::vector<albor::PceRow> rows = m_root->pce_rows();
        for (albor::PceRow& current_row : rows) {
            if (current_row.key == key)
                current_row = row;
        }
        m_root->set_pce_rows(std::move(rows));
    });
    // each managed package card starts from the options the netlist carries
    m_root->set_timeint_rows(build_option_rows(OptionPackage::TIMEINT, package_options(current.options, OptionPackage::TIMEINT)));
    m_root->set_nonlin_rows(build_option_rows(OptionPackage::NONLIN, package_options(current.options, OptionPackage::NONLIN)));
    m_root->set_nonlin_tran_rows(build_option_rows(OptionPackage::NONLIN_TRAN, package_options(current.options, OptionPackage::NONLIN_TRAN)));
    m_root->set_linsol_ac_rows(build_option_rows(OptionPackage::LINSOL_AC, package_options(current.options, OptionPackage::LINSOL_AC)));
    m_root->set_loca_rows(build_option_rows(OptionPackage::LOCA, package_options(current.options, OptionPackage::LOCA)));
    m_root->set_fft_rows(build_option_rows(OptionPackage::FFT, package_options(current.options, OptionPackage::FFT)));
    m_root->set_output_rows(build_option_rows(OptionPackage::OUTPUT, package_options(current.options, OptionPackage::OUTPUT)));
    // every option card edits the keyed rows of its package
    auto option_rows_for = [this](int package) -> std::vector<albor::OptionRow> {
        switch (package) {
        case 1:
            return m_root->timeint_rows();
        case 2:
            return m_root->nonlin_rows();
        case 3:
            return m_root->nonlin_tran_rows();
        case 5:
            return m_root->linsol_ac_rows();
        case 6:
            return m_root->loca_rows();
        case 11:
            return m_root->fft_rows();
        case 12:
            return m_root->output_rows();
        default:
            return {};
        }
    };
    auto set_option_rows = [this](int package, std::vector<albor::OptionRow> rows) {
        switch (package) {
        case 1:
            m_root->set_timeint_rows(std::move(rows));
            break;
        case 2:
            m_root->set_nonlin_rows(std::move(rows));
            break;
        case 3:
            m_root->set_nonlin_tran_rows(std::move(rows));
            break;
        case 5:
            m_root->set_linsol_ac_rows(std::move(rows));
            break;
        case 6:
            m_root->set_loca_rows(std::move(rows));
            break;
        case 11:
            m_root->set_fft_rows(std::move(rows));
            break;
        case 12:
            m_root->set_output_rows(std::move(rows));
            break;
        default:
            break;
        }
    };
    m_root->set_on_option_row_edited([option_rows_for, set_option_rows](int package, std::string key, albor::OptionRow row) {
        std::vector<albor::OptionRow> rows = option_rows_for(package);
        for (albor::OptionRow& current_row : rows) {
            if (current_row.key == key)
                current_row = row;
        }
        set_option_rows(package, std::move(rows));
    });
    // a picked combo entry writes the value the entry names, which the card maps back to its option
    m_root->set_on_option_choice_selected([option_rows_for, set_option_rows](int package, std::string key, int index) {
        std::vector<albor::OptionRow> rows = option_rows_for(package);
        for (albor::OptionRow& current_row : rows) {
            if (current_row.key != key)
                continue;
            select_option_choice(current_row, index);
        }
        set_option_rows(package, std::move(rows));
    });
    // clear any stale validation feedback
    m_root->set_error_message("");
    // the dialog hugs its page, so the fixed width body row leaves no empty band
    if (albor::Window* window = m_app->window(m_handle))
        window->set_size(albor::Size{measure_dialog_width(*m_root), DIALOG_HEIGHT});
    // the page fills the window, so the footer sits on the bottom edge
    fit_page_height(*m_root, DIALOG_HEIGHT);
    // forward the dialog actions to this view
    m_root->set_on_accepted([this] { accept(); });
    m_root->set_on_dismissed([this] { dismiss(); });
    // show the fully seeded window, which avoids a blank first frame
    if (albor::Window* window = m_app->window(m_handle))
        window->show();
    // the first editable field takes the caret once the first frame has arranged the page
    const int initial_tab = tab_for(current);
    m_app->post([this, initial_tab] {
        if (m_handle.valid())
            request_initial_focus(*m_app, m_handle, initial_tab);
    });
}

void studio::AlborSimulationParametersDialogView::close() {
    // a close with no open window is a no-op
    if (!m_handle.valid())
        return;
    // a second close waits for the first teardown
    if (m_closing)
        return;
    // record the close so the closed notification knows it was requested
    m_closing = true;
    // ask the application to close the window, which consults the veto and defers an input driven teardown
    (void)m_app->request_close(m_handle);
}

void studio::AlborSimulationParametersDialogView::show_error(std::string_view message) {
    // without a live facade there is nothing to update
    if (m_root == nullptr)
        return;
    // carry the message the validation produced
    m_root->set_error_message(std::string(message));
    // the message settles on the next frame, so the page gives up the height it takes then
    m_app->post([this] {
        if (m_handle.valid() && m_root != nullptr) {
            if (const albor::Window* window = m_app->window(m_handle))
                fit_page_height(*m_root, window->metrics().logical_size.height);
        }
    });
}

bool studio::AlborSimulationParametersDialogView::is_open() const { return m_handle.valid(); }

void studio::AlborSimulationParametersDialogView::on_window_closed(albor::WindowHandle handle) {
    // ignore windows this view does not own
    if (handle != m_handle)
        return;
    // take the presenter out before clearing, because the notification may reset it
    SimulationParametersDialogEvents* events = m_events;
    // the facade and the theme bindings die with the window
    m_handle = {};
    m_root = nullptr;
    m_events = nullptr;
    m_theme.reset();
    m_closing = false;
    // tell the presenter the surface is gone
    if (events != nullptr)
        events->on_dialog_closed();
}

SimulationConfig studio::AlborSimulationParametersDialogView::build_config() const {
    // start from the configuration the dialog opened with, so the untouched sections survive the edit
    SimulationConfig config = m_seed;
    // the selected tab names the analysis the fields describe
    const int tab = m_root != nullptr ? m_root->selected_tab() : 0;
    // the operating point replaces the analysis variant and keeps the companion sections
    if (tab == 0) {
        // the panel state builds the analysis
        config.analysis = build_op(m_root->op(), m_root->op_print());
        // the stored spelling is what the run reports
        config.analysis_type = "OP";
        // the tab owns the ground flag and the initial conditions
        config.replace_ground = m_root->op().replace_ground;
        config.ic_parameters = ICParameters::from_line(m_root->op().initial_conditions);
        // the NONLIN package is the option card the OP panel edits
        config.options.nonlin = apply_option_rows(m_root->nonlin_rows());
        return config;
    }
    // the DC sweep replaces the analysis variant and keeps the companion sections
    if (tab == 1) {
        // the panel state builds the analysis
        config.analysis = build_dc(m_root->dc(), m_root->dc_sweeps(), m_root->dc_print(), m_root->pce(), m_root->pce_rows(), m_root->pce_print(), m_seed);
        // the stored spelling is what the run reports
        config.analysis_type = "DC";
        // the tab owns the ground flag and the initial conditions
        config.replace_ground = m_root->dc().replace_ground;
        config.ic_parameters = ICParameters::from_line(m_root->dc().initial_conditions);
        // the NONLIN package is the option card the DC panel edits
        config.options.nonlin = apply_option_rows(m_root->nonlin_rows());
        return config;
    }
    // the transient analysis replaces the analysis variant and keeps the companion sections
    if (tab == 2) {
        // the panel state builds the analysis
        config.analysis = build_transient(m_root->tran(), m_root->tran_print(), m_root->pce(), m_root->pce_rows(), m_root->pce_print(), m_seed);
        // the stored spelling is what the run reports
        config.analysis_type = "TRAN";
        // the tab owns the ground flag and the initial conditions
        config.replace_ground = m_root->tran().replace_ground;
        config.ic_parameters = ICParameters::from_line(m_root->tran().initial_conditions);
        // the five transient packages are the cards the panel edits
        config.options.timeint = apply_option_rows(m_root->timeint_rows());
        config.options.nonlin_tran = apply_option_rows(m_root->nonlin_tran_rows());
        config.options.loca = apply_option_rows(m_root->loca_rows());
        config.options.fft = apply_option_rows(m_root->fft_rows());
        config.options.output = apply_option_rows(m_root->output_rows());
        return config;
    }
    // the AC sweep replaces the analysis variant and keeps the companion sections
    if (tab == 3) {
        // the panel state builds the analysis
        config.analysis = build_ac(m_root->ac(), m_root->ac_print(), m_seed);
        // the stored spelling is what the run reports
        config.analysis_type = "AC";
        // the tab owns the ground flag
        config.replace_ground = m_root->ac().replace_ground;
        // the LINSOL-AC package is the option card the AC panel edits
        config.options.linsol_ac = apply_option_rows(m_root->linsol_ac_rows());
        return config;
    }
    // the noise analysis replaces the analysis variant and keeps the companion sections
    if (tab == 4) {
        // the panel state builds the analysis
        config.analysis = build_noise(m_root->noise(), m_root->noise_print());
        // the stored spelling is what the run reports
        config.analysis_type = "NOISE";
        // the tab owns the ground flag
        config.replace_ground = m_root->noise().replace_ground;
        return config;
    }
    // the harmonic balance replaces the analysis variant and keeps the companion sections
    if (tab == 5) {
        // the panel state builds the analysis
        config.analysis = build_hb(m_root->hb(), m_root->hb_print());
        // the stored spelling is what the run reports
        config.analysis_type = "HB";
        // the tab owns the ground flag
        config.replace_ground = m_root->hb().replace_ground;
        return config;
    }
    // the linear analysis is the last tab
    config.analysis = build_lin(m_root->lin(), m_root->lin_print());
    // the stored spelling is what the run reports
    config.analysis_type = "LIN";
    // the tab owns the ground flag
    config.replace_ground = m_root->lin().replace_ground;
    return config;
}

void studio::AlborSimulationParametersDialogView::accept() {
    // an accept without a live dialog is stale
    if (m_root == nullptr || m_events == nullptr)
        return;
    // hand the configuration the fields describe to the presenter, which owns validation
    m_events->on_accept(build_config());
}

void studio::AlborSimulationParametersDialogView::dismiss() {
    // a dismiss without a presenter is stale
    if (m_events == nullptr)
        return;
    // forward the cancel request to the presenter
    m_events->on_cancel();
}

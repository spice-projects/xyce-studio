#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "../core/util.h"
#include "../netlist/netlist.h"
#include "print_parameters.h"

// init allowed generic print option keys
static const std::set<std::string> COMMON_OPTION_KEYS = {"FORMAT", "FILE", "WIDTH", "PRECISION", "FILTER", "DELIMITER", "TIMESCALEFACTOR", "HEADINGS"};

// init allowed sampling-specific print option keys
static const std::set<std::string> SAMPLE_OPTION_KEYS = {"OUTPUT_SAMPLE_STATS", "OUTPUT_ALL_SAMPLES"};

// init known print types with sample-specific options
static const std::set<std::string> SAMPLE_PRINT_TYPES = {"ES", "PCE", "SAMPLING", "TRANADJOINT"};

// init allowed format values from the reference guide
static const std::set<std::string> ALLOWED_FORMAT_VALUES = {"STD", "NOINDEX", "PROBE", "TECPLOT", "RAW", "CSV", "GNUPLOT", "SPLOT"};

// check whether a character is a valid identifier character (alpha, digit, or underscore)
static bool is_valid_key_char(char c, bool is_first) {
    // first char must be alpha or underscore
    if (is_first) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }
    // remaining chars can be alphanumeric or underscore
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// attempt to split a token into a key=value option pair;
// returns empty string pair when the token is not a valid option
static std::pair<std::string, std::string> split_option_token(const std::string& token) {
    // check for equals sign
    const auto eq_pos = token.find('=');
    if (eq_pos == std::string::npos) {
        // not an option token
        return {};
    }
    // extract key and value portions
    const std::string raw_key = token.substr(0, eq_pos);
    const std::string value = token.substr(eq_pos + 1);
    // strip leading/trailing whitespace from key
    const auto key_start = raw_key.find_first_not_of(" \t");
    if (key_start == std::string::npos) {
        // key is all whitespace — invalid
        return {};
    }
    const auto key_end = raw_key.find_last_not_of(" \t");
    const std::string stripped_key = raw_key.substr(key_start, key_end - key_start + 1);
    // validate key starts with alpha or underscore
    if (!is_valid_key_char(stripped_key[0], true)) {
        // invalid first character
        return {};
    }
    // validate remaining key chars
    for (size_t i = 1; i < stripped_key.size(); ++i) {
        if (!is_valid_key_char(stripped_key[i], false)) {
            // invalid character found
            return {};
        }
    }
    // return normalized key and raw value
    return {to_upper(stripped_key), value};
}

// check whether an option key is supported for the given print type
static bool is_supported_option(const std::string& print_type, const std::string& option_key) {
    // check common options
    if (COMMON_OPTION_KEYS.count(option_key)) {
        // supported
        return true;
    }
    // check sample-only options
    if (SAMPLE_OPTION_KEYS.count(option_key) && SAMPLE_PRINT_TYPES.count(print_type)) {
        // supported
        return true;
    }
    // unsupported
    return false;
}

std::vector<std::string> tokenize_print_statement(const std::string& print_statement) {
    // init token list
    std::vector<std::string> tokens;
    // init current token buffer
    std::string current;
    // init brace nesting depth
    int brace_depth = 0;
    // init quote state
    char quote_char = '\0';
    // iterate characters
    for (const char ch : print_statement) {
        // check active quote
        if (quote_char) {
            // append char
            current += ch;
            // check quote close
            if (ch == quote_char) {
                // reset quote state
                quote_char = '\0';
            }
            // next
            continue;
        }
        // check quote open
        if (ch == '"' || ch == '\'') {
            // append char
            current += ch;
            // set quote char
            quote_char = ch;
            // next
            continue;
        }
        // check opening brace
        if (ch == '{') {
            // append char
            current += ch;
            // increment depth
            ++brace_depth;
            // next
            continue;
        }
        // check closing brace
        if (ch == '}') {
            // append char
            current += ch;
            // decrement depth
            if (brace_depth > 0) {
                --brace_depth;
            }
            // next
            continue;
        }
        // check whitespace splitter
        if (std::isspace(static_cast<unsigned char>(ch)) && brace_depth == 0) {
            // check token has chars
            if (!current.empty()) {
                // append token
                tokens.push_back(current);
                // reset buffer
                current.clear();
            }
            // next
            continue;
        }
        // append regular char
        current += ch;
    }
    // check trailing token
    if (!current.empty()) {
        // append trailing token
        tokens.push_back(current);
    }
    // return tokens
    return tokens;
}

// join tokens with spaces
static std::string join_tokens(const std::vector<std::string>& tokens) {
    // init result
    std::string result;
    // loop tokens and join with spaces
    for (size_t i = 0; i < tokens.size(); ++i) {
        // add separator
        if (i > 0)
            result += ' ';
        // add token
        result += tokens[i];
    }
    return result;
}

PrintParameters::PrintParameters(std::string print_type, std::string print_format, std::string print_file, std::vector<std::string> output_variables, std::vector<std::string> extra_options) :
    print_type(std::move(print_type)), print_format(std::move(print_format)), print_file(std::move(print_file)), output_variables(sanitize_print_output_variables(this->print_type, output_variables)), extra_options(std::move(extra_options)) {}

std::vector<std::string> sanitize_print_output_variables(const std::string& print_type, const std::vector<std::string>& output_variables) {
    // wildcard tokens unsupported by the linear frequency-domain analyses per
    // the Xyce reference guide: power (P(*)/W(*)) and device lead currents
    // (IB/IC/IE/IS/ID/IG) are unsupported for AC, NOISE and HB prints, which
    // only carry voltages and branch currents (V, E, H, L devices, voltage
    // form B device)
    static const std::set<std::string> UNSUPPORTED_WILDCARDS = {"P(*)", "W(*)", "IB(*)", "IC(*)", "IE(*)", "IS(*)", "ID(*)", "IG(*)"};
    // print types restricted by the reference guide statements
    static const std::set<std::string> LINEAR_PRINT_TYPES = {"AC", "NOISE", "HB", "HB_FD", "HB_TD"};
    // any other print type keeps the variables unchanged
    if (UNSUPPORTED_WILDCARDS.empty() || LINEAR_PRINT_TYPES.count(to_upper(print_type)) == 0)
        return output_variables;
    // filtered variable list under construction
    std::vector<std::string> sanitized;
    // reserve space
    sanitized.reserve(output_variables.size());
    // keep the tokens the print type supports
    for (const auto& variable : output_variables) {
        if (UNSUPPORTED_WILDCARDS.count(variable) == 0)
            sanitized.push_back(variable);
    }
    // exit
    return sanitized;
}

std::optional<PrintParameters> PrintParameters::from_xyce_statement(const std::string& print_statement) {
    // parse tokens
    const auto tokens = tokenize_print_statement(print_statement);
    // reject non-print statements
    if (tokens.size() < 2 || to_upper(tokens[0]) != ".PRINT") {
        // return none
        return std::nullopt;
    }
    // parse print type
    const std::string print_type = to_upper(tokens[1]);
    // init format value
    std::string print_format;
    // init file value
    std::string print_file;
    // init extra options
    std::vector<std::string> extra_options;
    // init output variable list
    std::vector<std::string> output_variables;
    // init section flag
    bool in_output_variables = false;
    // iterate remaining tokens
    for (size_t i = 2; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        // parse option token when still in option section
        auto option_pair = (!in_output_variables) ? split_option_token(token) : std::pair<std::string, std::string>{};
        // check option token
        if (!option_pair.first.empty()) {
            // unpack option pair
            const auto& option_key = option_pair.first;
            const auto& option_value = option_pair.second;
            // validate option key against print type
            if (!is_supported_option(print_type, option_key)) {
                // skip unsupported option
                continue;
            }
            // map format option
            if (option_key == "FORMAT") {
                // normalize format candidate
                const std::string normalized_format = to_upper(option_value);
                // validate format value
                if (!ALLOWED_FORMAT_VALUES.count(normalized_format)) {
                    // skip invalid format
                    continue;
                }
                // store format (preserve original case from input)
                print_format = option_value;
                // next
                continue;
            }
            // map file option
            if (option_key == "FILE") {
                // store file (normalize a quoted value so the dialog and the
                // copy destination see the actual filename without quotes)
                print_file = strip_outer_quotes(option_value);
                // next
                continue;
            }
            // append generic option token
            extra_options.push_back(token);
            // next
            continue;
        }
        // mark output-variable section
        in_output_variables = true;
        // normalize W(...) to P(...) — W is the PSpice alias for P
        std::string var = token;
        if (to_upper(var).substr(0, 2) == "W(") {
            // replace W( prefix with P(
            var = "P(" + var.substr(2);
        }
        // append output variable
        output_variables.push_back(std::move(var));
    }
    // return model
    return PrintParameters(print_type, print_format, print_file, std::move(output_variables), std::move(extra_options));
}

// strip matching outer quote characters from an option value
std::string strip_outer_quotes(std::string value) {
    // check for a matching outer quote pair
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
        // strip the outer quotes
        return value.substr(1, value.size() - 2);
    }
    // return the value unchanged
    return value;
}

// format a FILE= option value: the model always carries the bare filename, so
// any outer quotes are stripped and the value is quoted again only when it
// contains whitespace (so the statement survives tokenization)
std::string format_print_file_value(const std::string& file) {
    // normalize the value: the model must not carry outer quotes
    const std::string value = strip_outer_quotes(file);
    // quote the value when it contains whitespace
    if (value.find_first_of(" \t") != std::string::npos) {
        // wrap the value in double quotes
        return "\"" + value + "\"";
    }
    // return the value unchanged
    return value;
}

std::string strip_print_file_option(const std::string& print_statement) {
    // parse tokens
    const auto tokens = tokenize_print_statement(print_statement);
    // reject non-print statements
    if (tokens.size() < 2 || to_upper(tokens[0]) != ".PRINT") {
        // return unchanged
        return print_statement;
    }
    // first pass: scan the option section (before the first output variable) for
    // the format option; only RAW and PROBE output (or an unspecified format) are redirected
    bool is_redirected = true;
    bool in_output_variables = false;
    for (size_t i = 2; i < tokens.size() && !in_output_variables; ++i) {
        // parse option token
        const auto option_pair = split_option_token(tokens[i]);
        // check for the format option
        if (option_pair.first == "FORMAT") {
            // only RAW and PROBE output are redirected to the netlist-derived file
            const std::string fmt = to_upper(option_pair.second);
            is_redirected = fmt == "RAW" || fmt == "PROBE";
        }
        // check for the start of the output-variable section
        else if (option_pair.first.empty()) {
            // mark output-variable section
            in_output_variables = true;
        }
    }
    // return non-redirected statements unchanged
    if (!is_redirected)
        return print_statement;
    // second pass: rebuild the statement without the file option
    std::vector<std::string> result;
    result.reserve(tokens.size());
    bool removed_file = false;
    in_output_variables = false;
    // the option section starts after the statement name and print type tokens
    result.push_back(tokens[0]);
    result.push_back(tokens[1]);
    for (size_t i = 2; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        // parse option token when still in option section
        const auto option_pair = (!in_output_variables) ? split_option_token(token) : std::pair<std::string, std::string>{};
        // check option token
        if (!option_pair.first.empty()) {
            // drop the file option
            if (option_pair.first == "FILE") {
                removed_file = true;
                continue;
            }
            // append the option token unchanged
            result.push_back(token);
            continue;
        }
        // mark output-variable section
        in_output_variables = true;
        // append token
        result.push_back(token);
    }
    // return the statement unchanged when no file option was removed
    if (!removed_file)
        return print_statement;
    // rebuild the statement from the remaining tokens
    return join_tokens(result);
}

std::string PrintParameters::to_xyce_statement() const {
    // init token list
    std::vector<std::string> tokens = {".PRINT", print_type};
    // append format option
    if (!print_format.empty())
        tokens.push_back("FORMAT=" + print_format);
    // append file option (quoted when the filename contains whitespace)
    if (!print_file.empty())
        tokens.push_back("FILE=" + format_print_file_value(print_file));
    // append extra options
    for (const auto& opt : extra_options)
        tokens.push_back(opt);
    // append output variables
    for (const auto& var : output_variables)
        tokens.push_back(var);
    // build joined statement
    return join_tokens(tokens);
}

std::string PrintParameters::to_xyce_statement(const NetlistTopology* topology) const {
    // topology-based wildcard expansion is removed; V(*)/I(*)/P(*) pass
    // through verbatim and are expanded natively by Xyce
    (void)topology;
    return to_xyce_statement();
}

bool PrintParameters::operator==(const PrintParameters& other) const {
    // compare all fields
    return print_type == other.print_type && print_format == other.print_format && print_file == other.print_file && output_variables == other.output_variables && extra_options == other.extra_options;
}

std::string prn_output_suffix(const std::string& print_type) {
    // normalize the print type to uppercase
    std::string u = print_type;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
    // frequency-domain print types (AC, HB, LIN)
    if (u == "AC" || u == "HB" || u == "HB_FD" || u == "LIN")
        return ".FD.prn";
    // time-domain subtypes (AC_IC, HB_TD, HB_IC, HB_STARTUP)
    if (u == "AC_IC" || u == "HB_TD" || u == "HB_IC" || u == "HB_STARTUP")
        return ".TD.prn";
    // specialized types with their own suffix
    if (u == "ES")
        return ".ES.prn";
    if (u == "SENS")
        return ".SENS.prn";
    if (u == "TRANADJOINT")
        return ".TRADJ.prn";
    if (u == "PCE")
        return ".PCE.prn";
    // default suffix for all other types (DC, TRAN, NOISE, HOMOTOPY, ...)
    return ".prn";
}

#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "core/util.h"
#include "measure_parameters.h"

namespace
{
    // analisis types allowed for measure directives
    const std::string ALLOWED_ANALYSIS_TYPES[8] = {"TRAN", "AC", "DC", "NOISE", "TRAN_CONT", "AC_CONT", "DC_CONT", "NOISE_CONT"};

    // measure type aliases for normalization
    const std::pair<std::string, std::string> MEASURE_TYPE_ALIASES[3] = {{"DERIVATIVE", "DERIV"}, {"INTEGRAL", "INTEG"}, {"PARAM", "EQN"}};

    // allowed measure types for validation
    const std::string ALLOWED_MEASURE_TYPES[] = {"AVG", "DERIV", "DUTY", "EQN", "ERR", "ERR1", "ERR2", "ERROR", "FIND", "FOUR", "FREQ", "INTEG", "MAX", "MIN", "OFF_TIME", "ON_TIME", "PP", "RMS", "WHEN", "ENOB", "SFDR", "SNDR", "SNR", "THD"};
} // namespace

// tokenize a measure statement by whitespace, respecting braces
static std::vector<std::string> measure_tokenize(const std::string& statement) {
    // init token list
    std::vector<std::string> tokens;
    // init current token buffer
    std::string current;
    // init brace nesting depth
    int brace_depth = 0;
    // iterate characters
    for (const char ch : statement) {
        // check opening brace
        if (ch == '{') {
            // append char
            current += ch;
            // increment depth
            brace_depth++;
            // next
            continue;
        }
        // check closing brace
        if (ch == '}') {
            // append char
            current += ch;
            // decrement depth
            brace_depth = std::max(0, brace_depth - 1);
            // next
            continue;
        }
        // check whitespace splitter
        if (std::isspace(static_cast<unsigned char>(ch)) && brace_depth == 0) {
            // check token has chars
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            // next
            continue;
        }
        // append regular char
        current += ch;
    }
    // check trailing token
    if (!current.empty())
        tokens.push_back(current);
    // return tokens
    return tokens;
}

// parse TRIG-TARG clause pairs starting at the TRIG keyword; fills the trig_*/targ_*
// fields of the entry and returns the index past the parsed clauses
static size_t parse_trig_targ_clauses(const std::vector<std::string>& tokens, size_t idx, MeasureEntry& entry) {
    // expect the TRIG keyword
    if (to_upper(tokens[idx]) != "TRIG") {
        return idx;
    }
    // parse a single clause into the given entry
    auto parse_clause = [&tokens](size_t start_idx, MeasureEntry& clause_entry) -> size_t {
        // init index
        size_t c_idx = start_idx;
        // check for AT form (AT=<value>)
        if (c_idx < tokens.size() && to_upper(tokens[c_idx]).substr(0, 3) == "AT=") {
            // set at value
            const auto& token = tokens[c_idx];
            const size_t eq_pos = token.find('=');
            // check for equals sign
            if (eq_pos != std::string::npos)
                clause_entry.trig_at_val = token.substr(eq_pos + 1);
            // advance index
            return c_idx + 1;
        }
        // check for variable form
        if (c_idx < tokens.size()) {
            // split variable and condition at a comparison operator
            const auto& token = tokens[c_idx];
            const size_t op_pos = token.find_first_of("=<>");
            if (op_pos != std::string::npos) {
                clause_entry.trig_variable = token.substr(0, op_pos);
                clause_entry.trig_condition = token.substr(op_pos);
            }
            else {
                clause_entry.trig_variable = token;
            }
            // advance index
            c_idx++;
            // parse qualifiers
            while (c_idx < tokens.size()) {
                // check for qualifier with equals sign
                const auto& qual_token = tokens[c_idx];
                const size_t qual_eq_pos = qual_token.find('=');
                // stop on tokens without an equals sign (e.g. TARG)
                if (qual_eq_pos == std::string::npos) {
                    break;
                }
                // split qualifier key and value
                const std::string qual_key = to_upper(qual_token.substr(0, qual_eq_pos));
                const std::string qual_val = qual_token.substr(qual_eq_pos + 1);
                // map known qualifiers
                if (qual_key == "VAL") {
                    clause_entry.trig_val = qual_val;
                }
                else if (qual_key == "TD") {
                    clause_entry.trig_td = qual_val;
                }
                else if (qual_key == "RISE") {
                    clause_entry.trig_rise = qual_val;
                }
                else if (qual_key == "FALL") {
                    clause_entry.trig_fall = qual_val;
                }
                else if (qual_key == "CROSS") {
                    clause_entry.trig_cross = qual_val;
                }
                else if (qual_key == "FRAC_MAX") {
                    clause_entry.trig_frac_max = qual_val;
                }
                else {
                    // stop on unknown qualifiers
                    break;
                }
                c_idx++;
            }
        }
        return c_idx;
    };

    // parse the TRIG clause into a temporary entry
    idx++;
    MeasureEntry trig_fields;
    idx = parse_clause(idx, trig_fields);
    entry.trig_variable = trig_fields.trig_variable;
    entry.trig_condition = trig_fields.trig_condition;
    entry.trig_val = trig_fields.trig_val;
    entry.trig_frac_max = trig_fields.trig_frac_max;
    entry.trig_td = trig_fields.trig_td;
    entry.trig_rise = trig_fields.trig_rise;
    entry.trig_fall = trig_fields.trig_fall;
    entry.trig_cross = trig_fields.trig_cross;
    entry.trig_at_val = trig_fields.trig_at_val;

    // parse the TARG clause if present
    if (idx < tokens.size() && to_upper(tokens[idx]) == "TARG") {
        idx++;
        MeasureEntry targ_fields;
        idx = parse_clause(idx, targ_fields);
        entry.targ_variable = targ_fields.trig_variable;
        entry.targ_condition = targ_fields.trig_condition;
        entry.targ_val = targ_fields.trig_val;
        entry.targ_frac_max = targ_fields.trig_frac_max;
        entry.targ_td = targ_fields.trig_td;
        entry.targ_rise = targ_fields.trig_rise;
        entry.targ_fall = targ_fields.trig_fall;
        entry.targ_cross = targ_fields.trig_cross;
        entry.targ_at_val = targ_fields.trig_at_val;
    }
    return idx;
}

// construct a MeasureEntry with default empty fields
MeasureEntry::MeasureEntry(std::string analysis_type, std::string result_name, std::string measure_type, std::string variable, std::string from_val, std::string to_val, std::string td_val, std::string rise_val, std::string fall_val, std::string cross_val, std::string minval, std::string default_val, std::string precision, std::string print_val, std::string at_val, std::string on_val, std::string off_val, std::string rfc_level, std::string output, std::string min_thresh, std::string max_thresh, std::string frac_max, std::string when_variable, std::string when_condition, std::string variable2, std::string trig_variable, std::string trig_condition, std::string trig_val, std::string trig_frac_max, std::string trig_td, std::string trig_rise, std::string trig_fall, std::string trig_cross, std::string trig_at_val, std::string targ_variable, std::string targ_condition, std::string targ_val, std::string targ_frac_max, std::string targ_td, std::string targ_rise, std::string targ_fall, std::string targ_cross, std::string targ_at_val, std::string error_file, std::string indepvarcol, std::string depvarcol, std::string comp_function, std::string numfreq, std::string gridsize, std::string binsiz, std::string maxfreq, std::string minfreq, std::string nbharm, std::string goal, std::string weight) :
    analysis_type(std::move(analysis_type)), result_name(std::move(result_name)), measure_type(std::move(measure_type)), variable(std::move(variable)), from_val(std::move(from_val)), to_val(std::move(to_val)), td_val(std::move(td_val)), rise_val(std::move(rise_val)), fall_val(std::move(fall_val)), cross_val(std::move(cross_val)), minval(std::move(minval)), default_val(std::move(default_val)), precision(std::move(precision)), print_val(std::move(print_val)), at_val(std::move(at_val)), on_val(std::move(on_val)), off_val(std::move(off_val)), rfc_level(std::move(rfc_level)), output(std::move(output)), min_thresh(std::move(min_thresh)), max_thresh(std::move(max_thresh)), frac_max(std::move(frac_max)), when_variable(std::move(when_variable)), when_condition(std::move(when_condition)), variable2(std::move(variable2)), trig_variable(std::move(trig_variable)), trig_condition(std::move(trig_condition)), trig_val(std::move(trig_val)), trig_frac_max(std::move(trig_frac_max)), trig_td(std::move(trig_td)), trig_rise(std::move(trig_rise)), trig_fall(std::move(trig_fall)), trig_cross(std::move(trig_cross)), trig_at_val(std::move(trig_at_val)), targ_variable(std::move(targ_variable)), targ_condition(std::move(targ_condition)), targ_val(std::move(targ_val)), targ_frac_max(std::move(targ_frac_max)), targ_td(std::move(targ_td)), targ_rise(std::move(targ_rise)), targ_fall(std::move(targ_fall)), targ_cross(std::move(targ_cross)), targ_at_val(std::move(targ_at_val)), error_file(std::move(error_file)), indepvarcol(std::move(indepvarcol)), depvarcol(std::move(depvarcol)), comp_function(std::move(comp_function)), numfreq(std::move(numfreq)), gridsize(std::move(gridsize)), binsiz(std::move(binsiz)), maxfreq(std::move(maxfreq)), minfreq(std::move(minfreq)), nbharm(std::move(nbharm)), goal(std::move(goal)), weight(std::move(weight)) {}

std::optional<MeasureEntry> MeasureEntry::from_xyce_statement(const std::string& statement) {
    // parse tokens
    const auto tokens = measure_tokenize(statement);
    // reject statements that are too short
    if (tokens.size() < 4) {
        // return none
        return std::nullopt;
    }
    // normalize command
    const std::string cmd = to_upper(tokens[0]);
    // check for .MEASURE or .MEAS
    if (cmd != ".MEASURE" && cmd != ".MEAS") {
        // return none
        return std::nullopt;
    }
    // parse analysis type and result name (preserving the original case)
    const std::string analysis_type = tokens[1];
    const std::string result_name = tokens[2];
    // flag indicating whether the analysis type is valid
    bool analysis_type_valid = false;
    // loop allowd analysis types
    for (const auto& allowed : ALLOWED_ANALYSIS_TYPES) {
        // check for match
        if (to_upper(analysis_type) == allowed) {
            // set flag and break
            analysis_type_valid = true;
            break;
        }
    }
    // reject invalid analysis types
    if (!analysis_type_valid) {
        // return none
        return std::nullopt;
    }
    // check for FFT keyword
    size_t idx = 3;
    if (to_upper(tokens[idx]) == "FFT") {
        // skip FFT token
        idx++;
        // check for end of tokens
        if (idx >= tokens.size())
            return std::nullopt;
    }
    // determine measure type
    std::string measure_type_raw = to_upper(tokens[idx]);
    idx++;
    // resolve aliases
    std::string measure_type_upper = measure_type_raw;
    // loop aliases
    for (const auto& [alias, normalized] : MEASURE_TYPE_ALIASES) {
        // check for alias match
        if (measure_type_upper == alias) {
            measure_type_upper = normalized;
            break;
        }
    }
    // check for TRIG keyword (special case)
    std::string measure_type;
    if (measure_type_upper == "TRIG")
        measure_type = "TRIG";
    // check for other keywords
    else {
        // flag indicating whether the measure type is valid
        bool found = false;
        // loop allowed measure types
        for (const auto& allowed : ALLOWED_MEASURE_TYPES) {
            // check for match
            if (measure_type_upper == allowed) {
                measure_type = measure_type_upper;
                found = true;
                break;
            }
        }
        // reject invalid measure types
        if (!found)
            return std::nullopt;
    }
    // init fields
    MeasureEntry entry;
    entry.analysis_type = analysis_type;
    entry.result_name = result_name;
    entry.measure_type = measure_type;
    // handle TRIG-TARG syntax
    if (measure_type == "TRIG") {
        // parse the TRIG-TARG clauses
        idx = parse_trig_targ_clauses(tokens, idx - 1, entry);

        // Standard qualifiers can follow TRIG-TARG
        for (size_t i = idx; i < tokens.size(); i++) {
            const auto& token = tokens[i];
            const size_t eq_pos = token.find('=');
            if (eq_pos != std::string::npos) {
                const std::string key = to_upper(token.substr(0, eq_pos));
                const std::string val = token.substr(eq_pos + 1);

                if (key == "MINVAL") {
                    entry.minval = val;
                }
                else if (key == "DEFAULT") {
                    entry.default_val = val;
                }
                else if (key == "PRECISION") {
                    entry.precision = val;
                }
                else if (key == "PRINT") {
                    entry.print_val = val;
                }
                else if (key == "FRAC_MAX") {
                    entry.frac_max = val;
                }
                else if (key == "FROM") {
                    entry.from_val = val;
                }
                else if (key == "TO") {
                    entry.to_val = val;
                }
            }
        }

        // return model
        return entry;
    }

    // handle standard measurement types
    else {
        // check if variable is present
        if (idx < tokens.size()) {
            // check for WHEN keyword (WHEN measure type has no variable)
            if (measure_type == "WHEN") {
                // set variable to empty for WHEN measure type
                entry.variable = "";
                // parse WHEN condition
                const auto& when_token = tokens[idx];
                idx++;

                // split variable and condition at a comparison operator
                const size_t op_pos = when_token.find_first_of("=<>");
                if (op_pos != std::string::npos) {
                    entry.when_variable = when_token.substr(0, op_pos);
                    entry.when_condition = when_token.substr(op_pos);
                }
                else {
                    entry.when_variable = when_token;
                }
            }
            // handle normal variable
            else {
                // set variable
                entry.variable = tokens[idx];
                idx++;
            }
        }

        // handle ERR/ERR1/ERR2 two-variable syntax
        if (measure_type == "ERR" || measure_type == "ERR1" || measure_type == "ERR2") {
            // check for second variable
            if (idx < tokens.size()) {
                // check the second token is not a keyword
                const std::string next = to_upper(tokens[idx]);
                if (next != "WHEN" && next != "TRIG" && next != "TARG") {
                    // set second variable
                    entry.variable2 = tokens[idx];
                    idx++;
                }
            }
        }

        // check that a required variable is present
        if (measure_type != "WHEN" && measure_type != "EQN" && entry.variable.empty()) {
            // return none
            return std::nullopt;
        }

        // iterate remaining tokens
        for (size_t i = idx; i < tokens.size(); i++) {
            const auto& token = tokens[i];

            // check for WHEN keyword
            if (to_upper(token) == "WHEN" && i + 1 < tokens.size()) {
                // set when variable
                entry.when_variable = tokens[i + 1];
                i++;

                // split variable and condition at a comparison operator
                const size_t op_pos = entry.when_variable.find_first_of("=<>");
                if (op_pos != std::string::npos) {
                    entry.when_condition = entry.when_variable.substr(op_pos);
                    entry.when_variable = entry.when_variable.substr(0, op_pos);
                }
                // no condition
                else {
                    entry.when_condition = "";
                }

                // check for a trailing second variable (WHEN var=var2 form)
                if (i + 1 < tokens.size() && tokens[i + 1].find('(') != std::string::npos) {
                    entry.variable2 = tokens[i + 1];
                    i++;
                }
                // continue to next token
                continue;
            }

            // check for TRIG keyword (TRIG-TARG windowing on other measures)
            if (to_upper(token) == "TRIG") {
                // parse the TRIG-TARG clauses
                i = parse_trig_targ_clauses(tokens, i, entry);
                // continue to next token
                continue;
            }

            // check for equals sign
            const size_t eq_pos = token.find('=');
            if (eq_pos != std::string::npos) {
                // split key and value
                const std::string key = to_upper(token.substr(0, eq_pos));
                const std::string val = token.substr(eq_pos + 1);

                // map FROM
                if (key == "FROM") {
                    entry.from_val = val;
                }
                // map TO
                else if (key == "TO") {
                    entry.to_val = val;
                }
                // map TD
                else if (key == "TD") {
                    entry.td_val = val;
                }
                // map RISE
                else if (key == "RISE") {
                    entry.rise_val = val;
                }
                // map FALL
                else if (key == "FALL") {
                    entry.fall_val = val;
                }
                // map CROSS
                else if (key == "CROSS") {
                    entry.cross_val = val;
                }
                // map MINVAL
                else if (key == "MINVAL") {
                    entry.minval = val;
                }
                // map DEFAULT
                else if (key == "DEFAULT") {
                    entry.default_val = val;
                }
                // map PRECISION
                else if (key == "PRECISION") {
                    entry.precision = val;
                }
                // map PRINT
                else if (key == "PRINT") {
                    entry.print_val = val;
                }
                // map AT
                else if (key == "AT") {
                    entry.at_val = val;
                }
                // map ON
                else if (key == "ON") {
                    entry.on_val = val;
                }
                // map OFF
                else if (key == "OFF") {
                    entry.off_val = val;
                }
                // map RFC_LEVEL
                else if (key == "RFC_LEVEL") {
                    entry.rfc_level = val;
                }
                // map OUTPUT
                else if (key == "OUTPUT") {
                    entry.output = val;
                }
                // map MIN_THRESH
                else if (key == "MIN_THRESH") {
                    entry.min_thresh = val;
                }
                // map MAX_THRESH
                else if (key == "MAX_THRESH") {
                    entry.max_thresh = val;
                }
                // map FILE (ERROR-specific)
                else if (key == "FILE") {
                    entry.error_file = val;
                }
                // map ERROR_FILE (ERROR-specific)
                else if (key == "ERROR_FILE") {
                    entry.error_file = val;
                }
                // map INDEPVARCOL (ERROR-specific)
                else if (key == "INDEPVARCOL") {
                    entry.indepvarcol = val;
                }
                // map DEPVARCOL (ERROR-specific)
                else if (key == "DEPVARCOL") {
                    entry.depvarcol = val;
                }
                // map COMP_FUNCTION (ERROR-specific)
                else if (key == "COMP_FUNCTION") {
                    entry.comp_function = val;
                }
                // map NUMFREQ (FOUR-specific)
                else if (key == "NUMFREQ") {
                    entry.numfreq = val;
                }
                // map GRIDSIZE (FOUR-specific)
                else if (key == "GRIDSIZE") {
                    entry.gridsize = val;
                }
                // map BINSIZ (FFT-specific)
                else if (key == "BINSIZ") {
                    entry.binsiz = val;
                }
                // map MAXFREQ (FFT-specific)
                else if (key == "MAXFREQ") {
                    entry.maxfreq = val;
                }
                // map MINFREQ (FFT-specific)
                else if (key == "MINFREQ") {
                    entry.minfreq = val;
                }
                // map NBHARM (FFT-specific)
                else if (key == "NBHARM") {
                    entry.nbharm = val;
                }
                // map HARM (FOUR-specific)
                else if (key == "HARM") {
                    entry.nbharm = val;
                }
                // map GOAL (HSPICE compatibility)
                else if (key == "GOAL") {
                    entry.goal = val;
                }
                // map WEIGHT (HSPICE compatibility)
                else if (key == "WEIGHT") {
                    entry.weight = val;
                }
                // map FRAC_MAX
                else if (key == "FRAC_MAX") {
                    entry.frac_max = val;
                }
            }
        }
    }

    // return model
    return entry;
}

std::string MeasureEntry::to_xyce_statement() const {
    // init tokens
    std::vector<std::string> tokens;
    tokens.push_back(".MEASURE");
    tokens.push_back(analysis_type);
    tokens.push_back(result_name);

    // handle TRIG-TARG syntax
    if (measure_type == "TRIG") {
        // append TRIG keyword
        tokens.push_back("TRIG");

        // check for AT form
        if (!trig_at_val.empty()) {
            // append AT clause with equals
            tokens.push_back("AT=" + trig_at_val);
        }
        // check for variable form
        else if (!trig_variable.empty()) {
            // check for condition
            if (!trig_condition.empty()) {
                // append combined variable and condition
                tokens.push_back(trig_variable + trig_condition);
            }
            else if (!trig_val.empty()) {
                // handle compatibility VAL=
                tokens.push_back(trig_variable + " VAL=" + trig_val);
            }
            else {
                // append trig variable
                tokens.push_back(trig_variable);
            }
            // append trig qualifiers
            if (!trig_td.empty()) {
                tokens.push_back("TD=" + trig_td);
            }
            if (!trig_rise.empty()) {
                tokens.push_back("RISE=" + trig_rise);
            }
            if (!trig_fall.empty()) {
                tokens.push_back("FALL=" + trig_fall);
            }
            if (!trig_cross.empty()) {
                tokens.push_back("CROSS=" + trig_cross);
            }
            if (!trig_frac_max.empty()) {
                tokens.push_back("FRAC_MAX=" + trig_frac_max);
            }
        }

        // append TARG keyword
        tokens.push_back("TARG");

        // check for AT form
        if (!targ_at_val.empty()) {
            // append AT clause with equals
            tokens.push_back("AT=" + targ_at_val);
        }
        // check for variable form
        else if (!targ_variable.empty()) {
            // check for condition
            if (!targ_condition.empty()) {
                // append combined variable and condition
                tokens.push_back(targ_variable + targ_condition);
            }
            else if (!targ_val.empty()) {
                // handle compatibility VAL=
                tokens.push_back(targ_variable + " VAL=" + targ_val);
            }
            else {
                // append targ variable
                tokens.push_back(targ_variable);
            }
            // append targ qualifiers
            if (!targ_td.empty()) {
                tokens.push_back("TD=" + targ_td);
            }
            if (!targ_rise.empty()) {
                tokens.push_back("RISE=" + targ_rise);
            }
            if (!targ_fall.empty()) {
                tokens.push_back("FALL=" + targ_fall);
            }
            if (!targ_cross.empty()) {
                tokens.push_back("CROSS=" + targ_cross);
            }
            if (!targ_frac_max.empty()) {
                tokens.push_back("FRAC_MAX=" + targ_frac_max);
            }
        }

        // Standard qualifiers can follow TRIG-TARG
        if (!minval.empty()) {
            tokens.push_back("MINVAL=" + minval);
        }
        if (!default_val.empty()) {
            tokens.push_back("DEFAULT=" + default_val);
        }
        if (!precision.empty()) {
            tokens.push_back("PRECISION=" + precision);
        }
        if (!frac_max.empty()) {
            tokens.push_back("FRAC_MAX=" + frac_max);
        }
        if (!print_val.empty()) {
            tokens.push_back("PRINT=" + print_val);
        }
    }
    // handle standard measurement types
    else {
        // check for FFT measure type
        if (measure_type == "ENOB" || measure_type == "SFDR" || measure_type == "SNDR" || measure_type == "SNR" || measure_type == "THD") {
            tokens.push_back("FFT");
        }
        // append measure type
        tokens.push_back(measure_type);
        // append variable
        if (!variable.empty()) {
            tokens.push_back(variable);
        }
        // append second variable for ERR1/ERR2
        if (!variable2.empty()) {
            tokens.push_back(variable2);
        }
        // append WHEN clause (except for WHEN measure type)
        if (!when_variable.empty() && measure_type != "WHEN") {
            // append WHEN keyword
            tokens.push_back("WHEN");
            // check for condition
            if (!when_condition.empty()) {
                // append combined variable and condition
                tokens.push_back(when_variable + when_condition);
            }
            else {
                // append when variable
                tokens.push_back(when_variable);
            }
        }
        // handle WHEN measure type specifically
        else if (measure_type == "WHEN" && !when_variable.empty()) {
            // append when variable and condition combined
            if (!when_condition.empty()) {
                tokens.push_back(when_variable + when_condition);
            }
            else {
                tokens.push_back(when_variable);
            }
        }
        // append common qualifiers
        if (!from_val.empty()) {
            tokens.push_back("FROM=" + from_val);
        }
        if (!to_val.empty()) {
            tokens.push_back("TO=" + to_val);
        }
        if (!td_val.empty()) {
            tokens.push_back("TD=" + td_val);
        }
        if (!rise_val.empty()) {
            tokens.push_back("RISE=" + rise_val);
        }
        if (!fall_val.empty()) {
            tokens.push_back("FALL=" + fall_val);
        }
        if (!cross_val.empty()) {
            tokens.push_back("CROSS=" + cross_val);
        }
        if (!minval.empty()) {
            tokens.push_back("MINVAL=" + minval);
        }
        if (!default_val.empty()) {
            tokens.push_back("DEFAULT=" + default_val);
        }
        if (!precision.empty()) {
            tokens.push_back("PRECISION=" + precision);
        }
        if (!frac_max.empty()) {
            tokens.push_back("FRAC_MAX=" + frac_max);
        }
        if (!print_val.empty()) {
            tokens.push_back("PRINT=" + print_val);
        }
        // type-specific qualifiers
        if (!at_val.empty()) {
            tokens.push_back("AT=" + at_val);
        }
        if (!on_val.empty()) {
            tokens.push_back("ON=" + on_val);
        }
        if (!off_val.empty()) {
            tokens.push_back("OFF=" + off_val);
        }
        if (!rfc_level.empty()) {
            tokens.push_back("RFC_LEVEL=" + rfc_level);
        }
        if (!output.empty()) {
            tokens.push_back("OUTPUT=" + output);
        }
        if (!min_thresh.empty()) {
            tokens.push_back("MIN_THRESH=" + min_thresh);
        }
        if (!max_thresh.empty()) {
            tokens.push_back("MAX_THRESH=" + max_thresh);
        }
        // ERROR-specific qualifiers
        if (!error_file.empty()) {
            tokens.push_back("FILE=" + error_file);
        }
        if (!indepvarcol.empty()) {
            tokens.push_back("INDEPVARCOL=" + indepvarcol);
        }
        if (!depvarcol.empty()) {
            tokens.push_back("DEPVARCOL=" + depvarcol);
        }
        if (!comp_function.empty()) {
            tokens.push_back("COMP_FUNCTION=" + comp_function);
        }
        // FOUR-specific qualifiers
        if (!numfreq.empty()) {
            tokens.push_back("NUMFREQ=" + numfreq);
        }
        if (!gridsize.empty()) {
            tokens.push_back("GRIDSIZE=" + gridsize);
        }
        // FFT-specific qualifiers
        if (!binsiz.empty()) {
            tokens.push_back("BINSIZ=" + binsiz);
        }
        if (!maxfreq.empty()) {
            tokens.push_back("MAXFREQ=" + maxfreq);
        }
        if (!minfreq.empty()) {
            tokens.push_back("MINFREQ=" + minfreq);
        }
        if (!nbharm.empty()) {
            tokens.push_back("NBHARM=" + nbharm);
        }
        // Compatibility
        if (!goal.empty()) {
            tokens.push_back("GOAL=" + goal);
        }
        if (!weight.empty()) {
            tokens.push_back("WEIGHT=" + weight);
        }
    }

    // return joined statement
    std::string result;
    for (size_t i = 0; i < tokens.size(); i++) {
        if (i > 0) {
            result += " ";
        }
        result += tokens[i];
    }
    return result;
}

bool MeasureEntry::operator==(const MeasureEntry& other) const {
    // compare all fields for equality
    return analysis_type == other.analysis_type && result_name == other.result_name && measure_type == other.measure_type && variable == other.variable && from_val == other.from_val && to_val == other.to_val && td_val == other.td_val && rise_val == other.rise_val && fall_val == other.fall_val && cross_val == other.cross_val && minval == other.minval && default_val == other.default_val && precision == other.precision && print_val == other.print_val && at_val == other.at_val && on_val == other.on_val && off_val == other.off_val && rfc_level == other.rfc_level && output == other.output && min_thresh == other.min_thresh && max_thresh == other.max_thresh && frac_max == other.frac_max && when_variable == other.when_variable && when_condition == other.when_condition && variable2 == other.variable2 && trig_variable == other.trig_variable && trig_condition == other.trig_condition && trig_val == other.trig_val && trig_frac_max == other.trig_frac_max && trig_td == other.trig_td && trig_rise == other.trig_rise && trig_fall == other.trig_fall && trig_cross == other.trig_cross && trig_at_val == other.trig_at_val && targ_variable == other.targ_variable && targ_condition == other.targ_condition && targ_val == other.targ_val && targ_frac_max == other.targ_frac_max && targ_td == other.targ_td && targ_rise == other.targ_rise && targ_fall == other.targ_fall && targ_cross == other.targ_cross && targ_at_val == other.targ_at_val && error_file == other.error_file && indepvarcol == other.indepvarcol && depvarcol == other.depvarcol && comp_function == other.comp_function && numfreq == other.numfreq && gridsize == other.gridsize && binsiz == other.binsiz && maxfreq == other.maxfreq && minfreq == other.minfreq && nbharm == other.nbharm && goal == other.goal && weight == other.weight;
}

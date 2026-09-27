#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/util.h"
#include "netlist_lexer.h"

namespace
{

    // Y-prefix device types supported by Xyce with multi-letter codes (Xyce RG Table 2-35)
    const std::vector<std::string_view> Y_PREFIXES = {"YMEMRISTOR", "YPDE", "YACC", "YLIN"};

    // standard node counts for single-letter device types (Xyce RG Table 2-35)
    const std::map<char, int, std::less<>> NODE_COUNTS = {
        {'B', 2}, {'C', 2}, {'D', 2}, {'E', 4}, {'F', 2}, {'G', 4}, {'H', 2}, {'I', 2}, {'J', 3}, {'K', 0}, {'L', 2}, {'M', 4}, {'O', 4}, {'P', 2}, {'Q', 3}, {'R', 2}, {'S', 4}, {'T', 4}, {'U', 2}, {'V', 2}, {'W', 2}, {'Z', 3},
    };

    // comprehensive simulation directives, modeling keywords, and builtin functions (Xyce RG Chapter 2)
    const std::set<std::string, std::less<>> KEYWORDS = {
        "ABS", "AC", "ACOS", "ACOSH", "ADDRESISTORS", "AGAUSS", "AKIMA", "AM", "ARCTAN", "ASIN", "ASINH", "AT", "ATAN", "ATAN2", "ATANH", "AVG", "BLACKMAN", "BLI", "CEIL", "CONTROL", "COS", "COSH", "CROSS", "CSV", "CUBIC", "DATA", "DB", "DC", "DCVOLT", "DDT", "DDX", "DEC", "DEVICE", "DIAGNOSTIC", "DIST", "DISTOF1", "DISTOF2", "EMBEDDEDSAMPLES", "EMBEDDEDSAMPLING", "END", "ENDDATA", "ENDS", "ERR", "ERR1", "ERR2", "EXP", "FALL", "FALSE", "FASTTABLE", "FFT", "FILE", "FIND", "FLOOR", "FMOD", "FORMAT", "FOUR", "FUNC", "GAUSS", "GLOBAL", "GLOBAL_PARAM", "GOAL", "HAMMING", "HANN", "HB", "HBINT", "I", "IB", "IC", "ID", "IDB", "IE", "IF", "IG", "II", "IM", "IMG", "INC", "INCLUDE", "INCL", "INT", "INTEG", "IP", "IR", "IS", "LAST", "LEVEL", "LIB", "LIMIT", "LIN", "LINSOL", "LINSOL-AC", "LINSOL-HB", "LN", "LOCA", "LOG", "LOG10", "M", "MAX", "MEAS", "MEASURE", "MIN", "MINVAL", "MODEL", "NINT", "NODCPATH", "NOISE", "NODESET", "NONLIN", "NONLIN-HB", "NP", "OCT", "OFF", "ON", "ONETERMINAL", "OP", "OPTIONS", "OUTPUT", "P", "PARAM", "PARAMS", "PARAMS:", "PARSER", "PCES", "PDB", "PH", "PLOT", "POLY", "POW", "PP", "PREPROCESS", "PRINT", "PROBE", "PULSE", "PWL", "PWR", "PWRS", "R", "RAW", "RE", "RECTANGULAR", "REMOVEUNUSED", "REPLACEGROUND", "RESTART", "RESULT", "RISE", "RMS", "SAMPLES", "SAMPLING", "SAVE", "SDT", "SENS", "SENSITIVITY", "SFFM", "SGN", "SIGN", "SIN", "SINH", "SPICE_EXP", "SPICE_PULSE", "SPICE_SFFM", "SPICE_SIN", "SPLINE", "SQRT", "STD", "STEP", "STP", "SUBCKT", "TABLE", "TABLEFILE", "TAN", "TANH", "TARG", "TD", "TEMP", "TIMEINT", "TITLE", "TRAN", "TRIG", "TRUE", "UNORM", "URAMP", "V", "VAL", "VDB", "VI", "VM", "VP", "VR", "W", "WHEN", "WINDOW", "WODICKA",
    };

    // a line slice with the delimiter that terminated it
    struct LineSlice
    {
        // line content without the trailing newline bytes
        std::string_view content;
        // delimiter ("\n", "\r\n", or "\r"); empty for the final line without one
        std::string_view ending;
    };

    // split raw text into line views preserving the newline delimiters
    std::vector<LineSlice> split_lines(std::string_view text) {
        // empty input yields a single empty line
        if (text.empty())
            return {{}};
        // container for extracted lines
        std::vector<LineSlice> lines;
        // start index for line search
        size_t start = 0;
        // iterate through characters searching for newline delimiters
        while (start < text.size()) {
            // find next carriage return or line feed
            auto pos = text.find_first_of("\r\n", start);
            // handle the final line when no more newlines exist
            if (pos == std::string_view::npos) {
                // append the remaining text slice with no delimiter
                lines.push_back({text.substr(start), {}});
                // exit the loop
                break;
            }
            // append line content up to the newline delimiter
            // advance past CRLF sequence or single newline character
            if (text[pos] == '\r' && pos + 1 < text.size() && text[pos + 1] == '\n') {
                lines.push_back({text.substr(start, pos - start), text.substr(pos, 2)});
                start = pos + 2;
            }
            else {
                lines.push_back({text.substr(start, pos - start), text.substr(pos, 1)});
                start = pos + 1;
            }
        }
        // trailing newline produces a final empty line
        if (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            lines.push_back({});
        // return the complete set of line views
        return lines;
    }

    // test whether a string view represents a SPICE numeric literal with optional scale/unit or imaginary J
    bool is_spice_number(std::string_view s) {
        // empty string cannot be a number
        if (s.empty())
            return false;
        // current scanning index
        size_t idx = 0;
        // consume optional leading sign
        if (s[idx] == '+' || s[idx] == '-')
            idx++;
        // sign alone is not a valid number
        if (idx >= s.size())
            return false;
        // track whether at least one digit was encountered
        bool has_digits = false;
        // consume integer digits
        while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
            // mark that a digit was found
            has_digits = true;
            // advance to next character
            idx++;
        }
        // consume optional fractional part
        if (idx < s.size() && s[idx] == '.') {
            // advance past the decimal point
            idx++;
            // consume fractional digits
            while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
                // mark that a digit was found
                has_digits = true;
                // advance to next character
                idx++;
            }
        }
        // number must have at least one digit
        if (!has_digits)
            return false;
        // consume optional scientific exponent
        if (idx < s.size() && (s[idx] == 'e' || s[idx] == 'E')) {
            // tentative exponent index
            size_t e_idx = idx + 1;
            // consume optional exponent sign
            if (e_idx < s.size() && (s[e_idx] == '+' || s[e_idx] == '-'))
                e_idx++;
            // verify that digits follow the exponent marker
            if (e_idx < s.size() && std::isdigit(static_cast<unsigned char>(s[e_idx]))) {
                // commit exponent start
                idx = e_idx;
                // consume exponent digits
                while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx])))
                    idx++;
            }
        }
        // any remaining characters represent scale factor, units, or imaginary 'j'/'J' and must not contain digits
        for (size_t k = idx; k < s.size(); ++k) {
            // reject if alphanumeric suffix contains embedded digits (e.g. 1N4148 is a diode model, not a number)
            if (std::isdigit(static_cast<unsigned char>(s[k])))
                return false;
        }
        // string is a valid SPICE number
        return true;
    }

    // test whether a string is a complex SPICE numeric literal of the form
    // <real>(+|-)<imaginary>J (Xyce RG 2.2.1: the imaginary part carries the
    // suffix letter J, e.g. 1.0+2.0J)
    bool is_complex_spice_number(std::string_view s) {
        // empty string cannot be a number
        if (s.empty())
            return false;
        // current scanning index
        size_t idx = 0;
        // consume optional leading sign
        if (s[idx] == '+' || s[idx] == '-')
            idx++;
        // track whether at least one digit was encountered in the real part
        bool has_digits = false;
        // consume integer digits
        while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
            // mark that a digit was found
            has_digits = true;
            // advance to next character
            idx++;
        }
        // consume optional fractional part
        if (idx < s.size() && s[idx] == '.') {
            // advance past the decimal point
            idx++;
            // consume fractional digits
            while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
                // mark that a digit was found
                has_digits = true;
                // advance to next character
                idx++;
            }
        }
        // real part must have at least one digit
        if (!has_digits)
            return false;
        // consume optional scientific exponent in the real part
        if (idx < s.size() && (s[idx] == 'e' || s[idx] == 'E')) {
            // tentative exponent index
            size_t e_idx = idx + 1;
            // consume optional exponent sign
            if (e_idx < s.size() && (s[e_idx] == '+' || s[e_idx] == '-'))
                e_idx++;
            // verify that digits follow the exponent marker
            if (e_idx < s.size() && std::isdigit(static_cast<unsigned char>(s[e_idx]))) {
                // commit exponent start
                idx = e_idx;
                // consume exponent digits
                while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx])))
                    idx++;
            }
        }
        // a complex literal requires an explicit real/imaginary separator
        if (idx >= s.size() || (s[idx] != '+' && s[idx] != '-'))
            return false;
        // skip the separator
        idx++;
        // track whether at least one digit was encountered in the imaginary part
        bool has_imag_digits = false;
        // consume imaginary integer digits
        while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
            // mark that a digit was found
            has_imag_digits = true;
            // advance to next character
            idx++;
        }
        // consume optional fractional part
        if (idx < s.size() && s[idx] == '.') {
            // advance past the decimal point
            idx++;
            // consume fractional digits
            while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
                // mark that a digit was found
                has_imag_digits = true;
                // advance to next character
                idx++;
            }
        }
        // imaginary part must have at least one digit
        if (!has_imag_digits)
            return false;
        // consume optional scientific exponent in the imaginary part
        if (idx < s.size() && (s[idx] == 'e' || s[idx] == 'E')) {
            // tentative exponent index
            size_t e_idx = idx + 1;
            // consume optional exponent sign
            if (e_idx < s.size() && (s[e_idx] == '+' || s[e_idx] == '-'))
                e_idx++;
            // verify that digits follow the exponent marker
            if (e_idx < s.size() && std::isdigit(static_cast<unsigned char>(s[e_idx]))) {
                // commit exponent start
                idx = e_idx;
                // consume exponent digits
                while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx])))
                    idx++;
            }
        }
        // consume optional unit/scale suffix characters before the J marker
        // (e.g. 500mJ); digits end the scan and fail the literal below
        while (idx < s.size() && s[idx] != 'J' && s[idx] != 'j' && !std::isdigit(static_cast<unsigned char>(s[idx])))
            idx++;
        // the imaginary part must carry the J suffix and end the literal
        if (idx + 1 != s.size() || (s[idx] != 'J' && s[idx] != 'j'))
            return false;
        // string is a valid complex SPICE number
        return true;
    }

    // determine the expected node count for a device instance name
    int get_device_node_count(std::string_view name) {
        // empty name has no nodes
        if (name.empty())
            return 0;
        // convert name to uppercase for matching
        const std::string upper = to_upper(name);
        // check Y-prefix devices first
        for (const auto& prefix : Y_PREFIXES) {
            // compare against prefix length
            if (upper.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), upper.begin())) {
                // YACC has 3 nodes, YPDE has 0, YMEMRISTOR and YLIN have 2
                if (prefix == "YACC")
                    return 3;
                if (prefix == "YPDE")
                    return 0;
                return 2;
            }
        }
        // check standard single letter device types
        const char first_char = upper[0];
        // subcircuit calls have variable node count
        if (first_char == 'X')
            return -1;
        // find device type in table
        const auto it = NODE_COUNTS.find(first_char);
        // return table count or default 2
        if (it != NODE_COUNTS.end())
            return it->second;
        return 2;
    }

    // test whether an identifier matches a valid SPICE device prefix
    bool is_device_name(std::string_view s) {
        // empty string cannot be a device
        if (s.empty())
            return false;
        // reject if starts with a digit, dot, or global prefix '$'
        if (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '.' || s[0] == '$')
            return false;
        // convert to uppercase
        const std::string upper = to_upper(s);
        // check multi-letter Y prefixes
        for (const auto& prefix : Y_PREFIXES) {
            // match prefix
            if (upper.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), upper.begin()))
                return true;
        }
        // check single letter prefix
        const char first = upper[0];
        // check subcircuit instance prefix
        if (first == 'X')
            return true;
        // check known single-letter device types
        return NODE_COUNTS.contains(first);
    }

    // test whether a string is a recognized keyword
    bool is_keyword(std::string_view s) {
        // convert to uppercase for lookup
        const std::string upper = to_upper(s);
        // query keyword set
        return KEYWORDS.contains(upper);
    }

    // tokenize a single line preserving all characters
    NetlistTokenLine tokenize_single_line(std::string_view line) {
        // container for line tokens
        NetlistTokenLine result;
        // empty line produces empty token list
        if (line.empty())
            return result;
        // total line length
        const size_t n = line.size();
        // find first non-whitespace character position
        const auto first_non_ws = line.find_first_not_of(" \t");
        // line containing only whitespace produces single whitespace token
        if (first_non_ws == std::string_view::npos) {
            // emit entire whitespace line
            result.m_tokens.push_back({std::string(line), NetlistTokenType::WHITESPACE});
            return result;
        }
        // full comment line begins with '*' or ';' (Xyce RG 2.1.39)
        const char first_char = line[first_non_ws];
        if (first_char == '*' || first_char == ';') {
            // emit leading whitespace if present
            if (first_non_ws > 0)
                result.m_tokens.push_back({std::string(line.substr(0, first_non_ws)), NetlistTokenType::WHITESPACE});
            // emit the remainder of the line as a comment
            result.m_tokens.push_back({std::string(line.substr(first_non_ws)), NetlistTokenType::COMMENT});
            return result;
        }
        // determine line classification flags
        const bool is_continuation = (first_char == '+');
        const bool is_directive_line = (first_char == '.');
        // tracking state for device instance lines
        bool has_emitted_device = is_continuation || is_directive_line;
        bool is_expecting_y_device_name = false;
        int expected_nodes = 0;
        int nodes_consumed = 0;
        // current scan index
        size_t i = 0;
        // iterate across line characters
        while (i < n) {
            // scan consecutive whitespace characters
            if (line[i] == ' ' || line[i] == '\t') {
                // start of whitespace segment
                const size_t start = i;
                // consume whitespace characters
                while (i < n && (line[i] == ' ' || line[i] == '\t'))
                    i++;
                // emit whitespace token
                result.m_tokens.push_back({std::string(line.substr(start, i - start)), NetlistTokenType::WHITESPACE});
                continue;
            }
            // inline comment begins with ';' (Xyce RG 2.1.39.2)
            if (line[i] == ';') {
                // emit the rest of the line as an inline comment
                result.m_tokens.push_back({std::string(line.substr(i)), NetlistTokenType::COMMENT});
                break;
            }
            // mathematical expression enclosed in curly braces (Xyce RG 2.2)
            if (line[i] == '{') {
                // start of expression
                const size_t start = i;
                // find matching closing brace
                const auto closing = line.find('}', start + 1);
                // compute end position
                const size_t end = (closing != std::string_view::npos) ? closing + 1 : n;
                // emit expression token
                result.m_tokens.push_back({std::string(line.substr(start, end - start)), NetlistTokenType::EXPRESSION});
                // advance index past expression
                i = end;
                continue;
            }
            // quoted string literal or single-quoted expression (Xyce RG 2.2)
            if (line[i] == '"' || line[i] == '\'') {
                // quote delimiter
                const char quote = line[i];
                // start of string
                const size_t start = i;
                // find matching closing quote
                const auto closing = line.find(quote, start + 1);
                // compute end position
                const size_t end = (closing != std::string_view::npos) ? closing + 1 : n;
                // emit string literal token
                result.m_tokens.push_back({std::string(line.substr(start, end - start)), NetlistTokenType::STRING_LITERAL});
                // advance index past string
                i = end;
                continue;
            }
            // continuation character at start of continuation line (Xyce RG 2.1.39.3)
            if (is_continuation && i == first_non_ws && line[i] == '+') {
                // emit continuation token
                result.m_tokens.push_back({std::string(line.substr(i, 1)), NetlistTokenType::CONTINUATION});
                // advance past '+'
                i++;
                continue;
            }
            // simulation directive starting with '.' (Xyce RG 2.1)
            if (line[i] == '.' && (i + 1 < n && !std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
                // start of directive
                const size_t start = i;
                // advance past leading dot
                i++;
                // consume directive name characters
                while (i < n && !std::isspace(static_cast<unsigned char>(line[i])) && line[i] != '=' && line[i] != '(' && line[i] != ')' && line[i] != ';')
                    i++;
                // emit directive token
                result.m_tokens.push_back({std::string(line.substr(start, i - start)), NetlistTokenType::DIRECTIVE});
                continue;
            }
            // exponentiation operator '**' (Xyce RG Table 2-32)
            if (line[i] == '*' && i + 1 < n && line[i + 1] == '*') {
                // emit exponentiation operator
                result.m_tokens.push_back({std::string(line.substr(i, 2)), NetlistTokenType::OPERATOR});
                // advance two characters
                i += 2;
                continue;
            }
            // relational operators '==', '!=', '<=', '>=' (Xyce RG Table 2-32)
            if ((line[i] == '=' || line[i] == '!' || line[i] == '<' || line[i] == '>') && i + 1 < n && line[i + 1] == '=') {
                // emit relational operator
                result.m_tokens.push_back({std::string(line.substr(i, 2)), NetlistTokenType::OPERATOR});
                // advance two characters
                i += 2;
                continue;
            }
            // single character operators and delimiters
            if (line[i] == '=' || line[i] == '(' || line[i] == ')' || line[i] == ',' || line[i] == '^' || line[i] == '?' || line[i] == '~' || line[i] == '|' || line[i] == '&' || line[i] == '<' || line[i] == '>') {
                // emit operator token
                result.m_tokens.push_back({std::string(line.substr(i, 1)), NetlistTokenType::OPERATOR});
                // advance index
                i++;
                continue;
            }
            // colon operator (hierarchy separator or ternary separator)
            if (line[i] == ':') {
                // emit operator token
                result.m_tokens.push_back({std::string(line.substr(i, 1)), NetlistTokenType::OPERATOR});
                // advance index
                i++;
                continue;
            }
            // arithmetic operators '+', '-', '*', '/' when standalone
            if ((line[i] == '+' || line[i] == '-') && (i + 1 >= n || (!std::isdigit(static_cast<unsigned char>(line[i + 1])) && line[i + 1] != '.'))) {
                // emit operator token
                result.m_tokens.push_back({std::string(line.substr(i, 1)), NetlistTokenType::OPERATOR});
                // advance index
                i++;
                continue;
            }
            if (line[i] == '*' || line[i] == '/') {
                // emit operator token
                result.m_tokens.push_back({std::string(line.substr(i, 1)), NetlistTokenType::OPERATOR});
                // advance index
                i++;
                continue;
            }
            // word token scanning
            const size_t start = i;
            // consume characters forming a word or token
            while (i < n && !std::isspace(static_cast<unsigned char>(line[i])) && line[i] != '=' && line[i] != '(' && line[i] != ')' && line[i] != ',' && line[i] != '^' && line[i] != ';' && line[i] != '{' && line[i] != '"' && line[i] != '\'') {
                // check '+' or '-' inside identifiers vs standalone operators
                if ((line[i] == '+' || line[i] == '-') && i > start) {
                    const char prev = line[i - 1];
                    // scientific exponent e.g. 1e-6, 2.5e+3
                    if (prev == 'e' || prev == 'E') {
                        i++;
                        continue;
                    }
                    // terminal suffix in pin/node names e.g. IN+, IN-, 1+, 1- (Xyce RG 2.3.2)
                    if (i + 1 == n || std::isspace(static_cast<unsigned char>(line[i + 1])) || line[i + 1] == ')' || line[i + 1] == ',' || line[i + 1] == ';') {
                        i++;
                        break;
                    }
                    // embedded hyphen in device or node name e.g. R-1, NET-A (Xyce RG 2.3.2);
                    // KiCad net names may also continue with an underscore (Net-_U303A-G2_)
                    if (std::isalnum(static_cast<unsigned char>(line[i + 1])) || line[i + 1] == '_') {
                        i++;
                        continue;
                    }
                    break;
                }
                // colon handling: a trailing colon (followed by a word boundary)
                // belongs to the word e.g. the PARAMS: keyword (Xyce RG 2.3.33);
                // a mid-word colon is a hierarchical separator e.g. X1:IN (Xyce RG 2.3.1.2)
                if (line[i] == ':') {
                    const char next = (i + 1 < n) ? line[i + 1] : ' ';
                    const bool trailing = std::isspace(static_cast<unsigned char>(next)) || next == '=' || next == '(' || next == ')' || next == ',' || next == '^' || next == ';' || next == '{' || next == '"' || next == '\'' || next == ':';
                    // mid-word colon ends the word before the separator
                    if (!trailing)
                        break;
                    // trailing colon is consumed and ends the word
                    i++;
                    break;
                }
                i++;
            }
            // extracted word view
            const std::string_view word = line.substr(start, i - start);
            const std::string upper_word = to_upper(word);
            // classify word token
            if (is_expecting_y_device_name) {
                // reset Y-device expectation flag
                is_expecting_y_device_name = false;
                // mark that device was emitted
                has_emitted_device = true;
                // emit Y-device instance name
                result.m_tokens.push_back({std::string(word), NetlistTokenType::DEVICE});
            }
            else if (!has_emitted_device && (upper_word == "YMEMRISTOR" || upper_word == "YLIN" || upper_word == "YACC" || upper_word == "YPDE")) {
                // set Y-device instance expectation flag
                is_expecting_y_device_name = true;
                // record expected node count
                expected_nodes = get_device_node_count(word);
                // emit Y-device type token
                result.m_tokens.push_back({std::string(word), NetlistTokenType::DEVICE});
            }
            else if (!has_emitted_device && is_device_name(word)) {
                // mark that device was emitted
                has_emitted_device = true;
                // record expected node count
                expected_nodes = get_device_node_count(word);
                // emit device token
                result.m_tokens.push_back({std::string(word), NetlistTokenType::DEVICE});
            }
            else if (word.size() >= 2 && word[0] == '$' && (word[1] == 'G' || word[1] == 'g')) {
                // global nodes begin with '$G' (Xyce RG 2.3.1.1 & 2.3.2)
                result.m_tokens.push_back({std::string(word), NetlistTokenType::NODE});
            }
            else if (has_emitted_device && !is_directive_line && (expected_nodes < 0 || nodes_consumed < expected_nodes)) {
                // increment consumed nodes counter
                nodes_consumed++;
                // emit node token
                result.m_tokens.push_back({std::string(word), NetlistTokenType::NODE});
            }
            else if (is_keyword(word)) {
                // emit keyword token
                result.m_tokens.push_back({std::string(word), NetlistTokenType::KEYWORD});
            }
            else if (upper_word == "GND" || upper_word == "GROUND") {
                // emit ground node token
                result.m_tokens.push_back({std::string(word), NetlistTokenType::NODE});
            }
            else if (is_spice_number(word) || is_complex_spice_number(word)) {
                // emit numeric literal token
                result.m_tokens.push_back({std::string(word), NetlistTokenType::NUMBER});
            }
            else {
                // emit default plain text token
                result.m_tokens.push_back({std::string(word), NetlistTokenType::PLAIN_TEXT});
            }
        }
        // X-device instance lines carry the subcircuit name as a trailing token
        // (Xyce RG 2.3.33: X<name> [node]* <subcircuit name> [PARAMS: ...]),
        // which must not be classified as a node
        if (expected_nodes < 0 && !result.m_tokens.empty()) {
            // locate the trailing parameter keyword; when present, the subcircuit
            // name is the token right before it and the rest is the parameter list
            std::size_t params_index = result.m_tokens.size();
            for (std::size_t t = 0; t < result.m_tokens.size(); ++t) {
                // ignore whitespace and comments when searching for the keyword
                if (result.m_tokens[t].m_type == NetlistTokenType::WHITESPACE || result.m_tokens[t].m_type == NetlistTokenType::COMMENT)
                    continue;
                const std::string upper_text = to_upper(result.m_tokens[t].m_text);
                if (upper_text == "PARAMS:" || upper_text == "PARAMS") {
                    params_index = t;
                    break;
                }
            }
            // locate the subcircuit name: the last non-whitespace, non-comment
            // token before PARAMS: (or at the end of the line)
            const std::size_t limit = (params_index < result.m_tokens.size()) ? params_index : result.m_tokens.size();
            std::size_t name_index = result.m_tokens.size();
            for (std::size_t t = limit; t-- > 0;) {
                // ignore whitespace and comments when searching for the name
                if (result.m_tokens[t].m_type == NetlistTokenType::WHITESPACE || result.m_tokens[t].m_type == NetlistTokenType::COMMENT)
                    continue;
                name_index = t;
                break;
            }
            // reclassify the subcircuit name; the node guard skips lines without
            // a name token (e.g. a bare device reference)
            if (name_index < limit && result.m_tokens[name_index].m_type == NetlistTokenType::NODE)
                result.m_tokens[name_index].m_type = NetlistTokenType::MODEL;
            // reclassify the parameter list tokens through the standard fallback
            for (std::size_t t = params_index; t < result.m_tokens.size(); ++t) {
                // only node-classified tokens need reclassification
                if (result.m_tokens[t].m_type != NetlistTokenType::NODE)
                    continue;
                const std::string upper_text = to_upper(result.m_tokens[t].m_text);
                if (is_keyword(result.m_tokens[t].m_text))
                    result.m_tokens[t].m_type = NetlistTokenType::KEYWORD;
                else if (upper_text == "GND" || upper_text == "GROUND")
                    result.m_tokens[t].m_type = NetlistTokenType::NODE;
                else if (is_spice_number(result.m_tokens[t].m_text) || is_complex_spice_number(result.m_tokens[t].m_text))
                    result.m_tokens[t].m_type = NetlistTokenType::NUMBER;
                else
                    result.m_tokens[t].m_type = NetlistTokenType::PLAIN_TEXT;
            }
        }
        // return completed tokenized line
        return result;
    }

} // namespace

std::vector<NetlistTokenLine> tokenize_netlist(std::string_view netlist) {
    // split input text into individual lines with their delimiters
    const auto raw_lines = split_lines(netlist);
    // allocate result line vector
    std::vector<NetlistTokenLine> tokenized_lines;
    tokenized_lines.reserve(raw_lines.size());
    // tokenize each line independently, preserving the line delimiter
    for (const auto& line : raw_lines) {
        // tokenize single line and add to collection
        tokenized_lines.emplace_back(tokenize_single_line(line.content));
        // record the delimiter so byte-for-byte reconstruction stays possible
        tokenized_lines.back().m_line_ending = std::string(line.ending);
    }
    // return all tokenized lines
    return tokenized_lines;
}

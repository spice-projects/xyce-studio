#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "core/util.h"
#include "fft_parameters.h"

// init allowed window values from the reference guide
static const std::set<std::string> FFT_ALLOWED_WINDOW_VALUES = {
    "RECT", "RECTANGULAR", "BART", "BARTLETT", "BARTLETTHANN", "BLACK", "BLACKMAN", "HAMM", "HAMMING", "HANN", "HANNING", "HARRIS", "BLACKMANHARRIS", "NUTTALL", "COSINE2", "COSINE4", "HALFCYCLESINE", "HALFCYCLESINE3", "HALFCYCLESINE6", "TRIANGULAR",
};

// init allowed format values from the reference guide
static const std::set<std::string> FFT_ALLOWED_FORMAT_VALUES = {
    "NORM",
    "UNORM",
};

// tokenize a .FFT statement respecting brace-enclosed expressions
static std::vector<std::string> tokenize_fft_statement(const std::string& fft_statement) {
    // init token list
    std::vector<std::string> tokens;
    // init current token buffer
    std::string current;
    // init brace nesting depth
    int brace_depth = 0;
    // iterate characters
    for (const char ch : fft_statement) {
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

FftParameters::FftParameters(std::string output_variable, std::string np, std::string window, std::string alfa, std::string fft_format, std::string start, std::string stop, std::string freq, std::string fmin, std::string fmax) :
    output_variable(std::move(output_variable)), np(std::move(np)), window(std::move(window)), alfa(std::move(alfa)), fft_format(std::move(fft_format)), start(std::move(start)), stop(std::move(stop)), freq(std::move(freq)), fmin(std::move(fmin)), fmax(std::move(fmax)) {}

std::optional<FftParameters> FftParameters::from_xyce_statement(const std::string& fft_statement) {
    // parse tokens
    const auto tokens = tokenize_fft_statement(fft_statement);
    // reject non-fft statements
    if (tokens.size() < 2 || to_upper(tokens[0]) != ".FFT") {
        // return none
        return std::nullopt;
    }
    // required positional output variable
    const std::string output_variable = tokens[1];
    // init options
    std::string np;
    // init window
    std::string window;
    // init alfa
    std::string alfa;
    // init format
    std::string fft_format;
    // init start
    std::string start;
    // init stop
    std::string stop;
    // init freq
    std::string freq;
    // init fmin
    std::string fmin;
    // init fmax
    std::string fmax;
    // iterate remaining tokens
    for (size_t i = 2; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        // check for equals sign
        const auto eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            // split key and value
            const std::string key = to_upper(token.substr(0, eq_pos));
            const std::string val = token.substr(eq_pos + 1);
            // map np
            if (key == "NP") {
                // set np
                np = val;
            }
            // map window
            else if (key == "WINDOW") {
                // normalize window
                const std::string norm_window = to_upper(val);
                // validate window
                if (FFT_ALLOWED_WINDOW_VALUES.count(norm_window)) {
                    // set window
                    window = norm_window;
                }
                // handle invalid window — skip silently
            }
            // map alfa
            else if (key == "ALFA") {
                // set alfa
                alfa = val;
            }
            // map format
            else if (key == "FORMAT") {
                // normalize format
                const std::string norm_format = to_upper(val);
                // validate format
                if (FFT_ALLOWED_FORMAT_VALUES.count(norm_format)) {
                    // set format
                    fft_format = norm_format;
                }
                // handle invalid format — skip silently
            }
            // map start (FROM is a synonym)
            else if (key == "START" || key == "FROM") {
                // set start
                start = val;
            }
            // map stop (TO is a synonym)
            else if (key == "STOP" || key == "TO") {
                // set stop
                stop = val;
            }
            // map freq
            else if (key == "FREQ") {
                // set freq
                freq = val;
            }
            // map fmin
            else if (key == "FMIN") {
                // set fmin
                fmin = val;
            }
            // map fmax
            else if (key == "FMAX") {
                // set fmax
                fmax = val;
            }
            // handle unknown option — skip silently
        }
        // handle unexpected token — skip silently
    }
    // return model
    return FftParameters(output_variable, np, window, alfa, fft_format, start, stop, freq, fmin, fmax);
}

std::string FftParameters::to_xyce_statement() const {
    // init tokens
    std::vector<std::string> tokens = {".FFT", output_variable};
    // append np
    if (!np.empty()) {
        // append np token
        tokens.push_back("NP=" + np);
    }
    // append window
    if (!window.empty()) {
        // append window token
        tokens.push_back("WINDOW=" + window);
    }
    // append alfa
    if (!alfa.empty()) {
        // append alfa token
        tokens.push_back("ALFA=" + alfa);
    }
    // append format
    if (!fft_format.empty()) {
        // append format token
        tokens.push_back("FORMAT=" + fft_format);
    }
    // append start
    if (!start.empty()) {
        // append start token
        tokens.push_back("START=" + start);
    }
    // append stop
    if (!stop.empty()) {
        // append stop token
        tokens.push_back("STOP=" + stop);
    }
    // append freq
    if (!freq.empty()) {
        // append freq token
        tokens.push_back("FREQ=" + freq);
    }
    // append fmin
    if (!fmin.empty()) {
        // append fmin token
        tokens.push_back("FMIN=" + fmin);
    }
    // append fmax
    if (!fmax.empty()) {
        // append fmax token
        tokens.push_back("FMAX=" + fmax);
    }
    // build joined statement
    std::string result;
    for (size_t i = 0; i < tokens.size(); ++i) {
        // add separator
        if (i > 0) {
            result += ' ';
        }
        // add token
        result += tokens[i];
    }
    // return joined statement
    return result;
}

bool FftParameters::operator==(const FftParameters& other) const {
    // compare all fields for equality
    return output_variable == other.output_variable && np == other.np && window == other.window && alfa == other.alfa && fft_format == other.fft_format && start == other.start && stop == other.stop && freq == other.freq && fmin == other.fmin && fmax == other.fmax;
}

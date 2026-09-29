#include <cctype>
#include <string>
#include <vector>

#include "../core/util.h"
#include "option_parameters.h"

struct NetlistTopology;

// parse a series of option tokens into a normalized map
static std::map<std::string, std::string> parse_option_tokens(const std::vector<std::string>& tokens) {
    // parse a series of option tokens into a normalized dictionary
    std::map<std::string, std::string> options;
    for (const auto& token : tokens) {
        // skip empty tokens produced by extra whitespace
        if (token.empty()) {
            continue;
        }
        // split key/value pairs and normalize keys to uppercase
        const auto eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            const std::string key = to_upper(token.substr(0, eq_pos));
            const std::string val = token.substr(eq_pos + 1);
            options[key] = val;
            continue;
        }
        // support flag-style options without an explicit value
        options[to_upper(token)] = "";
    }
    return options;
}

// parse option tokens where INITIAL_INTERVAL is followed by bare interval change points
static std::map<std::string, std::string> parse_interval_option_tokens(const std::vector<std::string>& tokens) {
    // parse a series of option tokens into a normalized dictionary
    std::map<std::string, std::string> options;
    // tracks the last key that accepted trailing bare tokens (INITIAL_INTERVAL)
    std::string trailing_key;
    for (const auto& token : tokens) {
        // skip empty tokens produced by extra whitespace
        if (token.empty()) {
            continue;
        }
        // split key/value pairs and normalize keys to uppercase
        const auto eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            const std::string key = to_upper(token.substr(0, eq_pos));
            const std::string val = token.substr(eq_pos + 1);
            options[key] = val;
            // only INITIAL_INTERVAL accepts trailing bare time/interval pairs
            trailing_key = key == "INITIAL_INTERVAL" ? key : std::string();
            continue;
        }
        // append bare tokens following INITIAL_INTERVAL as space-separated change points
        if (!trailing_key.empty() && !options[trailing_key].empty()) {
            options[trailing_key] += " " + token;
            continue;
        }
        // support flag-style options without an explicit value
        options[to_upper(token)] = "";
    }
    return options;
}

// merge parsed option entries into a package map, keeping the first value found per key (RG 2.1.25)
static void merge_options(std::map<std::string, std::string>& target, std::map<std::string, std::string> parsed) {
    // iterate over every parsed option entry
    for (auto& [key, value] : parsed) {
        // insert only when the key is absent so the first value found wins
        target.emplace(std::move(key), std::move(value));
    }
}

OptionParameters::OptionParameters(std::map<std::string, std::string> device, std::map<std::string, std::string> timeint, std::map<std::string, std::string> nonlin, std::map<std::string, std::string> linsol, std::map<std::string, std::string> fft, std::map<std::string, std::string> diagnostic, std::map<std::string, std::string> parser, std::map<std::string, std::string> linsol_ac, std::map<std::string, std::string> loca, std::map<std::string, std::string> dist, std::map<std::string, std::string> measure, std::map<std::string, std::string> nonlin_tran, std::map<std::string, std::string> output, std::map<std::string, std::string> restart, std::map<std::string, std::string> samples, std::map<std::string, std::string> embeddedsamples) :
    device(std::move(device)), timeint(std::move(timeint)), nonlin(std::move(nonlin)), linsol(std::move(linsol)), fft(std::move(fft)), diagnostic(std::move(diagnostic)), parser(std::move(parser)), linsol_ac(std::move(linsol_ac)), loca(std::move(loca)), dist(std::move(dist)), measure(std::move(measure)), nonlin_tran(std::move(nonlin_tran)), output(std::move(output)), restart(std::move(restart)), samples(std::move(samples)), embeddedsamples(std::move(embeddedsamples)) {}

OptionParameters OptionParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init option groups
    std::map<std::string, std::string> device;
    std::map<std::string, std::string> timeint;
    std::map<std::string, std::string> nonlin;
    std::map<std::string, std::string> linsol;
    std::map<std::string, std::string> fft;
    std::map<std::string, std::string> diagnostic;
    std::map<std::string, std::string> parser;
    std::map<std::string, std::string> linsol_ac;
    std::map<std::string, std::string> loca;
    std::map<std::string, std::string> dist;
    std::map<std::string, std::string> measure;
    std::map<std::string, std::string> nonlin_tran;
    std::map<std::string, std::string> output;
    std::map<std::string, std::string> restart;
    // sampling analysis option entries (RG 2.1.25.13)
    std::map<std::string, std::string> samples;
    // embedded sampling analysis option entries (RG 2.1.25.14)
    std::map<std::string, std::string> embeddedsamples;
    // track whether the OUTPUT statement was already accepted (only the first statement counts, RG 2.1.25)
    bool output_seen = false;
    // track whether the RESTART statement was already accepted (only the first statement counts, RG 2.1.25)
    bool restart_seen = false;

    // parse each directive looking for supported option packages
    for (const auto& directive : directives) {
        // break directive into tokens
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

        // skip empty directives
        if (tokens.empty()) {
            continue;
        }

        // handle only .OPTIONS directives
        if (to_upper(tokens[0]) != ".OPTIONS" || tokens.size() <= 1) {
            continue;
        }

        // normalize the package name
        const std::string package = to_upper(tokens[1]);

        // handle the device package options
        if (package == "DEVICE") {
            // merge the entries keeping the first value found per key
            merge_options(device, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the time integration package options
        if (package == "TIMEINT") {
            // merge the entries keeping the first value found per key
            merge_options(timeint, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the nonlinear solver package options
        if (package == "NONLIN") {
            // merge the entries keeping the first value found per key
            merge_options(nonlin, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the transient nonlinear solver package options
        if (package == "NONLIN-TRAN") {
            // merge the entries keeping the first value found per key
            merge_options(nonlin_tran, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the output package options, only the first statement is applied (RG 2.1.25)
        if (package == "OUTPUT") {
            // guard so statements beyond the first are ignored entirely
            if (!output_seen) {
                // record that the output statement was accepted
                output_seen = true;
                // parse the interval options of the first statement
                output = parse_interval_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end()));
            }
            // skip the remaining package handlers
            continue;
        }
        // handle the restart package options, only the first statement is applied (RG 2.1.25)
        if (package == "RESTART") {
            // guard so statements beyond the first are ignored entirely
            if (!restart_seen) {
                // record that the restart statement was accepted
                restart_seen = true;
                // parse the interval options of the first statement
                restart = parse_interval_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end()));
            }
            // skip the remaining package handlers
            continue;
        }
        // handle the linear solver package options
        if (package == "LINSOL") {
            // merge the entries keeping the first value found per key
            merge_options(linsol, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the ac linear solver package options
        if (package == "LINSOL-AC") {
            // merge the entries keeping the first value found per key
            merge_options(linsol_ac, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the fft package options
        if (package == "FFT") {
            // merge the entries keeping the first value found per key
            merge_options(fft, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the diagnostic package options
        if (package == "DIAGNOSTIC") {
            // merge the entries keeping the first value found per key
            merge_options(diagnostic, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the parser package options
        if (package == "PARSER") {
            // merge the entries keeping the first value found per key
            merge_options(parser, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the continuation tracking package options
        if (package == "LOCA") {
            // merge the entries keeping the first value found per key
            merge_options(loca, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the parallel distribution package options
        if (package == "DIST") {
            // merge the entries keeping the first value found per key
            merge_options(dist, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the measure package options
        if (package == "MEASURE") {
            // merge the entries keeping the first value found per key
            merge_options(measure, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the sampling package options
        if (package == "SAMPLES") {
            // merge the entries keeping the first value found per key
            merge_options(samples, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
        // handle the embedded sampling package options
        if (package == "EMBEDDEDSAMPLES") {
            // merge the entries keeping the first value found per key
            merge_options(embeddedsamples, parse_option_tokens(std::vector<std::string>(tokens.begin() + 2, tokens.end())));
            // skip the remaining package handlers
            continue;
        }
    }

    // construct the option parameters instance from the collected package maps
    return OptionParameters(device, timeint, nonlin, linsol, fft, diagnostic, parser, linsol_ac, loca, dist, measure, nonlin_tran, output, restart, samples, embeddedsamples);
}

std::vector<std::string> OptionParameters::to_xyce_directives(const NetlistTopology& topology) const {
    (void)topology;
    // serialize configured option blocks in a deterministic order
    std::vector<std::string> directives;

    // build helper lambda to format options
    auto format_options = [](const std::map<std::string, std::string>& options) -> std::string {
        std::string result;
        for (const auto& [key, value] : options) {
            if (!result.empty()) {
                result += " ";
            }
            if (!value.empty()) {
                result += key + "=" + value;
            }
            else {
                result += key;
            }
        }
        return result;
    };

    if (!device.empty()) {
        directives.push_back(".OPTIONS DEVICE " + format_options(device));
    }
    if (!timeint.empty()) {
        directives.push_back(".OPTIONS TIMEINT " + format_options(timeint));
    }
    if (!nonlin.empty()) {
        directives.push_back(".OPTIONS NONLIN " + format_options(nonlin));
    }
    if (!nonlin_tran.empty()) {
        directives.push_back(".OPTIONS NONLIN-TRAN " + format_options(nonlin_tran));
    }
    if (!linsol.empty()) {
        directives.push_back(".OPTIONS LINSOL " + format_options(linsol));
    }
    if (!linsol_ac.empty()) {
        directives.push_back(".OPTIONS LINSOL-AC " + format_options(linsol_ac));
    }
    if (!fft.empty()) {
        directives.push_back(".OPTIONS FFT " + format_options(fft));
    }
    if (!diagnostic.empty()) {
        directives.push_back(".OPTIONS DIAGNOSTIC " + format_options(diagnostic));
    }
    if (!parser.empty()) {
        directives.push_back(".OPTIONS PARSER " + format_options(parser));
    }
    if (!loca.empty()) {
        directives.push_back(".OPTIONS LOCA " + format_options(loca));
    }
    if (!dist.empty()) {
        directives.push_back(".OPTIONS DIST " + format_options(dist));
    }
    if (!measure.empty()) {
        directives.push_back(".OPTIONS MEASURE " + format_options(measure));
    }
    if (!output.empty()) {
        directives.push_back(".OPTIONS OUTPUT " + format_options(output));
    }
    if (!restart.empty()) {
        directives.push_back(".OPTIONS RESTART " + format_options(restart));
    }
    // emit the sampling package line when configured
    if (!samples.empty()) {
        // append the formatted samples options directive
        directives.push_back(".OPTIONS SAMPLES " + format_options(samples));
    }
    // emit the embedded sampling package line when configured
    if (!embeddedsamples.empty()) {
        // append the formatted embeddedsamples options directive
        directives.push_back(".OPTIONS EMBEDDEDSAMPLES " + format_options(embeddedsamples));
    }

    return directives;
}

bool OptionParameters::operator==(const OptionParameters& other) const {
    // compare all fields for equality
    return device == other.device && timeint == other.timeint && nonlin == other.nonlin && linsol == other.linsol && fft == other.fft && diagnostic == other.diagnostic && parser == other.parser && linsol_ac == other.linsol_ac && loca == other.loca && dist == other.dist && measure == other.measure && nonlin_tran == other.nonlin_tran && output == other.output && restart == other.restart && samples == other.samples && embeddedsamples == other.embeddedsamples;
}

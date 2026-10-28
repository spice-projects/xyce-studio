#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "core/util.h"
#include "hb_simulation_parameters.h"

HbSimulationParameters::HbSimulationParameters(std::vector<std::string> frequencies, std::vector<int> harmonics, std::optional<int> tahb, std::optional<std::string> selectharms, std::optional<int> startup_periods, std::optional<PrintParameters> print_parameters, std::map<std::string, std::string> nonlin_options, std::map<std::string, std::string> linsol_options) :
    frequencies(std::move(frequencies)), harmonics(std::move(harmonics)), tahb(std::move(tahb)), selectharms(std::move(selectharms)), startup_periods(std::move(startup_periods)), print_parameters(std::move(print_parameters)), nonlin_options(std::move(nonlin_options)), linsol_options(std::move(linsol_options)) {}

std::optional<HbSimulationParameters> HbSimulationParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init defaults
    std::vector<std::string> frequencies;
    std::vector<int> harmonics;
    std::optional<int> tahb;
    std::optional<std::string> selectharms;
    std::optional<int> startup_periods;
    std::optional<PrintParameters> print_parameters;
    std::map<std::string, std::string> nonlin_options;
    std::map<std::string, std::string> linsol_options;

    // flag indicating whether a valid directive was found
    bool found = false;

    // parse directives
    for (const auto& directive : directives) {
        // tokenize the directive
        const auto tokens = tokenize(directive);

        // skip empty directives
        if (tokens.empty()) {
            continue;
        }

        const std::string cmd = to_upper(tokens[0]);

        // parse print directives and retain hb-specific output config
        if (cmd == ".PRINT") {
            // parse the print statement from the directive
            const auto print_statement = PrintParameters::from_xyce_statement(directive);
            // retain hb print parameters when found
            if (print_statement) {
                const std::string print_type_upper = to_upper(print_statement->print_type);
                if (print_type_upper == "HB" || print_type_upper == "HB_FD" || print_type_upper == "HB_TD") {
                    // store the parsed print parameters
                    print_parameters = *print_statement;
                }
            }
            continue;
        }

        // handle .OPTIONS
        if (cmd == ".OPTIONS" && tokens.size() > 1) {
            // get package
            const std::string pkg = to_upper(tokens[1]);

            // handle HBINT
            if (pkg == "HBINT") {
                // parse options
                for (size_t i = 2; i < tokens.size(); ++i) {
                    const auto& token = tokens[i];
                    // check for equals
                    const auto eq_pos = token.find('=');
                    if (eq_pos != std::string::npos) {
                        // split key and value
                        const auto key = to_upper(token.substr(0, eq_pos));
                        const auto val = token.substr(eq_pos + 1);

                        // handle NUMFREQ
                        if (key == "NUMFREQ") {
                            // split comma-separated values
                            std::string current_val;
                            for (size_t j = 0; j < val.length(); ++j) {
                                const char c = val[j];
                                if (c == ',') {
                                    // parse integer
                                    try {
                                        harmonics.push_back(std::stoi(current_val));
                                    }
                                    catch (...) {
                                        // ignore invalid NUMFREQ
                                    }
                                    current_val.clear();
                                }
                                else {
                                    current_val += c;
                                }
                            }
                            // parse last value
                            if (!current_val.empty()) {
                                try {
                                    harmonics.push_back(std::stoi(current_val));
                                }
                                catch (...) {
                                    // ignore
                                }
                            }
                        }
                        // handle TAHB
                        else if (key == "TAHB") {
                            // parse integer
                            try {
                                tahb = std::stoi(std::string(val));
                            }
                            catch (...) {
                                // ignore
                            }
                        }
                        // handle SELECTHARMS
                        else if (key == "SELECTHARMS") {
                            // store selectharms (lowercase)
                            selectharms = val;
                        }
                        // handle STARTUPPERIODS
                        else if (key == "STARTUPPERIODS") {
                            // parse integer
                            try {
                                startup_periods = std::stoi(std::string(val));
                            }
                            catch (...) {
                                // ignore
                            }
                        }
                    }
                }
                continue;
            }

            // handle NONLIN-HB
            if (pkg == "NONLIN-HB") {
                // parse options
                for (size_t i = 2; i < tokens.size(); ++i) {
                    const auto& token = tokens[i];
                    // check for equals
                    const auto eq_pos = token.find('=');
                    if (eq_pos != std::string::npos) {
                        // split key and value
                        const auto key = token.substr(0, eq_pos);
                        const auto val = token.substr(eq_pos + 1);
                        // store (uppercase key)
                        nonlin_options[to_upper(key)] = val;
                    }
                }
                continue;
            }

            // handle LINSOL-HB
            if (pkg == "LINSOL-HB") {
                // parse options
                for (size_t i = 2; i < tokens.size(); ++i) {
                    const auto& token = tokens[i];
                    // check for equals
                    const auto eq_pos = token.find('=');
                    if (eq_pos != std::string::npos) {
                        // split key and value
                        const auto key = token.substr(0, eq_pos);
                        const auto val = token.substr(eq_pos + 1);
                        // store (uppercase key)
                        linsol_options[to_upper(key)] = val;
                    }
                }
                continue;
            }
        }

        // skip non-HB directives
        if (cmd != ".HB") {
            continue;
        }

        // flag indicating a valid HB directive was found
        found = true;

        // collect all fundamental frequencies from remaining tokens
        for (size_t i = 1; i < tokens.size(); ++i) {
            frequencies.push_back(std::string(tokens[i]));
        }
    }

    // return instance if a valid directive was found
    if (!found) {
        return std::nullopt;
    }

    return HbSimulationParameters(frequencies, harmonics, tahb, selectharms, startup_periods, print_parameters, nonlin_options, linsol_options);
}

std::vector<std::string> HbSimulationParameters::to_xyce_directives(const NetlistTopology& topology) const {
    // init output directive list
    std::vector<std::string> directives;
    // topology reserved for future wildcard expansion; pass-through for now
    (void)topology;
    // build the hb directive with space-separated fundamental frequencies
    std::string hb_directive = ".HB";
    for (const auto& freq : frequencies) {
        hb_directive += " " + freq;
    }
    directives.push_back(hb_directive);

    // build hbint options
    std::vector<std::string> hbint_options;

    // append NUMFREQ when provided
    if (!harmonics.empty()) {
        // format as comma-separated integers
        std::string numfreq_str;
        for (size_t i = 0; i < harmonics.size(); ++i) {
            if (i > 0) {
                numfreq_str += ",";
            }
            numfreq_str += std::to_string(harmonics[i]);
        }
        hbint_options.push_back("NUMFREQ=" + numfreq_str);
    }

    // append TAHB when provided
    if (tahb.has_value()) {
        hbint_options.push_back("TAHB=" + std::to_string(*tahb));
    }

    // append SELECTHARMS when provided
    if (selectharms.has_value()) {
        hbint_options.push_back("SELECTHARMS=" + *selectharms);
    }

    // append STARTUPPERIODS when provided
    if (startup_periods.has_value()) {
        hbint_options.push_back("STARTUPPERIODS=" + std::to_string(*startup_periods));
    }

    // check if we have hbint options
    if (!hbint_options.empty()) {
        // append .OPTIONS HBINT line
        std::string hbint_directive = ".OPTIONS HBINT";
        for (const auto& opt : hbint_options) {
            hbint_directive += " " + opt;
        }
        directives.push_back(hbint_directive);
    }

    // append nonlin-hb options
    if (!nonlin_options.empty()) {
        std::string opts;
        for (const auto& [k, v] : nonlin_options) {
            if (!opts.empty()) {
                opts += " ";
            }
            opts += k + "=" + v;
        }
        directives.push_back(".OPTIONS NONLIN-HB " + opts);
    }

    // append linsol-hb options
    if (!linsol_options.empty()) {
        std::string opts;
        for (const auto& [k, v] : linsol_options) {
            if (!opts.empty()) {
                opts += " ";
            }
            opts += k + "=" + v;
        }
        directives.push_back(".OPTIONS LINSOL-HB " + opts);
    }

    // append hb print directive with topology-aware wildcard expansion
    if (print_parameters) {
        const std::string print_type_upper = to_upper(print_parameters->print_type);
        if (print_type_upper == "HB" || print_type_upper == "HB_FD" || print_type_upper == "HB_TD") {
            directives.push_back(print_parameters->to_xyce_statement());
        }
    }

    // return the full directive list
    return directives;
}

bool HbSimulationParameters::operator==(const HbSimulationParameters& other) const {
    // compare all fields for equality
    return frequencies == other.frequencies && harmonics == other.harmonics && tahb == other.tahb && selectharms == other.selectharms && startup_periods == other.startup_periods && print_parameters == other.print_parameters && nonlin_options == other.nonlin_options && linsol_options == other.linsol_options;
}

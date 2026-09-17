#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "../core/util.h"
#include "lin_simulation_parameters.h"

LinSimulationParameters::LinSimulationParameters(bool sparcalc, std::string format, std::string lintype, std::string dataformat, std::string file, std::string width, std::string precision, std::string sweep_mode, std::string points, std::string start, std::string end, std::string data_table_name, std::optional<PrintParameters> print_parameters) :
    sparcalc(sparcalc), format(std::move(format)), lintype(std::move(lintype)), dataformat(std::move(dataformat)), file(std::move(file)), width(std::move(width)), precision(std::move(precision)), sweep_mode(std::move(sweep_mode)), points(std::move(points)), start(std::move(start)), end(std::move(end)), data_table_name(std::move(data_table_name)), print_parameters(std::move(print_parameters)) {}

std::optional<LinSimulationParameters> LinSimulationParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init defaults
    bool sparcalc = true;
    std::string format = "TOUCHSTONE2";
    std::string lintype = "S";
    std::string dataformat = "RI";
    std::string file;
    std::string width;
    std::string precision;
    std::string sweep_mode = "LIN";
    std::string points;
    std::string start;
    std::string end;
    std::string data_table_name;
    std::optional<PrintParameters> print_parameters;

    // flag indicating whether a valid directive was found
    bool found = false;
    // flag indicating whether FILE= has been seen (gives FILE= precedence over FILENAME=)
    bool file_seen = false;

    // parse directives
    for (const auto& directive : directives) {
        // tokenize the directive
        const auto tokens = tokenize(directive);

        // skip empty directives
        if (tokens.empty()) {
            continue;
        }

        const std::string cmd = to_upper(tokens[0]);

        // parse print directives and retain lin-specific output config
        if (cmd == ".PRINT") {
            // parse the print statement from the directive
            const auto print_statement = PrintParameters::from_xyce_statement(directive);
            // retain ac print parameters when found
            if (print_statement) {
                const std::string print_type_upper = to_upper(print_statement->print_type);
                if (print_type_upper == "AC") {
                    // store the parsed print parameters
                    print_parameters = *print_statement;
                    continue;
                }
            }
        }

        // parse the embedded ac sweep directive
        if (cmd == ".AC") {
            if (tokens.size() < 2) {
                continue;
            }

            const std::string second = to_upper(tokens[1]);

            // handle DATA sweep: .AC DATA=<tablename>
            if (second.substr(0, 5) == "DATA=" && second.find('=') != std::string::npos) {
                // set sweep mode and data table name
                sweep_mode = "DATA";
                data_table_name = second.substr(5);
                continue;
            }

            // detect decade or octave log sweep: .AC DEC|OCT <points> <start> <end>
            if (second == "DEC" || second == "OCT") {
                sweep_mode = second;
                if (tokens.size() >= 5) {
                    points = tokens[2];
                    start = tokens[3];
                    end = tokens[4];
                }
                continue;
            }

            // linear sweep: .AC [LIN] <points> <start> <end>
            sweep_mode = "LIN";
            if (second == "LIN") {
                // explicit LIN keyword
                if (tokens.size() >= 5) {
                    points = tokens[2];
                    start = tokens[3];
                    end = tokens[4];
                }
            }
            else {
                // implicit LIN
                if (tokens.size() >= 4) {
                    points = tokens[1];
                    start = tokens[2];
                    end = tokens[3];
                }
            }
            continue;
        }

        // skip non-LIN directives
        if (cmd != ".LIN") {
            continue;
        }

        // flag indicating a valid LIN directive was found
        found = true;

        // parse keyword=value pairs from the .LIN directive
        for (size_t i = 1; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            // normalize the token for case-insensitive key detection
            const std::string upper = to_upper(token);

            // skip tokens without an equals sign
            if (upper.find('=') == std::string::npos) {
                continue;
            }

            // split key and value at the first equals sign
            const auto eq_pos = token.find('=');
            const auto key = token.substr(0, eq_pos);
            const auto val = token.substr(eq_pos + 1);

            const std::string key_upper = to_upper(key);

            if (key_upper == "SPARCALC") {
                // set sparcalc from the value
                sparcalc = (to_upper(val) == "1" || to_upper(val) == "TRUE" || to_upper(val) == "YES");
            }
            else if (key_upper == "FORMAT") {
                // set the output format
                format = to_upper(val);
            }
            else if (key_upper == "TYPE" || key_upper == "LINTYPE") {
                // set the s-parameter type
                // LINTYPE is the documented RG keyword; TYPE is accepted for backward compatibility
                lintype = to_upper(val);
            }
            else if (key_upper == "DATAFORMAT") {
                // set the data format
                dataformat = to_upper(val);
            }
            else if (key_upper == "FILE") {
                // set the output file name
                file = val;
                file_seen = true;
            }
            else if (key_upper == "FILENAME") {
                // HSPICE synonym for FILE=; FILE= takes precedence when both are given
                if (!file_seen) {
                    file = val;
                }
            }
            else if (key_upper == "WIDTH") {
                // set the column width
                width = val;
            }
            else if (key_upper == "PRECISION") {
                // set the output precision
                precision = val;
            }
        }
    }

    // return instance if a valid directive was found
    if (!found) {
        return std::nullopt;
    }

    return LinSimulationParameters(sparcalc, format, lintype, dataformat, file, width, precision, sweep_mode, points, start, end, data_table_name, print_parameters);
}

std::vector<std::string> LinSimulationParameters::to_xyce_directives(const NetlistTopology& topology) const {
    // init output directive list
    std::vector<std::string> directives;
    // topology reserved for future wildcard expansion; pass-through for now
    (void)topology;
    // build the embedded ac sweep directive
    std::string ac_directive = ".AC";
    if (sweep_mode == "DATA") {
        ac_directive += " DATA=" + data_table_name;
    }
    else if (sweep_mode == "DEC" || sweep_mode == "OCT") {
        ac_directive += " " + sweep_mode + " " + points + " " + start + " " + end;
    }
    else {
        // lin sweep (explicit)
        ac_directive += " LIN " + points + " " + start + " " + end;
    }
    directives.push_back(ac_directive);

    // build and append the .LIN directive
    // SPARCALC/FORMAT/LINTYPE/DATAFORMAT are always emitted so a config that
    // round-trips through the dialog keeps the original directive text
    std::string lin_directive = ".LIN";
    lin_directive += " SPARCALC=";
    lin_directive += sparcalc ? "1" : "0";
    if (!format.empty()) {
        lin_directive += " FORMAT=" + format;
    }
    if (!lintype.empty()) {
        lin_directive += " LINTYPE=" + lintype;
    }
    if (!dataformat.empty()) {
        lin_directive += " DATAFORMAT=" + dataformat;
    }
    if (!file.empty()) {
        lin_directive += " FILE=" + file;
    }
    if (!width.empty()) {
        lin_directive += " WIDTH=" + width;
    }
    if (!precision.empty()) {
        lin_directive += " PRECISION=" + precision;
    }
    directives.push_back(lin_directive);

    // append ac print directive with topology-aware wildcard expansion
    if (print_parameters) {
        const std::string print_type_upper = to_upper(print_parameters->print_type);
        if (print_type_upper == "AC") {
            directives.push_back(print_parameters->to_xyce_statement());
        }
    }

    // return the full directive list
    return directives;
}

bool LinSimulationParameters::operator==(const LinSimulationParameters& other) const {
    // compare all fields for equality
    return sparcalc == other.sparcalc && format == other.format && lintype == other.lintype && dataformat == other.dataformat && file == other.file && width == other.width && precision == other.precision && sweep_mode == other.sweep_mode && points == other.points && start == other.start && end == other.end && data_table_name == other.data_table_name && print_parameters == other.print_parameters;
}

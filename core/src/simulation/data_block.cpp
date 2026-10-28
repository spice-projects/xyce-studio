#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "data_block.h"

DataBlock::DataBlock(std::string name, std::vector<std::string> parameters, std::vector<std::vector<std::string>> records) :
    name(std::move(name)), parameters(std::move(parameters)), records(std::move(records)) {}

bool DataBlock::is_number(const std::string& s) {
    // normalize to lowercase
    std::string normalized = s;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return std::tolower(c); });

    // try standard float conversion
    try {
        // check if it is float
        size_t idx = 0;
        [[maybe_unused]] const float parsed = std::stof(normalized, &idx);
        // check if entire string was consumed
        if (idx == normalized.length()) {
            return true;
        }
    }
    catch (...) {
        // pass to check suffix
    }

    // define spice scale factor suffixes
    const std::vector<std::string> suffixes = {"meg", "mil", "f", "p", "n", "u", "m", "k", "g", "t"};
    // iterate suffixes to see if string ends with any
    for (const auto& suffix : suffixes) {
        // check suffix match
        if (normalized.length() > suffix.length() && normalized.substr(normalized.length() - suffix.length()) == suffix) {
            // try to parse value portion without suffix
            try {
                const std::string val = normalized.substr(0, normalized.length() - suffix.length());
                size_t idx = 0;
                [[maybe_unused]] const float parsed = std::stof(val, &idx);
                // check if entire value portion was consumed
                if (idx == val.length()) {
                    return true;
                }
            }
            catch (...) {
                // pass to next suffix
            }
        }
    }
    // return false
    return false;
}

std::vector<DataBlock> DataBlock::from_xyce_directives(const std::vector<std::string>& directives) {
    // init results list
    std::vector<DataBlock> results;
    // parse active data block variables
    std::string active_name;
    std::vector<std::string> active_params;
    std::vector<std::string> active_values;

    // iterate all directives to parse data blocks
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

        // skip empty directives
        if (tokens.empty()) {
            continue;
        }

        // get upper command
        std::string cmd = tokens[0];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return std::toupper(c); });

        // check for .DATA start
        if (cmd == ".DATA") {
            // extract table name
            active_name = (tokens.size() > 1) ? tokens[1] : "";

            // process the rest of the tokens as potential headers and values
            active_params.clear();
            active_values.clear();

            // iterate over subsequent tokens to separate parameters from values
            for (size_t i = 2; i < tokens.size(); ++i) {
                if (is_number(tokens[i])) {
                    active_values.push_back(tokens[i]);
                }
                else {
                    active_params.push_back(tokens[i]);
                }
            }
        }
        // handle spice continuation lines (+) inside an active .DATA block
        else if (cmd == "+" && !active_name.empty()) {
            // classify each continuation token as a parameter name or value
            for (size_t i = 1; i < tokens.size(); ++i) {
                if (is_number(tokens[i])) {
                    active_values.push_back(tokens[i]);
                }
                else {
                    active_params.push_back(tokens[i]);
                }
            }
        }
        // check for .ENDDATA
        else if (cmd == ".ENDDATA") {
            // if there is an active table, group the values into rows
            if (!active_name.empty()) {
                // number of columns
                const size_t num_cols = active_params.size();
                // list of records
                std::vector<std::vector<std::string>> records;

                // group values if there are columns
                if (num_cols > 0) {
                    // group values by columns count
                    for (size_t i = 0; i < active_values.size(); i += num_cols) {
                        // append record row
                        std::vector<std::string> row;
                        for (size_t j = 0; j < num_cols && (i + j) < active_values.size(); ++j) {
                            row.push_back(active_values[i + j]);
                        }
                        records.push_back(row);
                    }
                }

                // append the data block to results
                results.emplace_back(active_name, active_params, records);
            }
            // reset active block
            active_name.clear();
            active_params.clear();
            active_values.clear();
        }
    }

    // return vector of data blocks
    return results;
}

std::vector<std::string> DataBlock::to_xyce_directives() const {
    // init directive lines
    std::vector<std::string> lines;

    // append the start directive
    lines.push_back(".DATA " + name);

    // build parameters line
    std::string param_str;
    for (size_t i = 0; i < parameters.size(); ++i) {
        if (i > 0) {
            param_str += " ";
        }
        param_str += parameters[i];
    }

    // append parameters line with continuation char
    lines.push_back("+ " + param_str);

    // iterate all records to output them
    for (const auto& record : records) {
        // build record line
        std::string record_str;
        for (size_t i = 0; i < record.size(); ++i) {
            if (i > 0) {
                record_str += " ";
            }
            record_str += record[i];
        }

        // append record line with continuation char
        lines.push_back("+ " + record_str);
    }

    // append the end directive
    lines.push_back(".ENDDATA");

    // return the directive list
    return lines;
}

bool DataBlock::operator==(const DataBlock& other) const {
    // compare all fields for equality
    return name == other.name && parameters == other.parameters && records == other.records;
}

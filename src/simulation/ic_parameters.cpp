#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "../core/util.h"
#include "../netlist/netlist.h"
#include "ic_parameters.h"

IcEntry::IcEntry(std::string node, std::string voltage) :
    node(std::move(node)), voltage(std::move(voltage)) {}

bool IcEntry::operator==(const IcEntry& other) const {
    // compare all fields for equality
    return node == other.node && voltage == other.voltage;
}

namespace
{
    // add an entry keyed by node so a repeated node keeps its first position and takes the last value
    void upsert_entry(std::vector<IcEntry>& entries, std::string node, std::string voltage) {
        // build a case-insensitive key because Xyce uppercases node names when it parses .IC
        const auto key = to_upper(node);
        // search the collected entries for the same node ignoring case
        const auto existing = std::find_if(entries.begin(), entries.end(), [&key](const IcEntry& entry) { return to_upper(entry.node) == key; });
        // keep the position and take the last value when the node was already collected
        if (existing != entries.end()) {
            // update value
            existing->voltage = std::move(voltage);
            // exit
            return;
        }
        // append the entry otherwise so top-down directive order is preserved
        entries.emplace_back(std::move(node), std::move(voltage));
    }
} // namespace

ICParameters::ICParameters(std::vector<IcEntry> entries) :
    m_entries(std::move(entries)) {}

ICParameters ICParameters::from_xyce_directives(const std::vector<std::string>& directives) {
    // init the output entry list
    std::vector<IcEntry> entries;

    // scan every directive string for .IC or .DCVOLT
    for (const auto& directive : directives) {
        // tokenize the directive
        const auto tokens = tokenize(directive);

        // skip empty directives
        if (tokens.empty()) {
            continue;
        }

        // normalize the command token
        const std::string cmd = to_upper(tokens[0]);

        // handle only .IC and .DCVOLT (they use the same format)
        if (cmd != ".IC" && cmd != ".DCVOLT") {
            continue;
        }

        // iterate the remaining tokens looking for V(node)=val or node val pairs
        for (size_t i = 1; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            if (token.find('=') != std::string::npos) {
                // V(node)=val or node=val form
                const auto eq_pos = token.find('=');
                const auto lhs = token.substr(0, eq_pos);
                const auto voltage = token.substr(eq_pos + 1);
                std::string node;
                if (lhs.substr(0, 2) == "V(" && lhs.back() == ')') {
                    // V(node)=val form — strip the V(...) wrapper
                    node = lhs.substr(2, lhs.length() - 3);
                }
                else {
                    // bare node name form (e.g. node=val)
                    node = lhs;
                }
                upsert_entry(entries, std::string(node), std::string(voltage));
            }
            else if (i + 1 < tokens.size()) {
                // node val pair form (e.g. .IC out 1.0)
                upsert_entry(entries, std::string(token), std::string(tokens[i + 1]));
                ++i;
            }
        }
    }

    return ICParameters(std::move(entries));
}

ICParameters ICParameters::from_line(std::string_view line) {
    // tokenize the raw text to detect an already present command prefix
    const auto tokens = tokenize(line);

    // treat blank input as no initial conditions
    if (tokens.empty()) {
        return {};
    }

    // reuse the text as written when it already carries an .IC or .DCVOLT command
    const std::string command = to_upper(tokens[0]);
    const std::string statement = (command == ".IC" || command == ".DCVOLT") ? std::string(line) : ".IC " + std::string(line);

    // parse through the directive parser so the dedup rules apply to edited lines too
    auto parsed = from_xyce_directives({statement});

    // reject the line when an entry lost its node or value so unusable text never reaches the netlist
    const auto unusable = std::any_of(parsed.m_entries.begin(), parsed.m_entries.end(), [](const IcEntry& entry) { return entry.node.empty() || entry.voltage.empty(); });
    if (unusable) {
        return {};
    }

    // return the parsed entries otherwise
    return parsed;
}

std::vector<std::string> ICParameters::to_xyce_directives(const NetlistTopology& topology) const {
    // topology reserved for future wildcard expansion; pass-through for now
    (void)topology;

    // init the output directive list
    std::vector<std::string> directives;

    // serialize all entries as one merged .IC line so a repeated node cannot appear twice
    const auto line = to_line();
    if (!line.empty()) {
        directives.push_back(".IC " + line);
    }

    // return the single-line directive list
    return directives;
}

std::string ICParameters::to_line() const {
    // init the output line
    std::string line;

    // append every entry as a V(node)=value pair separated by single spaces
    for (const auto& entry : m_entries) {
        // separate the pairs with a single space
        if (!line.empty()) {
            line += ' ';
        }
        // always wrap the node in V(...) because that is the general form the Xyce reference guide accepts
        line += "V(" + entry.node + ")=" + entry.voltage;
    }

    // return the joined pairs
    return line;
}

bool ICParameters::operator==(const ICParameters& other) const {
    // compare all entries for equality
    return m_entries == other.m_entries;
}

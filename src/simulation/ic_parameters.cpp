#include <cctype>
#include <string>
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
                entries.emplace_back(std::string(node), std::string(voltage));
            }
            else if (i + 1 < tokens.size()) {
                // node val pair form (e.g. .IC out 1.0)
                entries.emplace_back(std::string(token), std::string(tokens[i + 1]));
                ++i;
            }
        }
    }

    return ICParameters(std::move(entries));
}

std::vector<std::string> ICParameters::to_xyce_directives(const NetlistTopology& topology) const {
    // topology reserved for future wildcard expansion; pass-through for now
    (void)topology;

    // serialize the entries as one .IC directive per entry
    std::vector<std::string> directives;
    directives.reserve(m_entries.size());

    for (const auto& entry : m_entries) {
        // format the node, wrapping in V(...) unless it is already a voltage node name
        std::string node = entry.node;
        if (node.empty() || (node[0] != 'V' && node[0] != 'v')) {
            node = "V(" + entry.node + ")";
        }
        // emit one directive per entry
        directives.push_back(".IC " + node + "=" + entry.voltage);
    }

    return directives;
}

bool ICParameters::operator==(const ICParameters& other) const {
    // compare all entries for equality
    return m_entries == other.m_entries;
}

#pragma once

#include <string>
#include <vector>

struct NetlistTopology;

// entry for an initial condition (.IC / .DCVOLT) directive
struct IcEntry
{
    // construct an IC entry from node and voltage
    IcEntry(std::string node, std::string voltage);

    // node name
    std::string node;
    // voltage value
    std::string voltage;

    // equality operator
    [[nodiscard]] bool operator==(const IcEntry& other) const;
};

// IC parameters class — parses and serializes Xyce .IC / .DCVOLT directives
// These set initial conditions for operating point calculations and are
// independent of the analysis type (.OP, .TRAN, .DC, etc.)
class ICParameters
{
public:
    // construct an empty IC parameters instance
    ICParameters() = default;

    // construct an IC parameters instance from individual entries
    explicit ICParameters(std::vector<IcEntry> entries);

    // parse all .IC / .DCVOLT directives into an ICParameters instance
    [[nodiscard]] static ICParameters from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // check whether this instance holds any entries
    [[nodiscard]] bool empty() const { return m_entries.empty(); }

    // access the underlying entries (read-only)
    [[nodiscard]] const std::vector<IcEntry>& entries() const { return m_entries; }

    // equality operator
    [[nodiscard]] bool operator==(const ICParameters& other) const;

private:
    // interior list of initial condition entries
    std::vector<IcEntry> m_entries;
};

#pragma once

#include <memory>
#include <optional>

#include "kicad_connection.h"
#include "netlist/netlist_source.h"

// bundles the KiCad API connection and the netlist source for plugin mode
class KiCadSession
{
public:
    // build a session from the environment, or nullopt when not running inside KiCad
    [[nodiscard]] static std::optional<KiCadSession> from_environment();

    // access the KiCad API connection
    [[nodiscard]] KiCadConnection& connection() { return *m_connection; }

    // take ownership of the netlist source built for the active project
    [[nodiscard]] std::unique_ptr<NetlistSource> take_netlist_source() { return std::move(m_netlist_source); }

private:
    KiCadSession(std::unique_ptr<KiCadConnection> connection, std::unique_ptr<NetlistSource> netlist_source);

    std::unique_ptr<KiCadConnection> m_connection;
    std::unique_ptr<NetlistSource> m_netlist_source;
};

#pragma once

#include <filesystem>
#include <memory>

#include "netlist/netlist_source.h"

class KiCadSession;

namespace studio
{
    // the schematic integration a shell binds, which a test substitutes without KiCad
    class SchematicSession
    {
    public:
        virtual ~SchematicSession() = default;

        // take the netlist source the shell reads the schematic through
        [[nodiscard]] virtual std::unique_ptr<NetlistSource> take_netlist_source() = 0;
    };

    // the live KiCad session, which owns the API connection and the CLI netlist exporter
    class KiCadSchematicSession final : public SchematicSession
    {
    public:
        explicit KiCadSchematicSession(std::unique_ptr<KiCadSession> session);

        ~KiCadSchematicSession() override;

        // take the netlist source the session built for the active project
        [[nodiscard]] std::unique_ptr<NetlistSource> take_netlist_source() override;

    private:
        // the session the plugin launch opened
        std::unique_ptr<KiCadSession> m_session;
    };

#ifdef XYCE_STUDIO_AUTOMATION
    // the schematic stub the automation build binds, which serves an exported netlist from disk
    class FileSchematicSession final : public SchematicSession
    {
    public:
        explicit FileSchematicSession(std::filesystem::path netlist);

        // take the read-only source the stub hands to the shell once
        [[nodiscard]] std::unique_ptr<NetlistSource> take_netlist_source() override;

    private:
        // the exported netlist the stub serves
        std::filesystem::path m_netlist;

        // whether the shell already took the source
        bool m_taken = false;
    };
#endif
} // namespace studio

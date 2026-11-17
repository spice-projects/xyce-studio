#include "app/schematic_session.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "kicad/kicad_session.h"

studio::KiCadSchematicSession::KiCadSchematicSession(std::unique_ptr<KiCadSession> session) :
    m_session(std::move(session)) {}

studio::KiCadSchematicSession::~KiCadSchematicSession() = default;

std::unique_ptr<NetlistSource> studio::KiCadSchematicSession::take_netlist_source() {
    // the session owns the one source it built for the project
    return m_session->take_netlist_source();
}

#ifdef XYCE_STUDIO_AUTOMATION
namespace
{
    // the read-only source the stub serves, which reads an exported netlist from disk
    class FileSchematicSource final : public NetlistSource
    {
    public:
        explicit FileSchematicSource(std::filesystem::path netlist) :
            m_netlist(std::move(netlist)) {}

        [[nodiscard]] std::string title() const override { return m_netlist.filename().string(); }

        [[nodiscard]] bool is_read_only() const override { return true; }

        [[nodiscard]] bool has_backing_file() const override { return false; }

        [[nodiscard]] std::filesystem::path working_directory() const override { return m_netlist.parent_path(); }

        [[nodiscard]] std::tuple<bool, std::string> load_netlist() override {
            // an export the shell cannot read fails the way the KiCad exporter does
            std::ifstream file(m_netlist);
            // a missing netlist refuses the load
            if (!file)
                throw std::runtime_error("the schematic netlist could not be read");
            // the export is the whole file
            std::ostringstream content;
            content << file.rdbuf();
            // the first load exports the netlist, later ones report the file unchanged like the KiCad exporter
            const bool reloaded = !m_exported;
            m_exported = true;
            return {reloaded, content.str()};
        }

        void save_netlist(const std::string&) override {}

    private:
        // the exported netlist the source serves
        std::filesystem::path m_netlist;

        // whether the source exported the netlist already
        bool m_exported = false;
    };
} // namespace

studio::FileSchematicSession::FileSchematicSession(std::filesystem::path netlist) :
    m_netlist(std::move(netlist)) {}

std::unique_ptr<NetlistSource> studio::FileSchematicSession::take_netlist_source() {
    // the shell takes the stub source once
    if (m_taken)
        return nullptr;
    // the source serves the exported netlist
    m_taken = true;
    return std::make_unique<FileSchematicSource>(m_netlist);
}
#endif

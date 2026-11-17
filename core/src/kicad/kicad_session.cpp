#include <memory>
#include <optional>
#include <string>

#include <spdlog/spdlog.h>

#include "core/util.h"
#include "kicad_cli_netlist_source.h"
#include "kicad_session.h"

namespace
{
    // name of the environment variable holding the active project directory
    constexpr const char* PROJECT_DIR_ENV = "KIPRJMOD";
} // namespace

std::optional<KiCadSession> KiCadSession::from_environment() {
    // the launch environment names plugin mode, so a standalone shell never reads the project variable
    if (!KiCadConnection::is_kicad_mode())
        return std::nullopt;
    // the project directory decides the schematic, which every plugin launch names
    const auto project_dir = get_environment_variable(PROJECT_DIR_ENV);
    if (!project_dir.has_value() || project_dir->empty()) {
        // plugin mode without a project cannot resolve the active schematic
        spdlog::error("{} environment variable is not set", PROJECT_DIR_ENV);
        // fail when the project directory is missing
        return std::nullopt;
    }
    try {
        // connect to the KiCad API server, which fails when the socket is gone
        auto connection = KiCadConnection::from_environment();
        // resolve the kicad-cli binary path through the API connection
        const std::string kicad_cli_path = connection->get_kicad_binary_path("kicad-cli");
        // build the CLI-based netlist source for the active project
        auto netlist_source = std::make_unique<KicadCliNetlistSource>(project_dir->c_str(), kicad_cli_path);
        // create the session
        return KiCadSession(std::move(connection), std::move(netlist_source));
    }
    catch (const std::exception& error) {
        // a stale socket or an unresolvable project degrades to the standalone app
        spdlog::error("KiCad plugin session could not be opened: {}", error.what());
        // fail when the plugin session cannot be built
        return std::nullopt;
    }
}

KiCadSession::KiCadSession(std::unique_ptr<KiCadConnection> connection, std::unique_ptr<NetlistSource> netlist_source) :
    m_connection(std::move(connection)), m_netlist_source(std::move(netlist_source)) {}

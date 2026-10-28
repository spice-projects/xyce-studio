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
    // connect to the KiCad API server
    auto connection = KiCadConnection::from_environment();
    // no session when the environment does not describe a KiCad plugin launch
    if (!connection)
        return std::nullopt;
    // read the project directory set by KiCad
    const auto project_dir = get_environment_variable(PROJECT_DIR_ENV);
    if (!project_dir.has_value() || project_dir->empty()) {
        // log the missing project directory
        spdlog::error("KIPRJMOD environment variable is not set");
        // fail when the project directory is missing
        return std::nullopt;
    }
    // resolve the kicad-cli binary path through the API connection
    std::string kicad_cli_path;
    try {
        // query the binary path from KiCad
        kicad_cli_path = connection->get_kicad_binary_path("kicad-cli");
    }
    catch (const std::exception& e) {
        // log the failure to resolve the binary path
        spdlog::error("Failed to resolve kicad-cli binary path: {}", e.what());
        // fail when the binary path could not be resolved
        return std::nullopt;
    }
    // build the CLI-based netlist source for the active project
    auto netlist_source = std::make_unique<KicadCliNetlistSource>(project_dir->c_str(), kicad_cli_path);
    // create the session
    return KiCadSession(std::move(connection), std::move(netlist_source));
}

KiCadSession::KiCadSession(std::unique_ptr<KiCadConnection> connection, std::unique_ptr<NetlistSource> netlist_source) :
    m_connection(std::move(connection)), m_netlist_source(std::move(netlist_source)) {}

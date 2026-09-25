#pragma once

#include <filesystem>
#include <string>
#include <tuple>

#include "../netlist/netlist_source.h"

// schematic netlist source that exports the netlist via the kicad-cli binary
class KicadCliNetlistSource : public NetlistSource
{
public:
    KicadCliNetlistSource(std::filesystem::path project_dir, std::string kicad_cli_path);

    [[nodiscard]] virtual std::string title() const override;

    [[nodiscard]] bool is_read_only() const override;

    [[nodiscard]] bool has_backing_file() const override;

    [[nodiscard]] virtual std::filesystem::path working_directory() const override;

    [[nodiscard]] std::tuple<bool, std::string> load_netlist() override;

    virtual void save_netlist(const std::string& content = "") override;

    // resolve the active schematic path inside a KiCad project directory
    [[nodiscard]] static std::filesystem::path resolve_schematic_path(const std::filesystem::path& project_dir);

private:
    std::filesystem::path m_project_dir;
    std::string m_kicad_cli_path;
    std::filesystem::path m_schematic_path;

    std::string m_cached_netlist;
    std::filesystem::file_time_type m_last_export_time{};
    bool m_has_exported = false;
};

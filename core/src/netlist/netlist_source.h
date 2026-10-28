#pragma once

#include <filesystem>
#include <string>
#include <tuple>

// abstract source of netlist content shown in the editor
class NetlistSource
{
public:
    virtual ~NetlistSource() = default;

    [[nodiscard]] virtual std::string title() const = 0;

    [[nodiscard]] virtual bool is_read_only() const = 0;

    [[nodiscard]] virtual bool has_backing_file() const = 0;

    [[nodiscard]] virtual std::filesystem::path working_directory() const = 0;

    [[nodiscard]] virtual std::tuple<bool, std::string> load_netlist() = 0;

    virtual void save_netlist(const std::string& content = "") = 0;
};

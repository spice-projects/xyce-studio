#pragma once

#include <filesystem>
#include <string>
#include <tuple>

#include "netlist_source.h"

class FileNetlistSource : public NetlistSource
{
public:
    explicit FileNetlistSource(std::filesystem::path file_path);

    [[nodiscard]] virtual std::string title() const override;

    [[nodiscard]] bool is_read_only() const override;

    [[nodiscard]] bool has_backing_file() const override;

    [[nodiscard]] virtual std::filesystem::path working_directory() const override;

    [[nodiscard]] std::tuple<bool, std::string> load_netlist() override;

    virtual void save_netlist(const std::string& content = "") override;

private:
    std::filesystem::path m_file_path;
};

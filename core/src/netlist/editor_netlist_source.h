#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <tuple>

#include "file_netlist_source.h"
#include "netlist_source.h"

// netlist source backed by a file, but reporting the live editor text after the first load
class EditorNetlistSource : public NetlistSource
{
public:
    EditorNetlistSource(std::function<std::string()> get_editor_text, std::filesystem::path file_path);

    [[nodiscard]] std::string title() const override;

    [[nodiscard]] bool is_read_only() const override;

    [[nodiscard]] bool has_backing_file() const override;

    [[nodiscard]] std::filesystem::path working_directory() const override;

    [[nodiscard]] std::tuple<bool, std::string> load_netlist() override;

    void save_netlist(const std::string& content = "") override;

private:
    std::function<std::string()> m_get_editor_text;
    std::unique_ptr<FileNetlistSource> m_file_source;
    bool m_initialized = false;
};

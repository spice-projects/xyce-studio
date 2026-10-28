#pragma once

#include <string>

class PluginConfig
{
public:
    // construct default configuration
    PluginConfig() = default;

    // construct configuration with the given Xyce executable path
    explicit PluginConfig(std::string xyce_executable_path);

    // load configuration from disk, falling back to discovery
    [[nodiscard]] static PluginConfig load();

    // construct default configuration using discovered executable
    [[nodiscard]] static PluginConfig default_config();

    // persist configuration to disk
    void save() const;

    // check if the configured path points to a valid executable file
    [[nodiscard]] bool is_xyce_executable_valid() const;

    // get the configured Xyce executable path
    [[nodiscard]] const std::string& xyce_executable_path() const { return m_xyce_executable_path; }

    // set the Xyce executable path
    void set_xyce_executable_path(std::string path) { m_xyce_executable_path = std::move(path); }

    // search standard directories and PATH for a Xyce executable
    [[nodiscard]] static std::string discover_xyce_executable();

private:
    std::string m_xyce_executable_path;
};

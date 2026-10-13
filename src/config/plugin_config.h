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

    // check that a parallel run could actually be launched, meaning the launcher is executable and enough processes were requested
    [[nodiscard]] bool is_parallel_configuration_valid() const;

    // get the configured Xyce executable path
    [[nodiscard]] const std::string& xyce_executable_path() const { return m_xyce_executable_path; }

    // set the Xyce executable path
    void set_xyce_executable_path(std::string path) { m_xyce_executable_path = std::move(path); }

    // report whether the configured simulation runs through an MPI launcher
    [[nodiscard]] bool parallel_enabled() const { return m_parallel_enabled; }

    // turn the parallel run on or off
    void set_parallel_enabled(bool enabled) { m_parallel_enabled = enabled; }

    // get the configured MPI launcher path
    [[nodiscard]] const std::string& mpi_launcher_path() const { return m_mpi_launcher_path; }

    // set the MPI launcher path
    void set_mpi_launcher_path(std::string path) { m_mpi_launcher_path = std::move(path); }

    // get the configured number of parallel processes
    [[nodiscard]] unsigned process_count() const { return m_process_count; }

    // set the number of parallel processes
    void set_process_count(unsigned count) { m_process_count = count; }

    // get the additional arguments inserted between the launcher and Xyce
    [[nodiscard]] const std::string& mpi_launcher_arguments() const { return m_mpi_launcher_arguments; }

    // set the additional arguments inserted between the launcher and Xyce
    void set_mpi_launcher_arguments(std::string arguments) { m_mpi_launcher_arguments = std::move(arguments); }

    // search standard directories and PATH for a Xyce executable
    [[nodiscard]] static std::string discover_xyce_executable();

private:
    std::string m_xyce_executable_path;
    bool m_parallel_enabled = false;
    std::string m_mpi_launcher_path;
    unsigned m_process_count = 1;
    std::string m_mpi_launcher_arguments;
};

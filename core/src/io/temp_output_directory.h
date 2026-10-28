#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

// own the temporary directory a simulation run writes its outputs into; the directory is removed once the last shared reference is released, unless a user-owned file or a failed destination copy pinned it
class TempOutputDirectory
{
public:
    // create a unique temporary run directory, null when it cannot be created
    [[nodiscard]] static std::shared_ptr<TempOutputDirectory> create();

    // remove the run directory unless a user-owned file or a failed copy pinned it
    ~TempOutputDirectory();

    TempOutputDirectory(const TempOutputDirectory&) = delete;

    TempOutputDirectory& operator=(const TempOutputDirectory&) = delete;

    // the run directory path
    [[nodiscard]] const std::filesystem::path& path() const;

    // the netlist file inside the run directory
    [[nodiscard]] std::filesystem::path netlist_path() const;

    // write the netlist text into the run directory, false when the file cannot be written
    [[nodiscard]] bool write_netlist(std::string_view text) const;

    // true when the given path lies inside this run directory
    [[nodiscard]] bool owns(const std::filesystem::path& path) const;

    // keep this run directory for the rest of the process
    void pin();

    // pin the active run directory containing the given explicitly opened user file
    static void pin_path(const std::filesystem::path& path);

private:
    // create the object for an already created directory
    explicit TempOutputDirectory(std::filesystem::path path);

    // the run directory path
    std::filesystem::path m_path;
    // true when a user explicitly opened a file inside this directory
    bool m_pinned = false;
};

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// outcome of asking a Xyce binary what it was built with
struct XyceCapabilities
{
    // the binary was built with MPI support and can be launched through an MPI launcher
    bool is_parallel;
    // the capability banner line the classification was taken from
    std::string capability_line;
};

// process count used when the launcher does not report how many slots it can offer
inline constexpr unsigned DEFAULT_PROCESS_COUNT = 4;

// parse the capability banner of `Xyce -capabilities`; only the first non-blank line classifies the binary, because the run banner of every Xyce build also contains the word parallel
[[nodiscard]] std::optional<XyceCapabilities> parse_xyce_capabilities(std::string_view banner);

// parse the slot count out of the job map an Open MPI launcher prints for `mpirun --display map`
[[nodiscard]] std::optional<unsigned> parse_launcher_slot_count(std::string_view job_map);

// ask the Xyce binary what it was built with
[[nodiscard]] std::optional<XyceCapabilities> probe_xyce_capabilities(const std::filesystem::path& xyce_executable);

// ask an MPI launcher how many process slots it can offer, which can be far below the logical processor count
[[nodiscard]] std::optional<unsigned> probe_launcher_slot_count(const std::filesystem::path& mpi_launcher, const std::filesystem::path& xyce_executable);

// ask an MPI launcher to identify itself
[[nodiscard]] std::optional<std::string> probe_launcher_version(const std::filesystem::path& mpi_launcher);

// search PATH for an MPI launcher, preferring mpirun over its mpiexec alias
[[nodiscard]] std::string discover_mpi_launcher();

// resolve the process count a parallel run should use, clamped to the slot count the launcher reported
[[nodiscard]] unsigned clamp_process_count(unsigned requested, std::optional<unsigned> available_slots);

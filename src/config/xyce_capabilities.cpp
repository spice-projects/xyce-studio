#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

#include "../core/executable_probe.h"
#include "../core/util.h"
#include "xyce_capabilities.h"

namespace
{
    // the flag a Xyce binary answers with its build capabilities
    constexpr std::string_view CAPABILITIES_FLAG = "-capabilities";

    // the prefix of the capability banner line a parallel build reports
    constexpr std::string_view PARALLEL_PREFIX = "parallel";

    // the Open MPI flag that prints the job map carrying the slot count, passed as two arguments because the launcher matches whole arguments
    constexpr std::string_view DISPLAY_MAP_FLAG = "--display";
    constexpr std::string_view DISPLAY_MAP_VALUE = "map";

    // the marker the job map uses to report the allocated slots
    constexpr std::string_view SLOTS_MARKER = "Total slots allocated ";

    // the launcher names searched on PATH, in preference order
    constexpr std::array<std::string_view, 2> LAUNCHER_NAMES{"mpirun", "mpiexec"};

    // search every PATH directory for an executable carrying the given name
    std::string search_path_for_executable(std::string_view name) {
        // query the PATH environment variable
        const auto path_env = get_environment_variable("PATH");
        // nothing to search without PATH
        if (!path_env.has_value() || path_env->empty())
            return {};
#if defined(_WIN32)
        // Windows uses semicolon as path separator and the executable carries an .exe suffix
        constexpr char separator = ';';
        constexpr std::string_view suffix = ".exe";
#else
        // POSIX uses colon as path separator and the executable carries no suffix
        constexpr char separator = ':';
        constexpr std::string_view suffix = "";
#endif
        // split PATH entries by separator
        const auto directories = split_by(*path_env, separator);
        // iterate over each directory in PATH
        for (const auto directory : directories) {
            // skip an empty PATH entry, it means the current directory and is never a launcher location
            if (directory.empty())
                continue;
            // the candidate executable inside this directory
            const auto candidate = std::filesystem::path(directory) / (std::string(name) + std::string(suffix));
            std::error_code ec;
            // the candidate has to be a regular file
            if (!std::filesystem::is_regular_file(candidate, ec))
                continue;
            ec.clear();
#if defined(_WIN32)
            // a regular file is an executable on windows
            return candidate.string();
#else
            // the candidate also needs the executable permission bit
            if (::access(candidate.c_str(), X_OK) == 0)
                return candidate.string();
#endif
        }
        // no PATH directory carried the name
        return {};
    }
} // namespace

std::optional<XyceCapabilities> parse_xyce_capabilities(std::string_view banner) {
    // the capability line the classification was taken from
    std::string capability_line;
    // the offset of the first non-blank line
    size_t start = 0;
    while (start < banner.size()) {
        // the extent of the current line
        const auto line_end = banner.find('\n', start);
        const auto extent = line_end == std::string_view::npos ? banner.substr(start) : banner.substr(start, line_end - start);
        // trim the carriage return a windows launcher leaves at the end of the line
        auto candidate = trim(extent);
        // the first non-blank line is the one a build reports itself with
        if (!candidate.empty()) {
            capability_line = std::move(candidate);
            break;
        }
        // the line was blank, look at the next one
        if (line_end == std::string_view::npos)
            break;
        start = line_end + 1;
    }
    // a banner without a single non-blank line carries no classification
    if (capability_line.empty())
        return std::nullopt;
    // only the capability line classifies the binary, a run banner also contains the word parallel
    return XyceCapabilities{probe_text_first_line_starts_with(capability_line, PARALLEL_PREFIX), std::move(capability_line)};
}

std::optional<unsigned> parse_launcher_slot_count(std::string_view job_map) {
    // an empty job map carries no slot count
    if (job_map.empty())
        return std::nullopt;
    // search the capture for the slot count marker
    const auto marker = job_map.find(SLOTS_MARKER);
    // the launcher did not report a slot count
    if (marker == std::string_view::npos)
        return std::nullopt;
    // the offset of the digits that follow the marker
    const auto digits_start = marker + SLOTS_MARKER.size();
    // the offset of the first character that cannot be part of the number
    const auto digits_end = job_map.find_first_not_of("0123456789", digits_start);
    // the slot count occupies the rest of the capture when no delimiter follows it
    const auto digits = job_map.substr(digits_start, digits_end == std::string_view::npos ? std::string_view::npos : digits_end - digits_start);
    // the launcher reported a marker without a usable number
    if (digits.empty())
        return std::nullopt;
    // the parsed slot count
    unsigned slots = 0;
    // convert the digits, a value the platform cannot represent is rejected
    const auto converted = std::from_chars(digits.data(), digits.data() + digits.size(), slots);
    // reject a partial conversion and a value the unsigned type could not hold
    if (converted.ec != std::errc{} || converted.ptr != digits.data() + digits.size())
        return std::nullopt;
    return slots;
}

std::optional<XyceCapabilities> probe_xyce_capabilities(const std::filesystem::path& xyce_executable) {
    // nothing to probe without an executable
    if (xyce_executable.empty())
        return std::nullopt;
    // ask the binary what it was built with, it answers on standard output and exits immediately
    const auto probe = run_executable_probe(xyce_executable.string(), {std::string(CAPABILITIES_FLAG)});
    // a binary that could not run, timed out, or failed reports no capability
    if (!probe.exit_code.has_value() || *probe.exit_code != 0) {
        spdlog::debug("Xyce capability probe failed for {}", xyce_executable.string());
        return std::nullopt;
    }
    // classify the build from the reported banner
    auto capabilities = parse_xyce_capabilities(probe.standard_output);
    // report a parse failure the same way the caller reports an unknown binary
    if (!capabilities.has_value()) {
        spdlog::debug("Xyce capability probe returned no banner for {}", xyce_executable.string());
        return std::nullopt;
    }
    spdlog::debug("Xyce capability probe for {} reported: {}", xyce_executable.string(), capabilities->capability_line);
    return capabilities;
}

std::optional<unsigned> probe_launcher_slot_count(const std::filesystem::path& mpi_launcher, const std::filesystem::path& xyce_executable) {
    // nothing to probe without a launcher
    if (mpi_launcher.empty())
        return std::nullopt;
    // a launcher that does not accept the job map flag still reports through a single-rank run, so the count is taken from the capture either way
    const auto probe = run_executable_probe(mpi_launcher.string(), {std::string(DISPLAY_MAP_FLAG), std::string(DISPLAY_MAP_VALUE), "-np", "1", xyce_executable.string(), std::string(CAPABILITIES_FLAG)});
    // the launcher printed its job map on standard error
    const auto from_stderr = parse_launcher_slot_count(probe.standard_error);
    // some launchers print the job map on standard output instead
    if (from_stderr.has_value())
        return from_stderr;
    return parse_launcher_slot_count(probe.standard_output);
}

std::optional<std::string> probe_launcher_version(const std::filesystem::path& mpi_launcher) {
    // nothing to probe without a launcher
    if (mpi_launcher.empty())
        return std::nullopt;
    // ask the launcher to identify itself
    const auto probe = run_executable_probe(mpi_launcher.string(), {"--version"});
    // a launcher that could not run, timed out, or failed reports no version
    if (!probe.exit_code.has_value() || *probe.exit_code != 0)
        return std::nullopt;
    // the launcher identifies itself on standard output, some print the banner on standard error
    const auto banner = probe.standard_output.empty() ? probe.standard_error : probe.standard_output;
    // nothing was captured at all
    if (banner.empty())
        return std::nullopt;
    // the first non-blank line is the launcher's own name and version
    const auto line_end = banner.find('\n');
    return trim(line_end == std::string::npos ? banner : banner.substr(0, line_end));
}

std::string discover_mpi_launcher() {
    // try each known launcher name in preference order, mpirun is the documented spelling and mpiexec its Open MPI alias
    for (const auto name : LAUNCHER_NAMES) {
        // search PATH for this launcher
        const auto found = search_path_for_executable(name);
        // keep looking when this name is not installed
        if (found.empty())
            continue;
        spdlog::debug("Found MPI launcher {} on PATH: {}", name, found);
        return found;
    }
    spdlog::debug("MPI launcher not found via discovery");
    return {};
}

unsigned clamp_process_count(unsigned requested, std::optional<unsigned> available_slots) {
    // a run needs at least two processes to be a parallel run at all
    const auto floor = 2u;
    // the launcher reported no usable slot count, so the requested count stands
    if (!available_slots.has_value())
        return std::max(requested, floor);
    // the launcher offered fewer slots than a parallel run needs, fall back to the launcher limit
    if (*available_slots < floor)
        return *available_slots;
    // never exceed the slots the launcher actually offers, an oversized request fails with a not enough slots error
    return std::min(std::max(requested, floor), *available_slots);
}

#include <cstddef>
#include <optional>
#include <string>

#include "parallel_configuration.h"
#include "plugin_config.h"
#include "xyce_capabilities.h"

namespace
{
    // capability line shown while the probe is still in flight
    constexpr const char* PROBING_TEXT = "Detecting Xyce build capabilities...";

    // capability line shown for a binary that reports MPI support, carrying the line it reported
    constexpr const char* PARALLEL_TEXT_PREFIX = "Parallel support available: ";

    // capability line shown for a binary without MPI support
    constexpr const char* SERIAL_TEXT = "This Xyce build reports no MPI support, so it cannot run in parallel";

    // capability line shown when the binary could not be classified
    constexpr const char* PROBE_FAILED_TEXT = "Could not read the build capabilities of this Xyce binary";
} // namespace

std::string trimmed_or_empty(const std::string& value) {
    // the first character that is not whitespace
    const auto first = value.find_first_not_of(" \t\r\n");
    // the value holds nothing but whitespace
    if (first == std::string::npos)
        return {};
    // the last character that is not whitespace
    const auto last = value.find_last_not_of(" \t\r\n");
    // the trimmed value
    return value.substr(first, last - first + 1);
}

CapabilityPanelState describe_capabilities(const std::optional<XyceCapabilities>& capabilities, bool parallel_requested) {
    // a binary that could not be classified offers no parallel section
    if (!capabilities.has_value())
        return CapabilityPanelState{PROBE_FAILED_TEXT, false, true, false};
    // a serial build can never be launched through an MPI launcher
    if (!capabilities->is_parallel)
        return CapabilityPanelState{SERIAL_TEXT, false, false, false};
    // a parallel build keeps the request and shows the line it reported
    return CapabilityPanelState{std::string(PARALLEL_TEXT_PREFIX) + capabilities->capability_line, true, false, parallel_requested};
}

CapabilityPanelState describe_pending_capabilities(bool parallel_requested) { return CapabilityPanelState{PROBING_TEXT, false, false, parallel_requested}; }

ParallelFormRejection build_form_config(const std::string& xyce_path, bool parallel_requested, bool parallel_capable, const std::string& launcher_path, int process_count, bool slots_known, int available_slots, const std::string& launcher_arguments, PluginConfig& config) {
    // a run needs an Xyce binary to launch at all
    const auto trimmed_xyce_path = trimmed_or_empty(xyce_path);
    if (trimmed_xyce_path.empty())
        return ParallelFormRejection::XyceMissing;
    // the configuration starts from the validated path
    config = PluginConfig(trimmed_xyce_path);
    // a parallel request only survives for a binary that reports MPI support
    config.set_parallel_enabled(parallel_requested && parallel_capable);
    // the launcher path and the additional arguments are stored trimmed
    config.set_mpi_launcher_path(trimmed_or_empty(launcher_path));
    config.set_mpi_launcher_arguments(trimmed_or_empty(launcher_arguments));
    // the panel reports a signed spinner value, a non-positive count is not a parallel run
    const auto requested = process_count > 0 ? static_cast<unsigned>(process_count) : 0u;
    // clamp to the slots the launcher reported, which can be far below the logical processor count
    config.set_process_count(clamp_process_count(requested, slots_known ? std::optional<unsigned>(available_slots > 0 ? static_cast<unsigned>(available_slots) : 0u) : std::nullopt));
    // reject a path that is not an executable file
    if (!config.is_xyce_executable_valid())
        return ParallelFormRejection::XyceInvalid;
    // a serial run has no launcher to validate
    if (!config.parallel_enabled())
        return ParallelFormRejection::None;
    // a parallel run cannot start without a launcher
    if (config.mpi_launcher_path().empty())
        return ParallelFormRejection::LauncherMissing;
    // a launcher that answered the slot probe already proved it is executable, so
    // a reported slot count below the parallel floor is the real cause and no
    // process count can repair it
    if (slots_known && available_slots < 2)
        return ParallelFormRejection::LauncherTooFewSlots;
    // a launcher that cannot be executed, or fewer than two processes, cannot start a parallel run
    if (!config.is_parallel_configuration_valid())
        return ParallelFormRejection::LauncherInvalid;
    return ParallelFormRejection::None;
}

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "plugin_config.h"
#include "xyce_capabilities.h"

// what the configuration panel has to show after a capability probe completed
struct CapabilityPanelState
{
    // the capability line the panel displays
    std::string text;
    // the binary reports MPI support, so the parallel section is shown
    bool parallel_capable;
    // the probe could not classify the binary at all
    bool capabilities_unknown;
    // a parallel run stays requested only for a parallel-capable binary
    bool parallel_enabled;
};

// the reason a submitted configuration form was rejected
enum class ParallelFormRejection
{
    // the submitted form describes a usable configuration
    None,
    // no Xyce path was given at all
    XyceMissing,
    // the Xyce path is not an executable file
    XyceInvalid,
    // a parallel run was requested without an MPI launcher path
    LauncherMissing,
    // a parallel run was requested through a launcher that cannot be executed
    LauncherInvalid,
    // the launcher reported fewer slots than a parallel run needs
    LauncherTooFewSlots
};

// trim surrounding whitespace, returning an empty string when the value holds nothing but whitespace
[[nodiscard]] std::string trimmed_or_empty(const std::string& value);

// describe what the panel should show for a completed capability probe, dropping a parallel request when the binary turned out to be serial
[[nodiscard]] CapabilityPanelState describe_capabilities(const std::optional<XyceCapabilities>& capabilities, bool parallel_requested);

// describe what the panel should show while a capability probe is still in flight;
// the pending round has classified nothing yet, so it reports neither capability
// and carries the requested parallel run through untouched instead of clearing it
[[nodiscard]] CapabilityPanelState describe_pending_capabilities(bool parallel_requested);

// build the configuration a submitted form describes, with the process count clamped to the slots the launcher reported, and report why the form was rejected when it cannot be used
[[nodiscard]] ParallelFormRejection build_form_config(const std::string& xyce_path, bool parallel_requested, bool parallel_capable, const std::string& launcher_path, int process_count, bool slots_known, int available_slots, const std::string& launcher_arguments, PluginConfig& config);

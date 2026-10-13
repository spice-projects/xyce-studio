#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "plugin_config.h"
#include "xyce_capabilities.h"

// what a completed capability detection round observed
struct CapabilityProbeOutcome
{
    // the generation this outcome reports under, so a stale round can be discarded
    unsigned generation;
    // what the binary reported it was built with, empty when it could not be classified
    std::optional<XyceCapabilities> capabilities;
    // the process slots the launcher offered, empty when it reported none
    std::optional<unsigned> available_slots;
    // the launcher's own identification, empty when it could not identify itself
    std::optional<std::string> launcher_version;
};

// runs the Xyce and MPI launcher probes off the calling thread so a slow binary
// never blocks the ui. every round supersedes the previous one: a round whose
// result arrives after its path was replaced is discarded rather than reported,
// and the destructor joins whatever is still running so no thread outlives it.
class CapabilityProbeRunner
{
public:
    // delivers each completed round on the thread that started it, which is the
    // ui thread for the configuration dialog
    using OutcomeCallback = std::function<void(const CapabilityProbeOutcome&)>;

    CapabilityProbeRunner();

    ~CapabilityProbeRunner();

    CapabilityProbeRunner(const CapabilityProbeRunner&) = delete;

    CapabilityProbeRunner& operator=(const CapabilityProbeRunner&) = delete;

    // set the callback receiving completed rounds, replacing any previous one
    void set_outcome_callback(OutcomeCallback callback);

    // start detecting, superseding any round still in flight; a round for an
    // empty binary path reports nothing and only publishes the pending state
    void start(const std::string& xyce_executable_path, const std::string& mpi_launcher_path);

    // publish the pending state without probing anything, used for an empty path
    void announce_pending();

    // abandon the round in flight, so its result can no longer be reported
    void cancel();

    // the generation of the round currently in flight
    [[nodiscard]] unsigned generation() const;

private:
    // the callback completed rounds are delivered through
    OutcomeCallback m_callback;
    // the threads of the rounds that have not been joined yet
    std::vector<std::thread> m_threads;
    // incremented for every round, identifying which result is current
    std::atomic<unsigned> m_generation{0};
};

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "capability_probe_runner.h"
#include "xyce_capabilities.h"

CapabilityProbeRunner::CapabilityProbeRunner() = default;

CapabilityProbeRunner::~CapabilityProbeRunner() {
    // supersede whatever is in flight so no late result is delivered during teardown
    cancel();
}

void CapabilityProbeRunner::set_outcome_callback(OutcomeCallback callback) { m_callback = std::move(callback); }

unsigned CapabilityProbeRunner::generation() const { return m_generation.load(); }

void CapabilityProbeRunner::cancel() {
    // invalidate the round in flight, so its result is discarded when it arrives
    m_generation.fetch_add(1);
    // the threads of the rounds that are being superseded
    auto threads = std::move(m_threads);
    // drop them before joining, so a reentrant call finds nothing to join
    m_threads.clear();
    // join every one of them, so no probe thread outlives the runner
    for (auto& thread : threads) {
        // a thread only ever waits on a probe it started itself
        if (thread.joinable())
            thread.join();
    }
}

void CapabilityProbeRunner::announce_pending() {
    // the pending round has no binary to probe, so nothing is started
    cancel();
}

void CapabilityProbeRunner::start(const std::string& xyce_executable_path, const std::string& mpi_launcher_path) {
    // supersede any round still in flight for a previous path
    cancel();
    // the generation this round reports under
    const auto generation = m_generation.load();
    // the callback the completed round is delivered through
    const auto callback = m_callback;
    // nothing to probe without a binary
    if (xyce_executable_path.empty())
        return;
    m_threads.emplace_back([generation, callback, xyce_executable_path, mpi_launcher_path] {
        // ask the binary what it was built with
        const auto capabilities = probe_xyce_capabilities(std::filesystem::path(xyce_executable_path));
        // the slot count only matters for a binary built with MPI support
        const auto slots = capabilities.has_value() && capabilities->is_parallel ? probe_launcher_slot_count(std::filesystem::path(mpi_launcher_path), std::filesystem::path(xyce_executable_path)) : std::nullopt;
        // the launcher identifies itself only when a path is configured
        const auto version = mpi_launcher_path.empty() ? std::nullopt : probe_launcher_version(std::filesystem::path(mpi_launcher_path));
        // report the round through the callback captured when it was started
        if (callback)
            callback(CapabilityProbeOutcome{generation, capabilities, slots, version});
    });
}

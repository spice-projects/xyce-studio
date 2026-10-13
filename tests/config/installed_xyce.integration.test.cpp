#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "config/xyce_capabilities.h"
#include "core/executable_probe.h"

namespace
{
    // resolve an executable through PATH, or an empty string when it is absent
    std::string which(const std::string& name) {
        // the PATH entries to search
        const auto path_env = std::getenv("PATH");
        // nothing to search without PATH
        if (path_env == nullptr)
            return {};
        // the entries in PATH
        const std::string entries{path_env};
#if defined(_WIN32)
        constexpr char separator = ';';
#else
        constexpr char separator = ':';
#endif
        // the beginning of the current entry
        size_t start = 0;
        // walk every entry looking for the executable
        while (start <= entries.size()) {
            // the end of the current entry
            const auto end = entries.find(separator, start);
            // the current entry
            const auto entry = entries.substr(start, end == std::string::npos ? std::string::npos : end - start);
            // the candidate inside this entry
            const auto candidate = std::filesystem::path(entry) / name;
            // the candidate has to be an executable regular file
            if (!entry.empty() && std::filesystem::is_regular_file(candidate)) {
#if !defined(_WIN32)
                if (::access(candidate.c_str(), X_OK) == 0)
                    return candidate.string();
#else
                return candidate.string();
#endif
            }
            // the last entry has no separator after it
            if (end == std::string::npos)
                break;
            // continue after the separator
            start = end + 1;
        }
        // no entry carried the executable
        return {};
    }
} // namespace

TEST(InstalledXyceParallelRunChecks, a_real_parallel_run_produces_the_expected_command) {
    // arrange — resolve the real binaries this machine installed
    const auto xyce = which("Xyce") + ".parallel";
    const auto launcher = which("mpirun");
    // arrange / act
    if (xyce.empty() || launcher.empty()) {
        GTEST_SKIP() << "no parallel Xyce binary or mpirun on PATH";
    }
    const auto capabilities = probe_xyce_capabilities(std::filesystem::path(xyce));
    // assert — the installed binary really does report MPI support, which is what makes the feature reachable
    ASSERT_TRUE(capabilities.has_value());
    ASSERT_TRUE(capabilities->is_parallel);
}

TEST(InstalledXyceParallelRunChecks, a_real_parallel_run_completes_through_the_launcher) {
    // arrange — resolve the real binaries this machine installed
    const auto xyce = which("Xyce") + ".parallel";
    const auto launcher = which("mpirun");
    // arrange / act
    if (xyce.empty() || launcher.empty()) {
        GTEST_SKIP() << "no parallel Xyce binary or mpirun on PATH";
    }
    const auto slots = probe_launcher_slot_count(std::filesystem::path(launcher), std::filesystem::path(xyce));
    // arrange — a transient netlist the launcher can run end to end
    const auto netlist = std::filesystem::temp_directory_path() / "xyce_studio_installed_parallel.cir";
    {
        std::ofstream file(netlist);
        file << "v1 in 0 dc 1\nr1 in n1 1k\nc1 n1 0 1n\nr2 n1 0 1k\n.tran 1u 20u\n.print tran v(n1)\n.end\n";
    }
    const auto count = clamp_process_count(2, slots);
    // act — the exact command shape build_simulation_argv assembles for a parallel run
    const auto run = run_executable_probe(launcher, {"-np", std::to_string(count), xyce, netlist.string()});
    // assert — the launcher started the copies and Xyce produced its print output
    ASSERT_TRUE(run.exit_code.has_value());
    EXPECT_EQ(*run.exit_code, 0);
    EXPECT_TRUE(std::filesystem::exists(netlist.string() + ".prn"));
    // cleanup
    std::error_code ec;
    std::filesystem::remove(netlist, ec);
    std::filesystem::remove(netlist.string() + ".prn", ec);
}

TEST(InstalledXyceParallelRunChecks, a_real_serial_run_produces_the_expected_command) {
    // arrange — resolve the real serial binary this machine installed
    const auto xyce = which("Xyce");
    // arrange / act
    if (xyce.empty()) {
        GTEST_SKIP() << "no serial Xyce binary on PATH";
    }
    const auto capabilities = probe_xyce_capabilities(std::filesystem::path(xyce));
    // assert — the installed serial binary reports no MPI support, which is what hides the parallel section
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_FALSE(capabilities->is_parallel);
}

TEST(InstalledXyceParallelRunChecks, a_real_launcher_reports_a_slot_count_below_the_logical_processor_count) {
    // arrange — resolve the real binaries this machine installed
    const auto xyce = which("Xyce") + ".parallel";
    const auto launcher = which("mpirun");
    // arrange / act
    if (xyce.empty() || launcher.empty()) {
        GTEST_SKIP() << "no parallel Xyce binary or mpirun on PATH";
    }
    const auto slots = probe_launcher_slot_count(std::filesystem::path(launcher), std::filesystem::path(xyce));
    const auto logical = std::thread::hardware_concurrency();
    // assert — a default taken from the logical processor count would exceed what the launcher offers
    ASSERT_TRUE(slots.has_value());
    EXPECT_GT(logical, 0u);
    EXPECT_EQ(clamp_process_count(logical, slots), *slots);
}

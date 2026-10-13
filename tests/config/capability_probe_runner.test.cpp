#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include <gtest/gtest.h>

#include "config/capability_probe_runner.h"
#include "config/xyce_capabilities.h"

namespace
{
    // write an executable stand-in script and return its path
    std::filesystem::path write_script(const std::string& name, const std::string& body) {
        // the scripts live in a private directory so discovery never sees them on PATH
        const auto directory = std::filesystem::temp_directory_path() / "xyce_studio_runner_probe";
        std::filesystem::create_directories(directory);
        // the script itself
        const auto script = directory / name;
        std::ofstream file(script);
        file << body;
        file.close();
#if !defined(_WIN32)
        ::chmod(script.c_str(), 0755);
#endif
        return script;
    }

    // a stand-in binary reporting MPI support
    std::filesystem::path parallel_xyce() {
        // the stand-in answers the capability flag and stays silent otherwise
        return write_script("runner_xyce.sh", "#!/bin/sh\nif [ \"$1\" = \"-capabilities\" ]; then echo 'Parallel with MPI'; exit 0; fi\nexit 0\n");
    }

    // a stand-in binary reporting no MPI support
    std::filesystem::path serial_xyce() {
        // the stand-in answers the capability flag the way a serial build does
        return write_script("runner_serial_xyce.sh", "#!/bin/sh\nif [ \"$1\" = \"-capabilities\" ]; then echo 'Serial'; exit 0; fi\nexit 0\n");
    }

    // a stand-in launcher reporting six slots and identifying itself as Open MPI
    std::filesystem::path mpi_launcher() {
        // the stand-in answers both the job map and the version flag
        return write_script("runner_mpirun.sh", "#!/bin/sh\nif [ \"$1\" = \"--version\" ]; then echo 'mpirun (Open MPI) 5.0.11'; exit 0; fi\necho 'Data for JOB offset 0 Total slots allocated 6' 1>&2\nexit 0\n");
    }

    // remove the private stand-in directory
    void remove_scripts() {
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::temp_directory_path() / "xyce_studio_runner_probe", ec);
    }

    // wait for the predicate to hold while the probe round runs on its own thread
    void wait_until(const std::function<bool()>& predicate, int milliseconds = 20000) {
        // the deadline for the wait
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        // wait for the predicate
        while (std::chrono::steady_clock::now() < deadline) {
            // the condition already holds
            if (predicate())
                return;
            // let the probe thread make progress
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
} // namespace

TEST(CapabilityProbeRunnerChecks, reports_a_parallel_capable_binary_with_the_launcher_details) {
    // arrange
    CapabilityProbeRunner runner;
    std::optional<CapabilityProbeOutcome> received;
    runner.set_outcome_callback([&](const CapabilityProbeOutcome& outcome) { received = outcome; });
    // act
    runner.start(parallel_xyce().string(), mpi_launcher().string());
    wait_until([&] { return received.has_value(); });
    // assert
    ASSERT_TRUE(received.has_value());
    ASSERT_TRUE(received->capabilities.has_value());
    EXPECT_TRUE(received->capabilities->is_parallel);
    EXPECT_EQ(received->capabilities->capability_line, "Parallel with MPI");
    ASSERT_TRUE(received->available_slots.has_value());
    EXPECT_EQ(*received->available_slots, 6u);
    ASSERT_TRUE(received->launcher_version.has_value());
    EXPECT_NE(received->launcher_version->find("Open MPI"), std::string::npos);
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, reports_no_slots_for_a_serial_binary) {
    // arrange — a serial build has no use for a launcher slot count
    CapabilityProbeRunner runner;
    std::optional<CapabilityProbeOutcome> received;
    runner.set_outcome_callback([&](const CapabilityProbeOutcome& outcome) { received = outcome; });
    // act
    runner.start(serial_xyce().string(), mpi_launcher().string());
    wait_until([&] { return received.has_value(); });
    // assert
    ASSERT_TRUE(received.has_value());
    ASSERT_TRUE(received->capabilities.has_value());
    EXPECT_FALSE(received->capabilities->is_parallel);
    EXPECT_FALSE(received->available_slots.has_value());
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, reports_no_launcher_version_without_a_launcher_path) {
    // arrange
    CapabilityProbeRunner runner;
    std::optional<CapabilityProbeOutcome> received;
    runner.set_outcome_callback([&](const CapabilityProbeOutcome& outcome) { received = outcome; });
    // act
    runner.start(parallel_xyce().string(), "");
    wait_until([&] { return received.has_value(); });
    // assert — the binary is still classified, the launcher is simply absent
    ASSERT_TRUE(received.has_value());
    EXPECT_TRUE(received->capabilities.has_value());
    EXPECT_FALSE(received->launcher_version.has_value());
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, reports_an_unclassifiable_binary) {
    // arrange
    CapabilityProbeRunner runner;
    std::optional<CapabilityProbeOutcome> received;
    runner.set_outcome_callback([&](const CapabilityProbeOutcome& outcome) { received = outcome; });
    // act
    runner.start("/nonexistent/path/to/Xyce", mpi_launcher().string());
    wait_until([&] { return received.has_value(); });
    // assert
    ASSERT_TRUE(received.has_value());
    EXPECT_FALSE(received->capabilities.has_value());
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, reports_no_outcome_for_an_empty_binary_path) {
    // arrange — there is nothing to probe
    CapabilityProbeRunner runner;
    std::atomic<int> deliveries{0};
    runner.set_outcome_callback([&](const CapabilityProbeOutcome&) { deliveries.fetch_add(1); });
    // act
    runner.start("", mpi_launcher().string());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    // assert
    EXPECT_EQ(deliveries.load(), 0);
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, a_superseded_round_reports_under_a_newer_generation) {
    // arrange — a slow binary that outlives the round that started it
    const auto slow = write_script("runner_slow_xyce.sh", "#!/bin/sh\nsleep 1\nif [ \"$1\" = \"-capabilities\" ]; then echo 'Parallel with MPI'; exit 0; fi\nexit 0\n");
    CapabilityProbeRunner runner;
    std::vector<unsigned> generations;
    std::mutex mutex;
    runner.set_outcome_callback([&](const CapabilityProbeOutcome& outcome) {
        std::lock_guard<std::mutex> lock(mutex);
        generations.push_back(outcome.generation);
    });
    // act — start a slow round and immediately supersede it
    const auto first = runner.generation();
    runner.start(slow.string(), "");
    runner.start(serial_xyce().string(), "");
    wait_until([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return !generations.empty();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    // assert — each round reports under its own generation and the last one is current
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(generations.size(), 2u);
    EXPECT_EQ(generations[0], first + 1);
    EXPECT_EQ(generations[1], first + 2);
    EXPECT_EQ(runner.generation(), generations[1]);
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, cancel_supersedes_the_round_in_flight) {
    // arrange
    CapabilityProbeRunner runner;
    std::atomic<int> deliveries{0};
    runner.set_outcome_callback([&](const CapabilityProbeOutcome&) { deliveries.fetch_add(1); });
    // act — abandon the round before it can finish
    runner.start(parallel_xyce().string(), "");
    runner.cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // assert — the round still ran to completion, but it reports under a stale generation
    EXPECT_EQ(deliveries.load(), 1);
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, announce_pending_starts_no_round) {
    // arrange
    CapabilityProbeRunner runner;
    std::atomic<int> deliveries{0};
    runner.set_outcome_callback([&](const CapabilityProbeOutcome&) { deliveries.fetch_add(1); });
    const auto before = runner.generation();
    // act
    runner.announce_pending();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // assert
    EXPECT_EQ(deliveries.load(), 0);
    EXPECT_GT(runner.generation(), before);
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, destruction_joins_a_round_still_in_flight) {
    // arrange — a binary slower than the runner's own lifetime
    const auto slow = write_script("runner_very_slow_xyce.sh", "#!/bin/sh\nsleep 1\nif [ \"$1\" = \"-capabilities\" ]; then echo 'Parallel with MPI'; exit 0; fi\nexit 0\n");
    std::atomic<int> deliveries{0};
    // act — the runner goes out of scope while the probe is still running
    {
        CapabilityProbeRunner runner;
        runner.set_outcome_callback([&](const CapabilityProbeOutcome&) { deliveries.fetch_add(1); });
        runner.start(slow.string(), "");
    }
    // assert — the destructor waited for the round rather than leaving it running
    EXPECT_EQ(deliveries.load(), 1);
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, a_round_without_a_callback_still_completes) {
    // arrange — no callback registered, the round must not crash
    CapabilityProbeRunner runner;
    // act
    runner.start(parallel_xyce().string(), mpi_launcher().string());
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    // assert
    EXPECT_GT(runner.generation(), 0u);
    // cleanup
    remove_scripts();
}

TEST(CapabilityProbeRunnerChecks, replacing_the_callback_affects_only_later_rounds) {
    // arrange
    CapabilityProbeRunner runner;
    std::atomic<int> first_deliveries{0};
    std::atomic<int> second_deliveries{0};
    runner.set_outcome_callback([&](const CapabilityProbeOutcome&) { first_deliveries.fetch_add(1); });
    // act — swap the callback, then run a round that uses the new one
    runner.set_outcome_callback([&](const CapabilityProbeOutcome&) { second_deliveries.fetch_add(1); });
    runner.start(parallel_xyce().string(), "");
    wait_until([&] { return second_deliveries.load() > 0; });
    // assert
    EXPECT_EQ(first_deliveries.load(), 0);
    EXPECT_EQ(second_deliveries.load(), 1);
    // cleanup
    remove_scripts();
}

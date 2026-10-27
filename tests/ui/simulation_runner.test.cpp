#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "ui/simulation_runner.h"

#if !defined(_WIN32)
TEST(SimulationRunnerChecks, stop_and_wait_reaps_a_running_child_and_is_idempotent) {
    // arrange — a shell script that replaces itself with a long sleep
    const auto script_path = std::filesystem::temp_directory_path() / "xyce_studio_runner_stop_test.sh";
    {
        std::ofstream script(script_path, std::ios::out | std::ios::trunc);
        script << "#!/bin/sh\nexec sleep 30\n";
    }
    std::filesystem::permissions(script_path, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
    SimulationRunner runner;
    // act
    runner.start("/bin/sh", script_path, std::filesystem::temp_directory_path());
    runner.stop_and_wait();
    // assert
    EXPECT_FALSE(runner.is_running());
    runner.stop_and_wait();
    EXPECT_FALSE(runner.is_running());
    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(SimulationRunnerChecks, fast_exiting_child_leaves_the_runner_stopped) {
    // arrange — a shell script that exits immediately
    const auto script_path = std::filesystem::temp_directory_path() / "xyce_studio_runner_fast_exit_test.sh";
    {
        std::ofstream script(script_path, std::ios::out | std::ios::trunc);
        script << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(script_path, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
    SimulationRunner runner;
    // act
    runner.start("/bin/sh", script_path, std::filesystem::temp_directory_path());
    // wait for the monitor to reap the child
    for (int attempt = 0; attempt < 500 && runner.is_running(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    runner.stop_and_wait();
    // assert
    EXPECT_FALSE(runner.is_running());
    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}
#endif

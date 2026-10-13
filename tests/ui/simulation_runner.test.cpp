#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "ui/simulation_runner.h"

TEST(SimulationRunnerChecks, start_takes_a_command_and_a_working_directory) {
    // the runner is handed the whole command so a parallel run keeps its launcher arguments
    // arrange / act
    using Signature = void (SimulationRunner::*)(const std::vector<std::string>&, const std::filesystem::path&);
    // assert
    EXPECT_TRUE((std::is_same_v<decltype(&SimulationRunner::start), Signature>));
}

TEST(SimulationRunnerChecks, copy_constructor_is_deleted) {
    // arrange / act
    constexpr bool is_copy_constructible = std::is_copy_constructible_v<SimulationRunner>;
    constexpr bool is_copy_assignable = std::is_copy_assignable_v<SimulationRunner>;
    // assert
    EXPECT_FALSE(is_copy_constructible);
    EXPECT_FALSE(is_copy_assignable);
}

TEST(SimulationRunnerChecks, a_default_runner_is_not_running) {
    // arrange / act
    const SimulationRunner runner;
    // assert
    EXPECT_FALSE(runner.is_running());
    EXPECT_FALSE(runner.was_canceled());
    EXPECT_EQ(runner.exit_code(), -1);
}

TEST(SimulationRunnerChecks, a_default_runner_has_empty_run_paths) {
    // arrange / act
    const SimulationRunner runner;
    // assert
    EXPECT_TRUE(runner.working_directory().empty());
    EXPECT_TRUE(runner.netlist_file_path().empty());
}

TEST(SimulationRunnerChecks, cancelling_a_runner_that_never_started_is_harmless) {
    // arrange / act
    SimulationRunner runner;
    runner.cancel();
    // assert — no child existed, so nothing was signaled and the state is untouched
    EXPECT_FALSE(runner.is_running());
    EXPECT_FALSE(runner.was_canceled());
}

TEST(SimulationRunnerChecks, starting_an_empty_command_reports_a_launch_failure) {
    // arrange — a command with no program cannot be executed; the failure is
    // delivered through the finished callback, which a running slint event loop
    // would drain, so the callback is not observed here
    SimulationRunner runner;
    // act
    runner.start({}, std::filesystem::temp_directory_path());
    // assert — the runner never reached a running state
    EXPECT_FALSE(runner.is_running());
    EXPECT_FALSE(runner.was_canceled());
}

// note: a real child process cannot be launched from a unit test on macOS. the
// runner's reader and monitor threads deliver through slint::invoke_from_event_loop,
// which winit only permits on the main thread, and the test binary has no event loop
// of its own. the argv the child receives is therefore covered through
// build_exec_argv in tests/core/executable_probe.test.cpp, and the end-to-end launch
// is covered by the UI integration tests.

TEST(SimulationRunnerChecks, the_created_netlist_is_written_and_readable) {
    // arrange
    const auto netlist = SimulationRunner::create_temp_netlist("v1 1 0 1\nr1 1 0 1k\n.end\n");
    // act
    const auto content = [&netlist] {
        // read the whole file back
        std::ifstream file(netlist);
        // the file contents
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }();
    // assert
    ASSERT_FALSE(netlist.empty());
    EXPECT_EQ(content, "v1 1 0 1\nr1 1 0 1k\n.end\n");
    std::error_code ec;
    std::filesystem::remove(netlist, ec);
}

TEST(SimulationRunnerChecks, every_created_netlist_gets_a_unique_path) {
    // arrange / act
    const auto first = SimulationRunner::create_temp_netlist("a\n");
    const auto second = SimulationRunner::create_temp_netlist("b\n");
    // assert
    EXPECT_NE(first, second);
    std::error_code ec;
    std::filesystem::remove(first, ec);
    std::filesystem::remove(second, ec);
}

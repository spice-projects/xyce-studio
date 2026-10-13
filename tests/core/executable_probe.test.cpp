#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include <gtest/gtest.h>

#include "core/executable_probe.h"

namespace
{
    // mark a probe script executable on the platforms that need the permission bit
    void make_executable(const std::filesystem::path& path) {
#if !defined(_WIN32)
        ::chmod(path.c_str(), 0755);
#else
        // windows has no executable permission bit
        static_cast<void>(path);
#endif
    }
} // namespace

TEST(ExecutableProbeChecks, captures_standard_output_and_reports_the_exit_code) {
    // arrange
    const auto script = std::filesystem::temp_directory_path() / "xyce_studio_probe_stdout.sh";
    // arrange / act
    {
        std::ofstream file(script);
        file << "#!/bin/sh\necho first line\necho second line\nexit 0\n";
    }
    make_executable(script);
    const auto result = run_executable_probe(script.string(), {});
    // assert
    ASSERT_TRUE(result.exit_code.has_value());
    EXPECT_EQ(*result.exit_code, 0);
    EXPECT_NE(result.standard_output.find("first line"), std::string::npos);
    EXPECT_NE(result.standard_output.find("second line"), std::string::npos);
    EXPECT_TRUE(result.timed_out_output.empty());
    std::filesystem::remove(script);
}

TEST(ExecutableProbeChecks, captures_standard_error_separately_from_standard_output) {
    // arrange
    const auto script = std::filesystem::temp_directory_path() / "xyce_studio_probe_stderr.sh";
    // arrange / act
    {
        std::ofstream file(script);
        file << "#!/bin/sh\necho to-stdout\necho to-stderr 1>&2\nexit 0\n";
    }
    make_executable(script);
    const auto result = run_executable_probe(script.string(), {});
    // assert
    ASSERT_TRUE(result.exit_code.has_value());
    EXPECT_NE(result.standard_output.find("to-stdout"), std::string::npos);
    EXPECT_EQ(result.standard_output.find("to-stderr"), std::string::npos);
    EXPECT_NE(result.standard_error.find("to-stderr"), std::string::npos);
    std::filesystem::remove(script);
}

TEST(ExecutableProbeChecks, reports_a_nonzero_exit_code) {
    // arrange
    const auto script = std::filesystem::temp_directory_path() / "xyce_studio_probe_failure.sh";
    // arrange / act
    {
        std::ofstream file(script);
        file << "#!/bin/sh\necho failing\nexit 3\n";
    }
    make_executable(script);
    const auto result = run_executable_probe(script.string(), {});
    // assert
    ASSERT_TRUE(result.exit_code.has_value());
    EXPECT_EQ(*result.exit_code, 3);
    std::filesystem::remove(script);
}

TEST(ExecutableProbeChecks, reports_no_exit_code_for_an_empty_program) {
    // arrange / act
    const auto result = run_executable_probe("", {"-capabilities"});
    // assert
    EXPECT_FALSE(result.exit_code.has_value());
    EXPECT_TRUE(result.standard_output.empty());
    EXPECT_TRUE(result.standard_error.empty());
}

TEST(ExecutableProbeChecks, reports_the_exec_failure_code_when_the_program_cannot_be_launched) {
    // arrange — a path that does not exist, so the launch fails before the program runs
    const auto missing = std::filesystem::temp_directory_path() / "xyce_studio_probe_absent_executable";
    // arrange / act
    const auto result = run_executable_probe(missing.string(), {});
    // assert — 127 is the conventional code for a program that could not be executed
    ASSERT_TRUE(result.exit_code.has_value());
    EXPECT_EQ(*result.exit_code, 127);
}

TEST(ExecutableProbeChecks, kills_a_program_that_outlives_the_timeout) {
    // arrange — the child emits well inside the budget and then hangs, so the
    // capture is never a race between the child's first write and the kill
    const auto script = std::filesystem::temp_directory_path() / "xyce_studio_probe_hang.sh";
    // arrange / act
    {
        std::ofstream file(script);
        file << "#!/bin/sh\nsleep 0.2\necho started\nsleep 30\n";
    }
    make_executable(script);
    const auto result = run_executable_probe(script.string(), {}, {}, std::chrono::milliseconds(2000));
    // assert — the probe gave up on its own and reports no exit code
    EXPECT_FALSE(result.exit_code.has_value());
    // assert — the output the child produced before the kill was still recovered
    EXPECT_NE(result.timed_out_output.find("started"), std::string::npos);
    std::filesystem::remove(script);
}

TEST(ExecutableProbeChecks, runs_the_program_in_the_requested_working_directory) {
    // arrange
    const auto directory = std::filesystem::temp_directory_path() / "xyce_studio_probe_cwd";
    std::filesystem::create_directories(directory);
    const auto script = std::filesystem::temp_directory_path() / "xyce_studio_probe_cwd.sh";
    // arrange / act
    {
        std::ofstream file(script);
        file << "#!/bin/sh\npwd\n";
    }
    make_executable(script);
    const auto result = run_executable_probe(script.string(), {}, directory);
    // assert
    ASSERT_TRUE(result.exit_code.has_value());
    EXPECT_NE(result.standard_output.find(directory.filename().string()), std::string::npos);
    std::filesystem::remove(script);
    std::filesystem::remove_all(directory);
}

TEST(ExecutableProbeChecks, forwards_the_arguments_to_the_program) {
    // arrange
    const auto script = std::filesystem::temp_directory_path() / "xyce_studio_probe_arguments.sh";
    // arrange / act
    {
        std::ofstream file(script);
        file << "#!/bin/sh\nfor argument in \"$@\"; do echo \"arg:$argument\"; done\n";
    }
    make_executable(script);
    const auto result = run_executable_probe(script.string(), {"-np", "4", "value with space"});
    // assert
    ASSERT_TRUE(result.exit_code.has_value());
    EXPECT_NE(result.standard_output.find("arg:-np"), std::string::npos);
    EXPECT_NE(result.standard_output.find("arg:4"), std::string::npos);
    EXPECT_NE(result.standard_output.find("arg:value with space"), std::string::npos);
    std::filesystem::remove(script);
}

TEST(ExecutableProbeChecks, resolves_a_bare_program_name_through_the_platform_loader) {
    // arrange
    const auto program = std::filesystem::temp_directory_path() / "xyce_studio_probe_loader.sh";
    // arrange / act
    {
        std::ofstream file(program);
        file << "#!/bin/sh\necho loaded\n";
    }
    make_executable(program);
    const auto directory = program.parent_path().string();
    const auto previous_path = std::getenv("PATH");
    const std::string previous = previous_path != nullptr ? previous_path : "";
#ifdef _WIN32
    _putenv_s("PATH", directory.c_str());
#else
    setenv("PATH", directory.c_str(), 1);
#endif
    const auto result = run_executable_probe("xyce_studio_probe_loader.sh", {});
#ifdef _WIN32
    _putenv_s("PATH", previous.c_str());
#else
    setenv("PATH", previous.c_str(), 1);
#endif
    // assert
    ASSERT_TRUE(result.exit_code.has_value());
    EXPECT_NE(result.standard_output.find("loaded"), std::string::npos);
    std::filesystem::remove(program);
}

TEST(ExecutableProbeChecks, build_exec_argv_puts_the_program_first_and_terminates_the_array) {
    // arrange
    const std::vector<std::string> command{"/usr/local/bin/mpirun", "-np", "4", "/opt/Xyce", "/tmp/run.cir"};
    // act
    const auto argv = build_exec_argv(command);
    // assert
    ASSERT_EQ(argv.size(), command.size() + 1);
    EXPECT_EQ(argv[0], command[0].c_str());
    EXPECT_EQ(argv[1], command[1].c_str());
    EXPECT_EQ(argv[2], command[2].c_str());
    EXPECT_EQ(argv[3], command[3].c_str());
    EXPECT_EQ(argv[4], command[4].c_str());
    EXPECT_EQ(argv[5], nullptr);
}

TEST(ExecutableProbeChecks, build_exec_argv_terminates_an_empty_command) {
    // arrange / act
    const auto argv = build_exec_argv({});
    // assert
    ASSERT_EQ(argv.size(), 1u);
    EXPECT_EQ(argv[0], nullptr);
}

TEST(ExecutableProbeChecks, build_exec_argv_preserves_an_argument_holding_spaces) {
    // arrange — a launcher argument with a space in it must stay one argument
    const std::vector<std::string> command{"/usr/local/bin/mpirun", "--hostfile", "/tmp/my hosts", "Xyce"};
    // act
    const auto argv = build_exec_argv(command);
    // assert
    ASSERT_EQ(argv.size(), command.size() + 1);
    EXPECT_STREQ(argv[2], "/tmp/my hosts");
    EXPECT_STREQ(argv[3], "Xyce");
}

TEST(ExecutableProbeChecks, probe_text_contains_ignores_case) {
    // arrange / act
    const auto found = probe_text_contains("mpirun (Open MPI) 5.0.11", "open mpi");
    // assert
    EXPECT_TRUE(found);
}

TEST(ExecutableProbeChecks, probe_text_contains_rejects_an_absent_needle) {
    // arrange / act
    const auto found = probe_text_contains("mpirun (Open MPI) 5.0.11", "MPICH");
    // assert
    EXPECT_FALSE(found);
}

TEST(ExecutableProbeChecks, probe_text_contains_treats_an_empty_needle_as_a_match) {
    // arrange / act
    const auto found = probe_text_contains("anything", "");
    // assert
    EXPECT_TRUE(found);
}

TEST(ExecutableProbeChecks, probe_text_contains_rejects_empty_text_for_a_nonempty_needle) {
    // arrange / act
    const auto found = probe_text_contains("", "parallel");
    // assert
    EXPECT_FALSE(found);
}

TEST(ExecutableProbeChecks, probe_text_first_line_starts_with_accepts_a_matching_first_line) {
    // arrange / act
    const auto matched = probe_text_first_line_starts_with("Parallel with MPI\nVerilog-A models\n", "parallel");
    // assert
    EXPECT_TRUE(matched);
}

TEST(ExecutableProbeChecks, probe_text_first_line_starts_with_skips_leading_blank_lines) {
    // arrange / act
    const auto matched = probe_text_first_line_starts_with("\n\n   \nParallel with MPI\n", "PARALLEL");
    // assert
    EXPECT_TRUE(matched);
}

TEST(ExecutableProbeChecks, probe_text_first_line_starts_with_rejects_a_nonmatching_first_line) {
    // arrange / act
    const auto matched = probe_text_first_line_starts_with("Serial\nFFT (FFTW)\n", "parallel");
    // assert
    EXPECT_FALSE(matched);
}

TEST(ExecutableProbeChecks, probe_text_first_line_starts_with_treats_an_empty_prefix_as_a_match) {
    // arrange / act
    const auto matched = probe_text_first_line_starts_with("Serial", "");
    // assert
    EXPECT_TRUE(matched);
}

TEST(ExecutableProbeChecks, probe_text_first_line_starts_with_rejects_empty_text) {
    // arrange / act
    const auto matched = probe_text_first_line_starts_with("", "parallel");
    // assert
    EXPECT_FALSE(matched);
}

TEST(ExecutableProbeChecks, probe_text_first_line_starts_with_rejects_text_of_only_blank_lines) {
    // arrange / act
    const auto matched = probe_text_first_line_starts_with("\n \n\t\n", "parallel");
    // assert
    EXPECT_FALSE(matched);
}

#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// build the null-terminated argv array the exec family expects from a command,
// whose first element is the program to execute and whose remaining elements are
// its arguments; the pointers alias the given command, which has to outlive them
[[nodiscard]] std::vector<const char*> build_exec_argv(const std::vector<std::string>& command);

// default budget for a probe, generous enough to cover a cold MPI launcher start
inline constexpr std::chrono::milliseconds PROBE_TIMEOUT{10000};

// result of running an executable to completion and capturing both of its streams
struct ProcessProbeResult
{
    // exit code reported by the child, empty when the child never ran or the probe timed out
    std::optional<int> exit_code;
    // everything the child wrote to standard output
    std::string standard_output;
    // everything the child wrote to standard error
    std::string standard_error;
    // combined output captured before the timeout killed the child, empty when the child finished on its own
    std::string timed_out_output;
};

// run the program with the given arguments to completion and capture both streams; the program is launched through the platform loader so a bare name resolves against PATH, an empty working directory keeps the caller's directory, and a child that outlives the timeout is killed so a hung executable can never wedge the caller
[[nodiscard]] ProcessProbeResult run_executable_probe(const std::string& program, const std::vector<std::string>& arguments, const std::filesystem::path& working_directory = {}, std::chrono::milliseconds timeout = PROBE_TIMEOUT);

// report whether the captured text contains the given needle, ignoring case
[[nodiscard]] bool probe_text_contains(std::string_view text, std::string_view needle);

// report whether the first non-blank line of the captured text starts with the given prefix, ignoring case
[[nodiscard]] bool probe_text_first_line_starts_with(std::string_view text, std::string_view prefix);

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

// platform-neutral Xyce simulation runner for the slint ui. The child process
// is spawned with stdout
// and stderr captured on dedicated reader threads, and the callbacks are
// marshalled back to the slint event loop thread so the presenter can update
// the ui without extra synchronization.
class SimulationRunner
{
public:
    using LineCallback = std::function<void(const std::string& line)>;
    using FinishedCallback = std::function<void(int exit_code, bool was_canceled)>;

    SimulationRunner();

    ~SimulationRunner();

    SimulationRunner(const SimulationRunner&) = delete;

    SimulationRunner& operator=(const SimulationRunner&) = delete;

    // launch the program with the netlist path as its only argument; the
    // working directory becomes the child's current directory so Xyce writes
    // its output files next to the temporary netlist. Output lines and the
    // process end are delivered through the registered callbacks on the slint
    // event loop thread.
    void start(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory);

    // request a graceful shutdown of the running child process
    void cancel();

    // cancel a running child and wait for the runner threads to finish
    void stop_and_wait();

    [[nodiscard]] bool is_running() const;

    [[nodiscard]] bool was_canceled() const;

    [[nodiscard]] int exit_code() const;

    [[nodiscard]] const std::filesystem::path& working_directory() const;

    [[nodiscard]] const std::filesystem::path& netlist_file_path() const;

    void set_stdout_callback(LineCallback callback);

    void set_stderr_callback(LineCallback callback);

    void set_finished_callback(FinishedCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

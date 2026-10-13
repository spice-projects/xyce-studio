#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <slint.h>
#include <spdlog/spdlog.h>

#include "../core/executable_probe.h"
#include "simulation_runner.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
    // poll-free stream reading shared by the platform readers: read until EOF,
    // emitting complete lines through the callback; any trailing partial line
    // without a newline is flushed at the end
    template <typename Source>
    void read_lines(Source&& source, SimulationRunner::LineCallback callback) {
        // read buffer
        char buffer[4096];
        // buffered partial line carried between reads
        std::string pending;
        while (true) {
            // read the next chunk from the source
            const auto bytes = source(buffer, sizeof(buffer));
            // end of stream
            if (bytes <= 0)
                break;
            // append the chunk to the buffer
            pending.append(buffer, static_cast<size_t>(bytes));
            // emit every complete line in the buffer
            size_t start = 0;
            while (true) {
                // find the next newline
                const size_t newline = pending.find('\n', start);
                if (newline == std::string::npos)
                    break;
                // extract the line without the newline
                std::string line = pending.substr(start, newline - start);
                // strip a trailing carriage return (\r\n)
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                // deliver the line on the slint event loop thread
                slint::invoke_from_event_loop([callback, line = std::move(line)]() {
                    if (callback)
                        callback(line);
                });
                // move past the consumed line
                start = newline + 1;
            }
            // drop the consumed prefix, keeping the trailing partial line
            if (start > 0)
                pending.erase(0, start);
        }
        // flush the remaining partial line
        if (!pending.empty()) {
            slint::invoke_from_event_loop([callback, line = std::move(pending)]() {
                if (callback)
                    callback(line);
            });
        }
    }
} // namespace

struct SimulationRunner::Impl
{
    // state shared with the worker threads
    std::atomic<bool> running = false;
    std::atomic<bool> canceled = false;
    std::atomic<int> exit_code = -1;

    // ui-thread callbacks, set before start() and only read by the workers
    LineCallback stdout_callback;
    LineCallback stderr_callback;
    FinishedCallback finished_callback;

    // run paths
    std::filesystem::path working_directory;
    std::filesystem::path netlist_file_path;
    std::filesystem::path temp_netlist_path;

    // child process handle
#if defined(_WIN32)
    HANDLE child_process = nullptr;
#else
    pid_t child_pid = -1;
#endif

    // worker threads; the monitor joins the readers after the child exits
    std::thread reader_out;
    std::thread reader_err;
    std::thread monitor;

    ~Impl() {
        // stop a still-running child so the monitor thread terminates
        if (running.load())
            cancel();
        // wait for the monitor to drain the remaining output
        if (monitor.joinable())
            monitor.join();
        // remove the temporary netlist when it was never cleaned up
        remove_temp_netlist();
    }

    void start(const std::vector<std::string>& argv, const std::filesystem::path& cwd) {
        // reject a duplicate start
        if (running.load()) {
            spdlog::warn("SimulationRunner::start called while already running");
            return;
        }
        // reject a command with no program to execute
        if (argv.empty()) {
            spdlog::error("SimulationRunner::start called with an empty command");
            finish_launch_failure();
            return;
        }
        // store the run paths, the netlist is always the final argument of the command
        working_directory = cwd;
        netlist_file_path = argv.back();
        temp_netlist_path = argv.back();
        // reset the run state
        exit_code = -1;
        canceled = false;
        // spawn the child with captured stdout and stderr
        spawn_child(argv, cwd);
    }

    void cancel() {
        // check the child is still running and not already canceled
        if (!running.load() || canceled.exchange(true))
            return;
#if defined(_WIN32)
        // terminate the child process
        spdlog::info("Canceling simulation");
        TerminateProcess(child_process, 1);
#else
        // send SIGTERM for a graceful shutdown
        spdlog::info("Canceling simulation (pid {})", static_cast<int>(child_pid));
        ::kill(child_pid, SIGTERM);
        // fallback SIGKILL after a short delay in case the child ignores SIGTERM
        std::thread([pid = child_pid] {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            ::kill(pid, SIGKILL);
        }).detach();
#endif
    }

    // remove the temporary netlist file created for this run
    void remove_temp_netlist() {
        // check a temporary netlist was recorded
        if (temp_netlist_path.empty())
            return;
        // attempt to remove the file
        std::error_code ec;
        std::filesystem::remove(temp_netlist_path, ec);
        if (ec)
            spdlog::warn("Failed to remove temp netlist: {}", temp_netlist_path.string());
        // clear the recorded path
        temp_netlist_path.clear();
    }

    // post a launch failure result on the ui thread
    void finish_launch_failure() {
        // record the failure state
        running = false;
        exit_code = 127;
        // deliver the result on the ui thread; capture the callback by value so
        // a queued event never outlives this Impl
        slint::invoke_from_event_loop([callback = finished_callback]() {
            if (callback)
                callback(127, false);
        });
    }

#if defined(_WIN32)
    void spawn_child(const std::vector<std::string>& argv, const std::filesystem::path& cwd) {
        // the program being launched and the netlist it was handed
        const std::string& program = argv.front();
        const std::string netlist_path = argv.back().string();
        // inheritable security attributes for the pipe write ends
        SECURITY_ATTRIBUTES security_attributes{};
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.bInheritHandle = TRUE;
        // create the stdout and stderr pipes
        HANDLE out_read = nullptr;
        HANDLE out_write = nullptr;
        HANDLE err_read = nullptr;
        HANDLE err_write = nullptr;
        if (!CreatePipe(&out_read, &out_write, &security_attributes, 0) || !CreatePipe(&err_read, &err_write, &security_attributes, 0)) {
            // clean up any created handles
            if (out_read)
                CloseHandle(out_read);
            if (out_write)
                CloseHandle(out_write);
            if (err_read)
                CloseHandle(err_read);
            if (err_write)
                CloseHandle(err_write);
            spdlog::error("SimulationRunner::start failed to create pipes: {} {}", program, netlist_path);
            finish_launch_failure();
            return;
        }
        // the write ends are inherited by the child
        SetHandleInformation(out_write, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(err_write, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        // standard handles for the child
        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdOutput = out_write;
        startup_info.hStdError = err_write;
        startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        // build the command line with every argument quoted
        std::wstring command;
        for (const auto& argument : argv) {
            const std::wstring wide_argument = std::filesystem::path(argument).wstring();
            // separate the quoted arguments with a space
            if (!command.empty())
                command += L" ";
            command += L"\"" + wide_argument + L"\"";
        }
        const std::wstring cwd_wide = cwd.empty() ? std::wstring() : cwd.wstring();
        // launch the process without a console window
        PROCESS_INFORMATION process_info{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd_wide.empty() ? nullptr : cwd_wide.c_str(), &startup_info, &process_info)) {
            // close the pipe handles
            CloseHandle(out_read);
            CloseHandle(out_write);
            CloseHandle(err_read);
            CloseHandle(err_write);
            spdlog::error("SimulationRunner::start failed to launch: {} {}", program, netlist_path);
            finish_launch_failure();
            return;
        }
        // the parent keeps the read ends, the write ends are owned by the child
        CloseHandle(out_write);
        CloseHandle(err_write);
        child_process = process_info.hProcess;
        CloseHandle(process_info.hThread);
        // drain stdout and stderr on dedicated reader threads
        reader_out = std::thread([this, out_read] { read_lines_win(out_read, stdout_callback); });
        reader_err = std::thread([this, err_read] { read_lines_win(err_read, stderr_callback); });
        // wait for the child and report the result
        monitor = std::thread([this] { monitor_win(); });
        running = true;
        spdlog::info("Xyce simulation started");
    }

    static void read_lines_win(HANDLE handle, LineCallback callback) {
        // read the pipe until EOF, forwarding the collected lines
        read_lines(
            [handle](char* buffer, size_t size) -> long {
                DWORD bytes_read = 0;
                const BOOL ok = ReadFile(handle, buffer, static_cast<DWORD>(size), &bytes_read, nullptr);
                if (!ok || bytes_read == 0)
                    return 0;
                return static_cast<long>(bytes_read);
            },
            std::move(callback));
        // release the read handle
        CloseHandle(handle);
    }

    void monitor_win() {
        // wait for the child process to exit
        WaitForSingleObject(child_process, INFINITE);
        // read the exit code
        DWORD code = 1;
        GetExitCodeProcess(child_process, &code);
        CloseHandle(child_process);
        child_process = nullptr;
        // drain the remaining buffered output
        if (reader_out.joinable())
            reader_out.join();
        if (reader_err.joinable())
            reader_err.join();
        // clean up the temporary netlist
        remove_temp_netlist();
        // record and deliver the result
        exit_code = static_cast<int>(code);
        running = false;
        const bool was_canceled = canceled.load();
        // capture the callback by value so a queued event never outlives this Impl
        slint::invoke_from_event_loop([callback = finished_callback, code, was_canceled]() {
            if (callback)
                callback(static_cast<int>(code), was_canceled);
        });
        spdlog::info("Xyce simulation finished (exit code {}, canceled {})", static_cast<int>(code), was_canceled);
    }
#else
    void spawn_child(const std::vector<std::string>& argv, const std::filesystem::path& cwd) {
        // the program being launched and the netlist it was handed
        const std::string& program = argv.front();
        const std::string& netlist_path = argv.back();
        // create the stdout and stderr pipes
        int out_pipe[2] = {-1, -1};
        int err_pipe[2] = {-1, -1};
        if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
            // close any created pipes
            if (out_pipe[0] >= 0) {
                ::close(out_pipe[0]);
                ::close(out_pipe[1]);
            }
            if (err_pipe[0] >= 0) {
                ::close(err_pipe[0]);
                ::close(err_pipe[1]);
            }
            spdlog::error("SimulationRunner::start failed to create pipes: {} {}", program, netlist_path);
            finish_launch_failure();
            return;
        }
        // fork the child process
        const pid_t pid = ::fork();
        if (pid < 0) {
            // close all pipe ends on failure
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
            spdlog::error("SimulationRunner::start failed to fork: {} {}", program, netlist_path);
            finish_launch_failure();
            return;
        }
        if (pid == 0) {
            // child process: change the working directory before exec
            if (!cwd.empty() && ::chdir(cwd.c_str()) != 0)
                _exit(127);
            // wire the pipe write ends to the standard streams
            ::dup2(out_pipe[1], STDOUT_FILENO);
            ::dup2(err_pipe[1], STDERR_FILENO);
            // close the inherited pipe ends
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
            // arguments: the program followed by every argument of the command
            const auto child_argv = build_exec_argv(argv);
            // execute the program, replacing the child image
            ::execv(program.c_str(), const_cast<char* const*>(child_argv.data()));
            // exec failed, report the failure
            _exit(127);
        }
        // parent: close the write ends, keep the read ends for the readers
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);
        child_pid = pid;
        // drain stdout and stderr on dedicated reader threads
        reader_out = std::thread([this, out_fd = out_pipe[0]] { read_lines_posix(out_fd, stdout_callback); });
        reader_err = std::thread([this, err_fd = err_pipe[0]] { read_lines_posix(err_fd, stderr_callback); });
        // wait for the child and report the result
        monitor = std::thread([this] { monitor_posix(); });
        running = true;
        spdlog::info("Xyce simulation started (pid {})", static_cast<int>(pid));
    }

    static void read_lines_posix(int fd, LineCallback callback) {
        // read the pipe until EOF, forwarding the collected lines
        read_lines([fd](char* buffer, size_t size) -> ssize_t { return ::read(fd, buffer, size); }, std::move(callback));
        // release the read descriptor
        ::close(fd);
    }

    void monitor_posix() {
        // wait for the child process to exit
        int status = 0;
        ::waitpid(child_pid, &status, 0);
        // translate the wait status to an exit code
        int code = -1;
        if (WIFEXITED(status))
            code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            code = 128 + WTERMSIG(status);
        // drain the remaining buffered output
        if (reader_out.joinable())
            reader_out.join();
        if (reader_err.joinable())
            reader_err.join();
        // clean up the temporary netlist
        remove_temp_netlist();
        // record and deliver the result
        exit_code = code;
        running = false;
        const bool was_canceled = canceled.load();
        // capture the callback by value so a queued event never outlives this Impl
        slint::invoke_from_event_loop([callback = finished_callback, code, was_canceled]() {
            if (callback)
                callback(code, was_canceled);
        });
        spdlog::info("Xyce simulation finished (exit code {}, canceled {})", code, was_canceled);
    }
#endif
};

SimulationRunner::SimulationRunner() :
    m_impl(std::make_unique<Impl>()) {}

SimulationRunner::~SimulationRunner() = default;

void SimulationRunner::start(const std::vector<std::string>& argv, const std::filesystem::path& working_directory) { m_impl->start(argv, working_directory); }

void SimulationRunner::cancel() { m_impl->cancel(); }

bool SimulationRunner::is_running() const { return m_impl->running.load(); }

bool SimulationRunner::was_canceled() const { return m_impl->canceled.load(); }

int SimulationRunner::exit_code() const { return m_impl->exit_code.load(); }

const std::filesystem::path& SimulationRunner::working_directory() const { return m_impl->working_directory; }

const std::filesystem::path& SimulationRunner::netlist_file_path() const { return m_impl->netlist_file_path; }

void SimulationRunner::set_stdout_callback(LineCallback callback) { m_impl->stdout_callback = std::move(callback); }

void SimulationRunner::set_stderr_callback(LineCallback callback) { m_impl->stderr_callback = std::move(callback); }

void SimulationRunner::set_finished_callback(FinishedCallback callback) { m_impl->finished_callback = std::move(callback); }

std::filesystem::path SimulationRunner::create_temp_netlist(std::string_view text) {
    // unique name from the process id, a monotonic counter and the clock
    static std::atomic<uint64_t> counter{0};
#if defined(_WIN32)
    const auto pid = static_cast<long long>(GetCurrentProcessId());
#else
    const auto pid = static_cast<long long>(getpid());
#endif
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto name = "xyce_" + std::to_string(pid) + "_" + std::to_string(now) + "_" + std::to_string(counter.fetch_add(1)) + ".cir";
    // build the full temporary path
    const auto path = std::filesystem::temp_directory_path() / name;
    // write the netlist content
    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file.is_open())
        return {};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    // return the path as std::filesystem::path
    return path;
}

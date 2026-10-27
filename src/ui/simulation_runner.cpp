#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>

#include <slint.h>
#include <spdlog/spdlog.h>

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

    // child process handle
#if defined(_WIN32)
    std::atomic<HANDLE> child_process{nullptr};
#else
    std::atomic<pid_t> child_pid{-1};
#endif

    // worker threads; the monitor joins the readers after the child exits
    std::thread reader_out;
    std::thread reader_err;
    std::thread monitor;
    // POSIX fallback timer that force-kills a child ignoring SIGTERM
    std::thread kill_timer;

    ~Impl() {
        // stop and reap the child so the monitor thread terminates
        stop_and_wait();
    }

    void start(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& cwd) {
        // reject a duplicate start
        if (running.load()) {
            spdlog::warn("SimulationRunner::start called while already running");
            return;
        }
        // reap a previous finished run before reusing the runner
        if (monitor.joinable() || reader_out.joinable() || reader_err.joinable() || kill_timer.joinable())
            stop_and_wait();
        // store the run paths
        working_directory = cwd;
        netlist_file_path = netlist_path;
        // reset the run state
        exit_code = -1;
        canceled = false;
        // spawn the child with captured stdout and stderr
        spawn_child(program, netlist_path, cwd);
    }

    void cancel() {
        // check the child is still running and not already canceled
        if (!running.load() || canceled.exchange(true))
            return;
#if defined(_WIN32)
        // terminate the child process
        spdlog::info("Canceling simulation");
        const auto handle = child_process.load();
        // reject a child that already exited
        if (handle != nullptr)
            TerminateProcess(handle, 1);
#else
        // send SIGTERM for a graceful shutdown
        const auto pid = child_pid.load();
        // reject a child that already exited
        if (pid <= 0)
            return;
        spdlog::info("Canceling simulation (pid {})", static_cast<int>(pid));
        ::kill(pid, SIGTERM);
        // replace any previous fallback timer
        if (kill_timer.joinable())
            kill_timer.join();
        // force-kill the child when it ignores SIGTERM for two seconds
        kill_timer = std::thread([this] {
            // poll the running state so a clean exit ends the timer early
            for (int attempt = 0; attempt < 200 && running.load(); ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            // force-kill a child that is still running
            if (running.load()) {
                // load the current child identifier
                const auto current_pid = child_pid.load();
                // guard against an already reaped child
                if (current_pid > 0)
                    ::kill(current_pid, SIGKILL);
            }
        });
#endif
    }

    // cancel the child when needed and wait for the monitor to reap it
    void stop_and_wait() {
        // request cancellation when the child is still running
        if (running.load())
            cancel();
        // wait for the monitor to drain the remaining output
        if (monitor.joinable())
            monitor.join();
        // wait for the POSIX fallback timer
        if (kill_timer.joinable())
            kill_timer.join();
        // wait for any reader the monitor did not own
        if (reader_out.joinable())
            reader_out.join();
        if (reader_err.joinable())
            reader_err.join();
        // the child is reaped at this point
        running = false;
    }

    // post a launch failure result on the ui thread
    void finish_launch_failure() {
        // record the failure state
        running = false;
        exit_code = 127;
#if defined(_WIN32)
        // clear the unused child handle
        child_process = nullptr;
#else
        // clear the unused child identifier
        child_pid = -1;
#endif
        // deliver the result on the ui thread when a callback is registered; capture it by value so a queued event never outlives this Impl
        if (finished_callback)
            slint::invoke_from_event_loop([callback = finished_callback]() {
                if (callback)
                    callback(127, false);
            });
    }

#if defined(_WIN32)
    void spawn_child(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& cwd) {
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
            spdlog::error("SimulationRunner::start failed to create pipes: {} {}", program, netlist_path.string());
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
        // build the command line with both paths quoted
        const std::wstring program_wide = std::filesystem::path(program).wstring();
        const std::wstring netlist_wide = netlist_path.wstring();
        std::wstring command = L"\"" + program_wide + L"\" \"" + netlist_wide + L"\"";
        const std::wstring cwd_wide = cwd.empty() ? std::wstring() : cwd.wstring();
        // launch the process without a console window
        PROCESS_INFORMATION process_info{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd_wide.empty() ? nullptr : cwd_wide.c_str(), &startup_info, &process_info)) {
            // close the pipe handles
            CloseHandle(out_read);
            CloseHandle(out_write);
            CloseHandle(err_read);
            CloseHandle(err_write);
            spdlog::error("SimulationRunner::start failed to launch: {} {}", program, netlist_path.string());
            finish_launch_failure();
            return;
        }
        // the parent keeps the read ends, the write ends are owned by the child
        CloseHandle(out_write);
        CloseHandle(err_write);
        child_process = process_info.hProcess;
        CloseHandle(process_info.hThread);
        // mark the run active before the monitor can observe a fast exit
        running = true;
        // drain stdout and stderr on dedicated reader threads
        reader_out = std::thread([this, out_read] { read_lines_win(out_read, stdout_callback); });
        reader_err = std::thread([this, err_read] { read_lines_win(err_read, stderr_callback); });
        // wait for the child and report the result
        monitor = std::thread([this] { monitor_win(); });
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
        const auto handle = child_process.load();
        WaitForSingleObject(handle, INFINITE);
        // read the exit code
        DWORD code = 1;
        GetExitCodeProcess(handle, &code);
        CloseHandle(handle);
        child_process = nullptr;
        // mark the child as reaped before draining the readers
        running = false;
        // drain the remaining buffered output
        if (reader_out.joinable())
            reader_out.join();
        if (reader_err.joinable())
            reader_err.join();
        // record and deliver the result
        exit_code = static_cast<int>(code);
        const bool was_canceled = canceled.load();
        // capture the callback by value so a queued event never outlives this Impl
        if (finished_callback)
            slint::invoke_from_event_loop([callback = finished_callback, code, was_canceled]() {
                if (callback)
                    callback(static_cast<int>(code), was_canceled);
            });
        spdlog::info("Xyce simulation finished (exit code {}, canceled {})", static_cast<int>(code), was_canceled);
    }
#else
    void spawn_child(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& cwd) {
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
            spdlog::error("SimulationRunner::start failed to create pipes: {} {}", program, netlist_path.string());
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
            spdlog::error("SimulationRunner::start failed to fork: {} {}", program, netlist_path.string());
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
            // arguments: [program, netlist_path, nullptr]
            const std::string prog = program;
            const std::string net = netlist_path.string();
            const char* argv[] = {prog.c_str(), net.c_str(), nullptr};
            // execute the program, replacing the child image
            ::execv(prog.c_str(), const_cast<char* const*>(argv));
            // exec failed, report the failure
            _exit(127);
        }
        // parent: close the write ends, keep the read ends for the readers
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);
        child_pid = pid;
        // mark the run active before the monitor can observe a fast exit
        running = true;
        // drain stdout and stderr on dedicated reader threads
        reader_out = std::thread([this, out_fd = out_pipe[0]] { read_lines_posix(out_fd, stdout_callback); });
        reader_err = std::thread([this, err_fd = err_pipe[0]] { read_lines_posix(err_fd, stderr_callback); });
        // wait for the child and report the result
        monitor = std::thread([this] { monitor_posix(); });
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
        const auto pid = child_pid.load();
        int status = 0;
        ::waitpid(pid, &status, 0);
        // clear the reaped identifier before draining the readers
        child_pid = -1;
        // mark the child as reaped before draining the readers
        running = false;
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
        // record and deliver the result
        exit_code = code;
        const bool was_canceled = canceled.load();
        // capture the callback by value so a queued event never outlives this Impl
        if (finished_callback)
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

void SimulationRunner::start(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) { m_impl->start(program, netlist_path, working_directory); }

void SimulationRunner::cancel() { m_impl->cancel(); }

void SimulationRunner::stop_and_wait() { m_impl->stop_and_wait(); }

bool SimulationRunner::is_running() const { return m_impl->running.load(); }

bool SimulationRunner::was_canceled() const { return m_impl->canceled.load(); }

int SimulationRunner::exit_code() const { return m_impl->exit_code.load(); }

const std::filesystem::path& SimulationRunner::working_directory() const { return m_impl->working_directory; }

const std::filesystem::path& SimulationRunner::netlist_file_path() const { return m_impl->netlist_file_path; }

void SimulationRunner::set_stdout_callback(LineCallback callback) { m_impl->stdout_callback = std::move(callback); }

void SimulationRunner::set_stderr_callback(LineCallback callback) { m_impl->stderr_callback = std::move(callback); }

void SimulationRunner::set_finished_callback(FinishedCallback callback) { m_impl->finished_callback = std::move(callback); }

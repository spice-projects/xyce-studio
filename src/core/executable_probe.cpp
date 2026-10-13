#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

#include "executable_probe.h"
#include "util.h"

namespace
{
    // most a probe ever accumulates per stream, a capability banner is a handful of lines and this only bounds a runaway child
    constexpr size_t PROBE_OUTPUT_LIMIT = 1u << 20;

#if defined(_WIN32)
    // inheritable security attributes for the pipe write ends
    SECURITY_ATTRIBUTES inheritable_attributes() {
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;
        return attributes;
    }

    // append up to the remaining output budget from a pipe handle
    void drain_handle(HANDLE read_end, std::string& sink) {
        // stop once the budget is exhausted so a runaway child cannot grow the capture without bound
        if (sink.size() >= PROBE_OUTPUT_LIMIT)
            return;
        // peek to learn how many bytes are buffered without blocking
        DWORD available = 0;
        if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &available, nullptr))
            return;
        // nothing buffered yet
        if (available == 0)
            return;
        // never read past the remaining budget
        const auto wanted = static_cast<DWORD>(std::min<size_t>(available, PROBE_OUTPUT_LIMIT - sink.size()));
        std::array<char, 4096> buffer{};
        DWORD bytes_read = 0;
        if (ReadFile(read_end, buffer.data(), std::min(wanted, static_cast<DWORD>(buffer.size())), &bytes_read, nullptr) && bytes_read > 0)
            sink.append(buffer.data(), bytes_read);
    }

    // close a handle when the launcher produced one
    void close_handle(HANDLE handle) {
        // nothing to release
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
#else
    // set the non-blocking flag on a descriptor so the poll loop never stalls on a stream that is quiet
    void make_non_blocking(int fd) {
        // read the current descriptor flags
        const int flags = ::fcntl(fd, F_GETFL, 0);
        // the descriptor could not be inspected or already carries the flag
        if (flags < 0 || (flags & O_NONBLOCK) != 0)
            return;
        // add the non-blocking flag
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    // append up to the remaining output budget from a descriptor
    void drain_descriptor(int fd, std::string& sink) {
        // stop once the budget is exhausted so a runaway child cannot grow the capture without bound
        if (sink.size() >= PROBE_OUTPUT_LIMIT)
            return;
        std::array<char, 4096> buffer{};
        while (sink.size() < PROBE_OUTPUT_LIMIT) {
            const auto bytes = ::read(fd, buffer.data(), std::min(buffer.size(), PROBE_OUTPUT_LIMIT - sink.size()));
            // the non-blocking descriptor has nothing left, or it failed
            if (bytes <= 0)
                break;
            sink.append(buffer.data(), static_cast<size_t>(bytes));
        }
    }

    // translate a wait status into an exit code
    std::optional<int> exit_code_from_status(int status) {
        // the child exited normally
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        // the child was killed by a signal
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        // the child neither exited nor was signaled
        return std::nullopt;
    }
#endif
} // namespace

ProcessProbeResult run_executable_probe(const std::string& program, const std::vector<std::string>& arguments, const std::filesystem::path& working_directory, std::chrono::milliseconds timeout) {
    // the outcome reported back to the caller
    ProcessProbeResult result;
    // reject a probe with no program to run
    if (program.empty())
        return result;

#if defined(_WIN32)
    // inheritable security attributes for the pipe write ends
    const SECURITY_ATTRIBUTES security_attributes = inheritable_attributes();
    // create the stdout and stderr pipes
    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    HANDLE err_read = nullptr;
    HANDLE err_write = nullptr;
    if (!CreatePipe(&out_read, &out_write, &security_attributes, 0) || !CreatePipe(&err_read, &err_write, &security_attributes, 0)) {
        // clean up any created handles
        close_handle(out_read);
        close_handle(out_write);
        close_handle(err_read);
        close_handle(err_write);
        spdlog::error("run_executable_probe failed to create pipes: {}", program);
        return result;
    }
    // keep the parent pipe read ends out of the child process
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
    // quote every argument, CreateProcessW parses the single command line string itself
    std::wstring command_line;
    for (const auto& argument : arguments) {
        const std::wstring wide_argument = std::filesystem::path(argument).wstring();
        // separate the quoted arguments with a space
        if (!command_line.empty())
            command_line += L" ";
        command_line += L"\"" + wide_argument + L"\"";
    }
    // standard handles for the child
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdOutput = out_write;
    startup_info.hStdError = err_write;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    // the working directory for the child
    const std::wstring working_directory_wide = working_directory.empty() ? std::wstring() : working_directory.wstring();
    // launch the process without a console window
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, working_directory_wide.empty() ? nullptr : working_directory_wide.c_str(), &startup_info, &process_info)) {
        // clean up every handle
        close_handle(out_read);
        close_handle(out_write);
        close_handle(err_read);
        close_handle(err_write);
        spdlog::error("run_executable_probe failed to launch: {}", program);
        return result;
    }
    // the parent keeps the read ends, the write ends are owned by the child
    close_handle(out_write);
    close_handle(err_write);
    close_handle(process_info.hThread);
    // the probe budget starts once the child is running
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    // the child exited before the budget ran out
    bool exited = false;
    // the budget ran out and the child was killed
    bool timed_out = false;
    while (!exited && !timed_out) {
        // the time left before the probe budget is spent
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        // the budget is spent, stop waiting for this slice
        if (remaining.count() <= 0) {
            timed_out = true;
            break;
        }
        // wake as soon as the child exits, capped by the remaining budget
        if (WaitForSingleObject(process_info.hProcess, static_cast<DWORD>(remaining.count())) == WAIT_OBJECT_0)
            exited = true;
        // collect whatever the child produced during the slice
        drain_handle(out_read, result.standard_output);
        drain_handle(err_read, result.standard_error);
    }
    // kill a child that outlived the budget so it cannot linger behind the probe
    if (timed_out) {
        TerminateProcess(process_info.hProcess, 1);
        WaitForSingleObject(process_info.hProcess, INFINITE);
        // recover the partial output the killed child had already produced
        drain_handle(out_read, result.standard_output);
        drain_handle(err_read, result.standard_error);
        // hand the partial capture back through the timeout channel
        result.timed_out_output = result.standard_output + result.standard_error;
        spdlog::warn("run_executable_probe timed out after {}ms, killed: {}", timeout.count(), program);
    }
    // collect the tail the child wrote just before exiting
    drain_handle(out_read, result.standard_output);
    drain_handle(err_read, result.standard_error);
    // read the exit code of the child
    DWORD exit_code = 0;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    // a probe that had to kill its child reports no exit code, the caller sees the timeout instead
    if (!timed_out)
        result.exit_code = static_cast<int>(exit_code);
    // release every handle
    close_handle(process_info.hProcess);
    close_handle(out_read);
    close_handle(err_read);
    return result;
#else
    // create pipes for stdout and stderr
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        // close any created pipe ends
        if (out_pipe[0] >= 0) {
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
        }
        if (err_pipe[0] >= 0) {
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
        }
        spdlog::error("run_executable_probe failed to create pipes: {}", program);
        return result;
    }
    // the poll loop reads both descriptors without blocking, so neither pipe can fill up
    make_non_blocking(out_pipe[0]);
    make_non_blocking(err_pipe[0]);
    // fork the child process
    const pid_t pid = ::fork();
    // the child could not be created
    if (pid < 0) {
        // close all pipe ends
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        spdlog::error("run_executable_probe failed to fork: {}", program);
        return result;
    }
    // the child process replaces its image with the probe program
    if (pid == 0) {
        // change the working directory when the caller requested one
        if (!working_directory.empty() && ::chdir(working_directory.c_str()) != 0)
            _exit(127);
        // wire the pipe write ends to the standard streams
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        // close the inherited pipe ends
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        // build the argv array with the program as argv[0]
        std::vector<std::string> argv_storage;
        argv_storage.reserve(arguments.size() + 1);
        argv_storage.push_back(program);
        argv_storage.insert(argv_storage.end(), arguments.begin(), arguments.end());
        std::vector<const char*> argv;
        argv.reserve(argv_storage.size() + 1);
        for (const auto& argument : argv_storage)
            argv.push_back(argument.c_str());
        argv.push_back(nullptr);
        // launch through the platform loader so a bare program name resolves against PATH
        ::execvp(argv[0], const_cast<char* const*>(argv.data()));
        // the program could not be launched
        _exit(127);
    }
    // parent: close the write ends, keep the read ends for the drain loop
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    // the probe budget starts once the child is running
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    // the child exited before the budget ran out
    bool exited = false;
    // the budget ran out and the child was killed
    bool timed_out = false;
    // the wait status collected for the child
    int status = 0;
    while (!exited && !timed_out) {
        // the time left before the probe budget is spent
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        // the budget is spent, stop waiting for this slice
        if (remaining.count() <= 0) {
            timed_out = true;
            break;
        }
        // wait for either output or the child exiting, capped by the remaining budget
        pollfd descriptors[2]{};
        descriptors[0].fd = out_pipe[0];
        descriptors[0].events = POLLIN;
        descriptors[1].fd = err_pipe[0];
        descriptors[1].events = POLLIN;
        const int ready = ::poll(descriptors, 2, static_cast<int>(remaining.count()));
        // the slice elapsed without output, fall through to the child check
        if (ready < 0 && errno != EINTR) {
            timed_out = true;
            break;
        }
        // collect whatever the child produced during the slice
        if (ready > 0) {
            if ((descriptors[0].revents & (POLLIN | POLLHUP)) != 0)
                drain_descriptor(out_pipe[0], result.standard_output);
            if ((descriptors[1].revents & (POLLIN | POLLHUP)) != 0)
                drain_descriptor(err_pipe[0], result.standard_error);
        }
        // check whether the child already exited without blocking on it
        if (::waitpid(pid, &status, WNOHANG) == pid)
            exited = true;
    }
    // collect the tail the child wrote just before exiting
    drain_descriptor(out_pipe[0], result.standard_output);
    drain_descriptor(err_pipe[0], result.standard_error);
    // kill a child that outlived the budget so it cannot linger behind the probe
    if (timed_out) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
        // recover the partial output the killed child had already produced
        drain_descriptor(out_pipe[0], result.standard_output);
        drain_descriptor(err_pipe[0], result.standard_error);
        // hand the partial capture back through the timeout channel
        result.timed_out_output = result.standard_output + result.standard_error;
        spdlog::warn("run_executable_probe timed out after {}ms, killed: {}", timeout.count(), program);
    }
    // reap a child that was still running when the streams closed
    else if (!exited)
        ::waitpid(pid, &status, 0);
    // release the read ends
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);
    // a probe that had to kill its child reports no exit code, the caller sees the timeout instead
    if (!timed_out)
        result.exit_code = exit_code_from_status(status);
    return result;
#endif
}

bool probe_text_contains(std::string_view text, std::string_view needle) {
    // an empty needle matches everything
    if (needle.empty())
        return true;
    // an empty text matches nothing but the empty needle
    if (text.empty())
        return false;
    // compare both sides without case so launcher banners need no normalization by the caller
    return to_lower(text).find(to_lower(needle)) != std::string::npos;
}

bool probe_text_first_line_starts_with(std::string_view text, std::string_view prefix) {
    // an empty prefix matches everything
    if (prefix.empty())
        return true;
    // an empty text has no first line to match
    if (text.empty())
        return false;
    // the offset of the first non-blank line
    size_t start = 0;
    while (start < text.size()) {
        // the extent of the current line
        const auto line_end = text.find('\n', start);
        const auto extent = line_end == std::string_view::npos ? text.substr(start) : text.substr(start, line_end - start);
        // trim the carriage return a Windows launcher leaves at the end of the line
        auto candidate = trim(extent);
        if (!candidate.empty()) {
            // only the first non-blank line classifies the binary, a Xyce run banner also contains the word parallel
            return to_lower(candidate).starts_with(to_lower(prefix));
        }
        // the line was blank, look at the next one
        if (line_end == std::string_view::npos)
            break;
        start = line_end + 1;
    }
    // the capture held no non-blank line at all
    return false;
}
std::vector<const char*> build_exec_argv(const std::vector<std::string>& command) {
    // the null-terminated array handed to the exec family
    std::vector<const char*> argv;
    // reserve room for the command and the terminator
    argv.reserve(command.size() + 1);
    // every element of the command, program first
    for (const auto& argument : command)
        argv.push_back(argument.c_str());
    // terminate the array
    argv.push_back(nullptr);
    return argv;
}

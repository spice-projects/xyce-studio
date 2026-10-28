#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

#include "kicad_cli_netlist_source.h"

namespace
{
    // create a unique temporary file path for exported netlists
    std::filesystem::path create_temp_netlist_path() {
        // atomic counter for unique names
        static std::atomic<uint64_t> counter{0};
#if defined(_WIN32)
        const auto pid = static_cast<long long>(GetCurrentProcessId());
#else
        const auto pid = static_cast<long long>(getpid());
#endif
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto name = "kicad_xyce_" + std::to_string(pid) + "_" + std::to_string(now) + "_" + std::to_string(counter.fetch_add(1)) + ".net";
        return std::filesystem::temp_directory_path() / name;
    }

#if !defined(_WIN32)
    // read all content from an open file descriptor until EOF
    std::string read_all_fd(int fd) {
        std::string content;
        char buffer[4096];
        while (true) {
            const ssize_t bytes_read = ::read(fd, buffer, sizeof(buffer));
            if (bytes_read <= 0)
                break;
            content.append(buffer, static_cast<size_t>(bytes_read));
        }
        return content;
    }

    // run kicad-cli and return the process exit code, capturing stdout and stderr
    int run_kicad_cli(const std::filesystem::path& command_cwd, const std::vector<std::string>& args, std::string& stdout_text, std::string& stderr_text) {
        if (args.empty())
            return -1;
        // create pipes for stdout and stderr
        int out_pipe[2] = {-1, -1};
        int err_pipe[2] = {-1, -1};
        if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
            if (out_pipe[0] >= 0) {
                ::close(out_pipe[0]);
                ::close(out_pipe[1]);
            }
            if (err_pipe[0] >= 0) {
                ::close(err_pipe[0]);
                ::close(err_pipe[1]);
            }
            return -1;
        }
        // fork child process
        const pid_t pid = ::fork();
        if (pid < 0) {
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
            return -1;
        }
        if (pid == 0) {
            // child process: change working directory if specified
            if (!command_cwd.empty() && ::chdir(command_cwd.c_str()) != 0)
                _exit(127);
            // redirect stdout and stderr to pipe write ends
            ::dup2(out_pipe[1], STDOUT_FILENO);
            ::dup2(err_pipe[1], STDERR_FILENO);
            // close unused pipe descriptors
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
            // prepare argv array
            std::vector<const char*> argv;
            argv.reserve(args.size() + 1);
            for (const auto& arg : args)
                argv.push_back(arg.c_str());
            argv.push_back(nullptr);
            // execute kicad-cli
            ::execvp(argv[0], const_cast<char* const*>(argv.data()));
            // exec failed
            _exit(127);
        }
        // parent: close pipe write ends
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);
        // read stdout and stderr in parallel threads to avoid pipe deadlocks
        std::thread thread_out([&stdout_text, &out_pipe] { stdout_text = read_all_fd(out_pipe[0]); });
        std::thread thread_err([&stderr_text, &err_pipe] { stderr_text = read_all_fd(err_pipe[0]); });
        thread_out.join();
        thread_err.join();
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        // wait for child process to exit
        int status = 0;
        ::waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        return -1;
    }
#else
    // read all content from a pipe handle until EOF
    std::string read_all_handle(HANDLE handle) {
        std::string content;
        char buffer[4096];
        DWORD bytes_read = 0;
        while (ReadFile(handle, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
            content.append(buffer, bytes_read);
        }
        return content;
    }

    // run kicad-cli and return the process exit code, capturing stdout and stderr
    int run_kicad_cli(const std::filesystem::path& command_cwd, const std::vector<std::string>& args, std::string& stdout_text, std::string& stderr_text) {
        if (args.empty())
            return -1;
        // inheritable security attributes for the pipe write ends
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE out_read = nullptr;
        HANDLE out_write = nullptr;
        HANDLE err_read = nullptr;
        HANDLE err_write = nullptr;
        if (!CreatePipe(&out_read, &out_write, &sa, 0) || !CreatePipe(&err_read, &err_write, &sa, 0)) {
            if (out_read)
                CloseHandle(out_read);
            if (out_write)
                CloseHandle(out_write);
            if (err_read)
                CloseHandle(err_read);
            if (err_write)
                CloseHandle(err_write);
            return -1;
        }
        // keep the parent pipe read ends out of the child process
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
        // detect batch files — CreateProcessW cannot launch them directly without
        // cmd.exe, and the auto-detect path mangles quoting via cmd.exe /S /C.
        const bool is_batch = [&] {
            if (args.empty())
                return false;
            const auto ext = std::filesystem::path(args[0]).extension().wstring();
            return ext == L".bat" || ext == L".BAT" || ext == L".cmd" || ext == L".CMD";
        }();
        // build quoted command line
        std::wstring command_app;
        std::wstring command_line;
        if (is_batch) {
            wchar_t sysdir[MAX_PATH];
            GetSystemDirectoryW(sysdir, MAX_PATH);
            command_app = std::wstring(sysdir) + L"\\cmd.exe";
            // cmd.exe requires the complete batch invocation to be one quoted command string
            command_line = L"/d /s /c \"";
            for (const auto& arg : args) {
                std::wstring wide_arg = std::filesystem::path(arg).wstring();
                // separate each quoted batch argument
                if (command_line.size() > 11)
                    command_line += L" ";
                command_line += L"\"" + wide_arg + L"\"";
            }
            // close the command string around the quoted batch invocation
            command_line += L"\"";
        }
        else {
            for (const auto& arg : args) {
                std::wstring wide_arg = std::filesystem::path(arg).wstring();
                if (!command_line.empty())
                    command_line += L" ";
                command_line += L"\"" + wide_arg + L"\"";
            }
        }
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = out_write;
        si.hStdError = err_write;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        const std::wstring cwd_wide = command_cwd.empty() ? std::wstring() : command_cwd.wstring();
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(command_app.empty() ? nullptr : command_app.c_str(), command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd_wide.empty() ? nullptr : cwd_wide.c_str(), &si, &pi)) {
            CloseHandle(out_read);
            CloseHandle(out_write);
            CloseHandle(err_read);
            CloseHandle(err_write);
            return -1;
        }
        CloseHandle(out_write);
        CloseHandle(err_write);
        // read stdout and stderr in parallel threads to avoid pipe deadlocks
        std::thread thread_out([&stdout_text, &out_read] { stdout_text = read_all_handle(out_read); });
        std::thread thread_err([&stderr_text, &err_read] { stderr_text = read_all_handle(err_read); });
        thread_out.join();
        thread_err.join();
        CloseHandle(out_read);
        CloseHandle(err_read);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exit_code = 1;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return static_cast<int>(exit_code);
    }
#endif
} // namespace

KicadCliNetlistSource::KicadCliNetlistSource(std::filesystem::path project_dir, std::string kicad_cli_path) :
    m_project_dir(std::move(project_dir)), m_kicad_cli_path(std::move(kicad_cli_path)) {
    // resolve the schematic path for this project up front
    m_schematic_path = resolve_schematic_path(m_project_dir);
}

[[nodiscard]] std::string KicadCliNetlistSource::title() const { return m_schematic_path.filename().string(); }

[[nodiscard]] bool KicadCliNetlistSource::is_read_only() const { return true; }

bool KicadCliNetlistSource::has_backing_file() const {
    // the schematic backs the netlist; saving is disabled in plugin mode
    return true;
}

[[nodiscard]] std::filesystem::path KicadCliNetlistSource::working_directory() const { return m_project_dir; }

std::tuple<bool, std::string> KicadCliNetlistSource::load_netlist() {
    // check if the schematic changed since the last export
    const auto last_modified = std::filesystem::last_write_time(m_schematic_path);
    // re-export when never exported or when the schematic changed
    const bool needs_export = !m_has_exported || last_modified != m_last_export_time;
    // return the cached content when the schematic is unchanged
    if (!needs_export) {
        // exit with no re-export performed
        return {false, m_cached_netlist};
    }
    // log information
    spdlog::info("Exporting schematic netlist from {}", m_schematic_path.string());
    // create a temporary output file path for the exported netlist
    const std::filesystem::path output_path = create_temp_netlist_path();
    // run the export through the kicad-cli binary
    std::string stdout_text;
    std::string stderr_text;
    // build the export command arguments
    const std::vector<std::string> args = {m_kicad_cli_path, "sch", "export", "netlist", "--format", "spice", "--output", output_path.string(), m_schematic_path.string()};
    // execute the export
    const int exit_code = run_kicad_cli(m_project_dir, args, stdout_text, stderr_text);
    // fail when the export did not complete successfully
    if (exit_code != 0) {
        // log the failure with captured output
        spdlog::error("kicad-cli netlist export failed ({}): {}", exit_code, stderr_text.empty() ? stdout_text : stderr_text);
        // remove the temporary file
        std::error_code ec;
        std::filesystem::remove(output_path, ec);
        // throw with the process output diagnostics
        throw std::runtime_error("failed to export schematic netlist with kicad-cli: " + (stderr_text.empty() ? stdout_text : stderr_text));
    }
    // read the exported netlist content
    std::ifstream file(output_path, std::ios::in);
    // fail when the exported file could not be opened
    if (!file.is_open()) {
        // throw with a descriptive error
        throw std::runtime_error("failed to open the exported netlist file");
    }
    // read the exported content
    std::string netlist;
    netlist.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // close the file
    file.close();
    // remove the temporary file
    std::error_code ec;
    std::filesystem::remove(output_path, ec);
    // cache the exported content
    m_cached_netlist = netlist;
    // store the export time for change detection
    m_last_export_time = last_modified;
    m_has_exported = true;
    // indicate a fresh export was performed
    return {true, std::move(netlist)};
}

void KicadCliNetlistSource::save_netlist(const std::string&) {}

std::filesystem::path KicadCliNetlistSource::resolve_schematic_path(const std::filesystem::path& project_dir) {
    // error code for project file lookup
    std::error_code error_code;
    // find the .kicad_pro file to derive the true project name
    for (const auto& entry : std::filesystem::directory_iterator(project_dir, error_code)) {
        // skip non-project files
        if (entry.path().extension() != ".kicad_pro")
            continue;
        // schematic file with the same stem is the root schematic
        const auto root_schematic = project_dir / (entry.path().stem().string() + ".kicad_sch");
        // use the root schematic when it exists
        if (std::filesystem::exists(root_schematic))
            return root_schematic;
    }
    // iterate over the project directory
    for (const auto& entry : std::filesystem::directory_iterator(project_dir, error_code)) {
        // skip directories
        if (entry.is_directory(error_code))
            continue;
        // only consider schematic files
        if (entry.path().extension() != ".kicad_sch")
            continue;
        // use the first schematic found as a fallback when no root schematic exists
        return entry.path();
    }
    // no schematic found
    throw std::runtime_error("no schematic file (.kicad_sch) found inside " + project_dir.string());
}

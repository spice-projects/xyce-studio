#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "temp_output_directory.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    // live run directories, used to find the owner of an explicitly opened user file
    std::vector<std::weak_ptr<TempOutputDirectory>>& registry() {
        // one process-wide registry shared by every run directory
        static std::vector<std::weak_ptr<TempOutputDirectory>> directories;
        // return the registry
        return directories;
    }

    // mutex guarding the live run directory registry
    std::mutex& registry_mutex() {
        // one process-wide mutex shared by every run directory
        static std::mutex mutex;
        // return the mutex
        return mutex;
    }

    // process identifier used to keep run directory names unique across instances
    long long process_id() {
#if defined(_WIN32)
        // Windows process identifier
        return static_cast<long long>(GetCurrentProcessId());
#else
        // POSIX process identifier
        return static_cast<long long>(getpid());
#endif
    }

    // canonical path text used for directory containment checks
    std::string normalized_path(const std::filesystem::path& path) {
        // resolve dot components and symlinks, allowing a missing final component
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(path, ec);
        // reject an unresolvable path
        if (ec)
            return {};
        // use forward separators so the prefix check is platform independent
        auto text = canonical.generic_string();
#if defined(_WIN32) || defined(__APPLE__)
        // Windows and the default macOS filesystems compare paths case-insensitively
        std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
#endif
        // return the normalized text
        return text;
    }
} // namespace

TempOutputDirectory::TempOutputDirectory(std::filesystem::path path) :
    m_path(std::move(path)) {}

TempOutputDirectory::~TempOutputDirectory() {
    // read the pin state under the registry lock
    bool pinned = false;
    {
        // protect the pin flag against an explicit user open racing destruction
        std::scoped_lock lock(registry_mutex());
        // copy the pin state
        pinned = m_pinned;
    }
    // an explicitly opened user file owns the directory for the rest of the process
    if (pinned) {
        // keep every file of the pinned run available to the user
        spdlog::debug("Temporary run directory retained: {}", m_path.string());
        // exit without deleting
        return;
    }
    // make the single deletion attempt of this run directory
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
    // ignore a failed attempt, another process may still have one of the files open
    if (ec)
        spdlog::debug("Temporary run directory retained: {} ({})", m_path.string(), ec.message());
}

std::shared_ptr<TempOutputDirectory> TempOutputDirectory::create() {
    // resolve the OS temporary directory
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec);
    // reject an unavailable temporary directory
    if (ec)
        return nullptr;
    // make the run directory path absolute before Xyce changes its working directory
    const auto temp_root = std::filesystem::absolute(root, ec);
    // reject an unresolvable temporary root
    if (ec)
        return nullptr;
    // monotonic run sequence shared by every directory created in this process
    static std::atomic<uint64_t> counter{0};
    // clock value keeping names unique across process restarts
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    // build the unique run directory name
    const auto name = "xyce-studio-" + std::to_string(process_id()) + "-" + std::to_string(now) + "-" + std::to_string(counter.fetch_add(1));
    // build the full run directory path
    const auto path = temp_root / name;
    // create the run directory
    if (!std::filesystem::create_directory(path, ec) || ec)
        return nullptr;
#if !defined(_WIN32)
    // restrict the run directory to its owner
    std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    // keep the usable directory when the filesystem does not support permissions
    if (ec) {
        // report the unsupported permission change
        spdlog::debug("Temporary run directory permissions left unchanged: {} ({})", path.string(), ec.message());
        // clear the permission error
        ec.clear();
    }
#endif
    // build the shared run directory owner
    auto directory = std::shared_ptr<TempOutputDirectory>(new TempOutputDirectory(path));
    // register the weak owner so an explicit user open can pin it
    {
        // protect the registry
        std::scoped_lock lock(registry_mutex());
        // drop expired entries before appending
        auto& directories = registry();
        // remove every run directory that no longer exists
        std::erase_if(directories, [](const auto& entry) { return entry.expired(); });
        // register the new run directory
        directories.push_back(directory);
    }
    // return the run directory owner
    return directory;
}

const std::filesystem::path& TempOutputDirectory::path() const { return m_path; }

std::filesystem::path TempOutputDirectory::netlist_path() const { return m_path / "netlist.cir"; }

bool TempOutputDirectory::write_netlist(std::string_view text) const {
    // open the netlist file for writing
    std::ofstream file(netlist_path(), std::ios::out | std::ios::binary);
    // reject an unopenable netlist file
    if (!file.is_open())
        return false;
    // write the complete netlist text
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    // flush and close so a full disk is reported here
    file.close();
    // report whether every byte reached the file
    return !file.fail();
}

bool TempOutputDirectory::owns(const std::filesystem::path& path) const {
    // normalize both paths for a stable prefix comparison
    const auto directory = normalized_path(m_path);
    const auto candidate = normalized_path(path);
    // reject an unresolvable path
    if (directory.empty() || candidate.empty())
        return false;
    // reject the run directory itself and any path outside it
    if (candidate.size() <= directory.size() || candidate.compare(0, directory.size(), directory) != 0)
        return false;
    // require a path separator after the directory prefix
    return candidate[directory.size()] == '/';
}

void TempOutputDirectory::pin() {
    // protect the pin flag
    std::scoped_lock lock(registry_mutex());
    // keep the directory for the rest of the process
    m_pinned = true;
}

void TempOutputDirectory::pin_path(const std::filesystem::path& path) {
    // collect the matching run directories under the registry lock
    std::vector<std::shared_ptr<TempOutputDirectory>> matches;
    {
        // protect the registry while resolving the owning run directory
        std::scoped_lock lock(registry_mutex());
        // iterate every live run directory
        auto& directories = registry();
        // walk the registry and drop expired entries
        for (auto it = directories.begin(); it != directories.end();) {
            // lock the current run directory
            const auto directory = it->lock();
            // drop a run directory that already exited
            if (!directory) {
                // erase the expired entry
                it = directories.erase(it);
                // continue with the next entry
                continue;
            }
            // remember the directory when it contains the explicitly opened file
            if (directory->owns(path))
                matches.push_back(directory);
            // continue with the next entry
            ++it;
        }
    }
    // pin every matching directory without holding the registry lock
    for (const auto& directory : matches)
        directory->pin();
}

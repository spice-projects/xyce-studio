#pragma once

#include <cstddef>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

class MappedFile
{
public:
    MappedFile() = default;

    explicit MappedFile(const std::filesystem::path& path);

    ~MappedFile() noexcept;

    MappedFile(const MappedFile&) = delete;

    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept;

    MappedFile& operator=(MappedFile&& other) noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] const char* data() const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    void prefetch() const noexcept;

private:
    void release() noexcept;

    void reset() noexcept;

#ifdef _WIN32
    HANDLE m_file_handle = INVALID_HANDLE_VALUE;
    HANDLE m_mapping_handle = nullptr;
#else
    int m_fd = -1;
#endif

    char* m_data = nullptr;
    std::size_t m_size = 0;
};

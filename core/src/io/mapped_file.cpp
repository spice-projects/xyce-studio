#include "mapped_file.h"

#ifdef _WIN32
namespace
{
    std::size_t get_file_size(HANDLE file_handle) {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file_handle, &size)) {
            // return zero size on error
            return 0;
        }
        return static_cast<std::size_t>(size.QuadPart);
    }
} // namespace
#endif

MappedFile::MappedFile(const std::filesystem::path& path) {
#ifdef _WIN32
    // open file for read-only access
    m_file_handle = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_file_handle == INVALID_HANDLE_VALUE) {
        // exit
        return;
    }
    // get file size
    m_size = get_file_size(m_file_handle);
    if (m_size == 0) {
        // close file handle
        CloseHandle(m_file_handle);
        // reset state
        m_file_handle = INVALID_HANDLE_VALUE;
        // exit
        return;
    }
    // create mapping object
    m_mapping_handle = CreateFileMappingW(m_file_handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m_mapping_handle) {
        // close file handle
        CloseHandle(m_file_handle);
        // reset state
        m_file_handle = INVALID_HANDLE_VALUE;
        // exit
        return;
    }
    // map view of the file
    m_data = static_cast<char*>(MapViewOfFile(m_mapping_handle, FILE_MAP_READ, 0, 0, m_size));
    if (!m_data) {
        // close mapping object
        CloseHandle(m_mapping_handle);
        // close file handle
        CloseHandle(m_file_handle);
        // reset state
        m_file_handle = INVALID_HANDLE_VALUE;
        m_mapping_handle = nullptr;
        // exit
        return;
    }
#else
    // open file descriptor
    m_fd = open(path.c_str(), O_RDONLY);
    if (m_fd < 0)
        return;
    // get file size
    struct stat file_info{};
    if (fstat(m_fd, &file_info) == -1) {
        // close file descriptor
        close(m_fd);
        // reset state
        m_fd = -1;
        // exit
        return;
    }
    // assign file size
    m_size = static_cast<std::size_t>(file_info.st_size);
    if (m_size == 0) {
        // close file descriptor
        close(m_fd);
        // reset state
        m_fd = -1;
        // exit
        return;
    }
    // map into memory
    m_data = reinterpret_cast<char*>(mmap(nullptr, m_size, PROT_READ, MAP_SHARED, m_fd, 0));
    if (m_data == MAP_FAILED) {
        // close file descriptor
        close(m_fd);
        // reset state
        m_fd = -1;
        // exit
        return;
    }
#endif
}

MappedFile::~MappedFile() noexcept { release(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
#ifdef _WIN32
    :
    m_file_handle(other.m_file_handle),
    m_mapping_handle(other.m_mapping_handle),
    m_data(other.m_data),
    m_size(other.m_size)
#else
    :
    m_fd(other.m_fd),
    m_data(other.m_data),
    m_size(other.m_size)
#endif
{
    // transfer ownership
    other.reset();
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        // release current mapping
        release();
#ifdef _WIN32
        m_file_handle = other.m_file_handle;
        m_mapping_handle = other.m_mapping_handle;
        m_data = other.m_data;
#else
        m_fd = other.m_fd;
        m_data = other.m_data;
#endif
        m_size = other.m_size;
        // transfer ownership
        other.reset();
    }
    return *this;
}

bool MappedFile::is_valid() const noexcept {
    // check valid data pointer
    return m_data != nullptr;
}

const char* MappedFile::data() const noexcept {
    // return memory pointer
    return static_cast<const char*>(m_data);
}

std::size_t MappedFile::size() const noexcept {
    // return mapped size
    return m_size;
}

void MappedFile::prefetch() const noexcept {
    // check mapping
    if (!m_data || m_size == 0)
        return;
#ifdef _WIN32
    // prefetch the mapped region into RAM
    WIN32_MEMORY_RANGE_ENTRY entry;
    entry.VirtualAddress = m_data;
    entry.NumberOfBytes = m_size;
    PrefetchVirtualMemory(GetCurrentProcess(), 1, &entry, 0);
#else
    // hint the kernel to load the mapped file into RAM ahead of use
    madvise(m_data, m_size, MADV_WILLNEED);
#endif
}

void MappedFile::release() noexcept {
#ifdef _WIN32
    if (m_data) {
        // unmap view
        UnmapViewOfFile(m_data);
        // reset state
        m_data = nullptr;
    }
    if (m_mapping_handle) {
        // close mapping object
        CloseHandle(m_mapping_handle);
        // reset state
        m_mapping_handle = nullptr;
    }
    if (m_file_handle != INVALID_HANDLE_VALUE) {
        // close file handle
        CloseHandle(m_file_handle);
        // reset state
        m_file_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (m_data) {
        // unmap memory
        munmap(m_data, m_size);
        // reset state
        m_data = nullptr;
    }
    if (m_fd >= 0) {
        // close file descriptor
        close(m_fd);
        // reset state
        m_fd = -1;
    }
#endif
    m_size = 0;
}

void MappedFile::reset() noexcept {
#ifdef _WIN32
    // reset state
    m_data = nullptr;
    m_mapping_handle = nullptr;
    m_file_handle = INVALID_HANDLE_VALUE;
#else
    // reset state
    m_data = nullptr;
    m_fd = -1;
#endif
    m_size = 0;
}

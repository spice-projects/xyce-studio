#include <limits>

#include <windows.h>

#include "clipboard.h"

namespace
{
    // manage the lifetime of an open Windows clipboard
    class ClipboardSession
    {
    public:
        // open the clipboard for the current process
        ClipboardSession() :
            m_is_open(OpenClipboard(nullptr) != FALSE) {}

        // close the clipboard when the session leaves scope
        ~ClipboardSession() {
            // close the clipboard only when opening it succeeded
            if (m_is_open)
                CloseClipboard();
        }

        // prevent multiple owners of the clipboard session
        ClipboardSession(const ClipboardSession&) = delete;
        // prevent assigning clipboard sessions
        ClipboardSession& operator=(const ClipboardSession&) = delete;

        // report whether the clipboard was opened
        [[nodiscard]] bool is_open() const { return m_is_open; }

    private:
        bool m_is_open;
    };

    // manage movable global memory until ownership reaches the clipboard
    class GlobalMemory
    {
    public:
        // allocate movable memory for clipboard data
        explicit GlobalMemory(SIZE_T size) :
            m_handle(GlobalAlloc(GMEM_MOVEABLE, size)) {}

        // release memory that was not transferred to the clipboard
        ~GlobalMemory() {
            // free the allocation only while this wrapper owns it
            if (m_handle != nullptr)
                GlobalFree(m_handle);
        }

        // prevent multiple owners of the global memory
        GlobalMemory(const GlobalMemory&) = delete;
        // prevent assigning global memory wrappers
        GlobalMemory& operator=(const GlobalMemory&) = delete;

        // expose the handle for Windows API calls
        [[nodiscard]] HGLOBAL get() const { return m_handle; }

        // transfer ownership of the handle to the caller
        [[nodiscard]] HGLOBAL release() {
            // preserve the handle before clearing this wrapper's ownership
            HGLOBAL handle = m_handle;
            // mark the allocation as transferred
            m_handle = nullptr;
            // return the transferred handle
            return handle;
        }

    private:
        HGLOBAL m_handle;
    };
} // namespace

void copy_to_clipboard(const std::string& text) {
    // acquire the clipboard with automatic release
    ClipboardSession clipboard;
    // stop when another process owns the clipboard
    if (!clipboard.is_open())
        return;

    // remove the previous clipboard contents
    if (!EmptyClipboard())
        return;

    // reject input that cannot be represented by the Win32 conversion API
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return;

    // convert the source byte count to the API's signed length type
    const int source_length = static_cast<int>(text.size());
    // determine the UTF-16 buffer length required by the source text
    const int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), source_length, nullptr, 0);
    // preserve the macOS behavior for invalid non-empty UTF-8 input
    if (wide_length == 0 && !text.empty())
        return;

    // allocate space for the UTF-16 text and its null terminator
    GlobalMemory clipboard_data((static_cast<SIZE_T>(wide_length) + 1) * sizeof(wchar_t));
    // stop when the system cannot allocate clipboard memory
    if (clipboard_data.get() == nullptr)
        return;

    // lock the global memory while writing the UTF-16 payload
    auto* clipboard_text = static_cast<wchar_t*>(GlobalLock(clipboard_data.get()));
    // stop when the allocation cannot be locked
    if (clipboard_text == nullptr)
        return;

    // write the UTF-16 text into the allocated clipboard buffer
    const int converted_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), source_length, clipboard_text, wide_length);
    // release the lock and discard the allocation when conversion changes length
    if (converted_length != wide_length) {
        // unlock the allocation before its RAII wrapper frees it
        GlobalUnlock(clipboard_data.get());
        // exit
        return;
    }

    // terminate the clipboard text for CF_UNICODETEXT consumers
    clipboard_text[converted_length] = L'\0';
    // unlock the completed clipboard payload
    GlobalUnlock(clipboard_data.get());

    // transfer ownership because SetClipboardData owns successful payloads
    SetClipboardData(CF_UNICODETEXT, clipboard_data.release());
}

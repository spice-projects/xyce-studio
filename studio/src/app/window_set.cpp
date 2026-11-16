#include "app/window_set.h"

#include <utility>

studio::WindowSet::WindowSet(Deferral defer) :
    m_defer(std::move(defer)) {}

void studio::WindowSet::add(std::unique_ptr<ShellWindow> window) {
    // an empty slot names no shell
    if (window == nullptr)
        return;
    // the shell outlives this call
    m_windows.push_back(std::move(window));
}

void studio::WindowSet::route_close(const albor::WindowHandle closed) {
    // every shell is asked whether the handle names its window
    for (const std::unique_ptr<ShellWindow>& window : m_windows) {
        // a shell that owned the handle is released after this dispatch
        if (window != nullptr && window->on_window_closed(closed)) {
            // the release waits for the close dispatch to return
            m_defer([this] { release_closed(); });
            // the handle belongs to one shell only
            return;
        }
    }
}

void studio::WindowSet::release_closed() {
    // the closed shells waiting for their release
    std::vector<std::unique_ptr<ShellWindow>> released;
    // a shell is taken out of the list before its destructor can report another close
    for (std::unique_ptr<ShellWindow>& window : m_windows) {
        // a closed shell owns no window any more
        if (window != nullptr && !window->alive())
            released.push_back(std::move(window));
    }
    // the list keeps the live shells in their order
    std::erase_if(m_windows, [](const std::unique_ptr<ShellWindow>& window) { return window == nullptr; });
}

std::size_t studio::WindowSet::size() const { return m_windows.size(); }

studio::ShellWindow* studio::WindowSet::at(const std::size_t index) const {
    // an index past the end names no shell
    if (index >= m_windows.size())
        return nullptr;
    // the shell the index names
    return m_windows[index].get();
}

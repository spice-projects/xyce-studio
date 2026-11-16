#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include "albor/window/window_handle.h"
#include "app/shell_window.h"

namespace studio
{
    // the shells the application keeps alive, and the deferred release a close dispatch uses
    class WindowSet final
    {
    public:
        // the sink that runs a task after the close dispatch returns
        using Deferral = std::function<void(std::function<void()>)>;

        explicit WindowSet(Deferral defer);

        WindowSet(const WindowSet&) = delete;

        WindowSet& operator=(const WindowSet&) = delete;

        // keep a shell alive in the set
        void add(std::unique_ptr<ShellWindow> window);

        // route one closed window to the shell that owns it, releasing it after the dispatch returns
        void route_close(albor::WindowHandle closed);

        // release every shell whose window closed
        void release_closed();

        // hand out how many shells the set keeps
        [[nodiscard]] std::size_t size() const;

        // hand out one shell, or null past the end
        [[nodiscard]] ShellWindow* at(std::size_t index) const;

    private:
        // the sink a close dispatch defers its release to
        Deferral m_defer;

        // the shells the set keeps alive, in the order they were built
        std::vector<std::unique_ptr<ShellWindow>> m_windows;
    };
} // namespace studio

#pragma once

#include <memory>

#include "albor/window/window_handle.h"

namespace albor
{
    class Application;
    class ThemeController;
} // namespace albor

namespace studio
{
    // install the native theme over a window and hand back the controller that keeps the bindings alive
    [[nodiscard]] std::unique_ptr<albor::ThemeController> install_theme(albor::Application& application, albor::WindowHandle handle);
} // namespace studio

#include "app/theme.h"

#include <memory>

#include "albor/application/application.h"
#include "albor/style/theme.h"
#include "albor/style/theme_builder.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/system_appearance.h"
#include "albor/window/window.h"

namespace
{
    // the light accents, which is the window surface the system shows in light mode
    albor::Accents light_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.925f, 0.925f, 0.930f), .on_surface = albor::Color::rgb(0.18f, 0.18f, 0.20f), .accent = accent, .outline = albor::Color::rgb(0.55f, 0.57f, 0.60f)}; }

    // the dark accents, which is the window surface the system shows in dark mode
    albor::Accents dark_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.125f, 0.125f, 0.135f), .on_surface = albor::Color::rgb(0.92f, 0.94f, 0.96f), .accent = accent, .outline = albor::Color::rgb(0.42f, 0.44f, 0.48f)}; }

    // the preset that reads as the native control set of the platform
    albor::ThemePreset native_preset() {
#if defined(__APPLE__)
        return albor::ThemePreset::Macos;
#elif defined(_WIN32)
        return albor::ThemePreset::Fluent;
#else
        return albor::ThemePreset::Derived;
#endif
    }
} // namespace

std::unique_ptr<albor::ThemeController> studio::install_theme(albor::Application& application, albor::WindowHandle handle) {
    // the window supplies the system appearance the theme follows
    albor::Window* window = application.window(handle);
    // the tree is where the theme installs its bindings
    albor::UiNode* root = application.root(handle);
    // a closed window or a refused tree has nothing to theme
    if (window == nullptr || root == nullptr)
        return nullptr;
    // read the appearance the system currently reports
    const albor::SystemAppearance appearance = window->system_appearance();
    // build both schemes from the system accent through the native preset
    auto controller = std::make_unique<albor::ThemeController>(light_accents(appearance.accent), dark_accents(appearance.accent), native_preset());
    // install the reported scheme over the window's tree
    controller->install(*root, appearance.scheme);
    // clear the native surface with the same colour the tree draws on
    application.set_window_background(handle, controller->theme().surface());
    // hand the controller back so the caller keeps the bindings alive
    return controller;
}

#pragma once

#include <functional>
#include <memory>

#include <slint.h>

#include <main_window.h>

#include "charts_renderer.h"
#include "core/step_information.h"
#include "main_window_view_def.h"

namespace step_tool_dialog_view
{
    // owns the inline step tool panel rendered within the main window (replacing
    // the separate slint Dialog window). The panel is an inline child of the
    // MainWindow, so all interaction goes through the window's properties and
    // callbacks — no separate OS window and no modal_manager input blocking is
    // needed. The step selection is applied back to the chart through the
    // renderer. The view must be kept alive for the lifetime of the view, so
    // it lives in its own translation unit (the generated main_window header
    // defines a SharedGlobals type that conflicts with the other dialog headers
    // in the same compilation unit).
    class StepToolDialogView
    {
    public:
        explicit StepToolDialogView(slint::ComponentHandle<main_window::MainWindow> main_window, ChartsRenderer& renderer);

        StepToolDialogView(const StepToolDialogView&) = delete;
        StepToolDialogView& operator=(const StepToolDialogView&) = delete;

        ~StepToolDialogView();

        // shows the panel for the chart at the given index, pre-checking the
        // chart's selected steps. The on_closed callback is invoked on both
        // accept and cancel, after the panel is hidden, so the caller can
        // release the modal state.
        void show_for_chart(size_t chart_index, const std::function<void()>& on_closed);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace step_tool_dialog_view

#pragma once

#include <functional>
#include <memory>

#include <slint.h>

#include <main_window.h>

#include "charts_renderer.h"
#include "dsp/fft.h"
#include "expression/expression.h"
#include "main_window_view_def.h"

namespace fft_dialog_view
{
    // owns the inline FFT setup panel rendered within the main window (replacing
    // the separate slint Dialog window). The panel is an inline child of the
    // MainWindow, so all interaction goes through the window's properties and
    // callbacks — no separate OS window and no modal_manager input blocking is
    // needed. The view must be kept alive for the lifetime of the presenter, so
    // it lives in its own translation unit (the generated main_window header
    // defines a SharedGlobals type that conflicts with the other dialog headers
    // in the same compilation unit).
    class FftDialogView
    {
    public:
        explicit FftDialogView(slint::ComponentHandle<main_window::MainWindow> main_window, ChartsRenderer& renderer);

        FftDialogView(const FftDialogView&) = delete;
        FftDialogView& operator=(const FftDialogView&) = delete;

        ~FftDialogView();

        // shows the panel for the chart at the given index, pre-selecting the
        // chart's plotted expressions. The on_closed callback is invoked on
        // both accept and cancel, after the panel is hidden, so the caller can
        // release the modal state; on accept the selected expressions and FFT
        // parameters are delivered through the event handler.
        void show_for_chart(size_t chart_index, MainWindowViewDefEvents& handler, const std::function<void()>& on_closed);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace fft_dialog_view

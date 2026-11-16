#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "charts/plot_selection.h"
#include "dsp/fft.h"

namespace studio
{
    // the largest transform the presenter runs, because a larger one exhausts memory before it finishes
    inline constexpr std::size_t MAX_FFT_POINTS = 1U << 20U;

    // the expressions and range the FFT dialog opens with
    struct FftSelectionState
    {
        // the chart the dialog was opened from
        std::size_t chart_index = 0;

        // the dataset that owns the chart, which the accept checks before applying
        int dataset_id = 0;

        // the real expressions the dataset carries, preselected when the chart plots them
        std::vector<PlotExpression> expressions;

        // the dataset abscissa range
        double all_from = 0.0;

        double all_to = 0.0;

        // the chart's visible abscissa range, which the current zoom preset names
        double zoom_from = 0.0;

        double zoom_to = 0.0;

        // whether the chart is zoomed away from the dataset range
        bool has_zoom = false;

        // the default transform length
        std::size_t points = 1024;
    };

    // the transform the dialog accepted
    struct FftRequest
    {
        // the expression names the transform reads
        std::vector<std::string> expressions;

        // the abscissa interval the transform covers
        double from = 0.0;

        double to = 0.0;

        // the uniform grid size and transform length
        std::size_t points = 1024;

        // the window, output format and output presentation
        fft::WindowFunction window = fft::WindowFunction::HANNING;

        fft::FftFormat format = fft::FftFormat::NORM;

        fft::FftOutput output = fft::FftOutput::MAGNITUDE;

        // whether the per-signal mean stays in the spectrum
        bool keep_dc = true;
    };
} // namespace studio

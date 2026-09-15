#pragma once

#include "chart_engine.h"

// chart palette matching the slint cupertino widgets, consumed by the slint
// native chart view through the ChartFrameData colors

// palette roles consumed by the chart render paths
struct ChartPalette
{
    ChartColor text;

    ChartColor muted;

    ChartColor background;

    ChartColor panel;

    ChartColor border;

    ChartColor grid;
};

// light theme palette (macOS Cupertino)
inline const ChartPalette CHART_PALETTE_LIGHT = {
    {0.12f, 0.12f, 0.14f, 1.00f}, // text
    {0.55f, 0.55f, 0.58f, 1.00f}, // muted
    {0.93f, 0.93f, 0.93f, 1.00f}, // background
    {1.00f, 1.00f, 1.00f, 1.00f}, // panel
    {0.82f, 0.82f, 0.84f, 1.00f}, // border
    {0.91f, 0.91f, 0.93f, 1.00f}, // grid
};

// dark theme palette (macOS Cupertino)
inline const ChartPalette CHART_PALETTE_DARK = {
    {0.92f, 0.92f, 0.94f, 1.00f}, // text
    {0.60f, 0.60f, 0.64f, 1.00f}, // muted
    {0.12f, 0.12f, 0.12f, 1.00f}, // background
    {0.17f, 0.17f, 0.18f, 1.00f}, // panel
    {0.28f, 0.28f, 0.30f, 1.00f}, // border
    {0.22f, 0.22f, 0.24f, 1.00f}, // grid
};

// palette of the active theme
inline const ChartPalette& chart_palette(bool is_dark) { return is_dark ? CHART_PALETTE_DARK : CHART_PALETTE_LIGHT; }

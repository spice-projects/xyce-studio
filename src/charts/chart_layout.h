#pragma once

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "chart_engine.h"

// one axis tick: data value, pixel position inside the plot rect, grid/mark
// classification and the formatted label text
struct ChartTick
{
    // abscissa or ordinate value in data space
    double value = 0.0;

    // pixel position along the axis inside the plot rect
    float pixel_pos = 0.0f;

    // major ticks draw unconditionally; minor ticks are density gated
    bool major = false;

    // label visibility after the locator density prune
    bool show_label = false;

    // formatted label text and measured width in logical px
    std::string label;

    float label_width = 0.0f;
};

// one grid line at a pixel position along the owning axis
struct ChartGridLine
{
    float pixel_pos = 0.0f;

    bool major = false;

    // minor grid alpha includes the density gate (0 means not drawn)
    float alpha = 0.0f;
};

// y axis geometry and ticks of one chart
struct ChartAxisFrame
{
    bool enabled = false;

    // opposite axes render on the right side of the plot
    bool opposite = false;

    // axis datum line position; tick marks and labels are placed from here
    float datum = 0.0f;

    std::vector<ChartTick> ticks;

    std::vector<ChartGridLine> grid_lines;
};

// one polyline point in pixel coordinates relative to the chart top-left
struct ChartPoint
{
    float x = 0.0f;

    float y = 0.0f;
};

// one polyline run: one series on one step
struct ChartSeriesFrame
{
    std::string name;

    ChartColor color;

    int axis = 0;

    size_t step = 0;

    std::vector<ChartPoint> points;
};

// one legend entry
struct ChartLegendItem
{
    std::string name;

    ChartColor color;
};

// pixel-space rendering data for one chart filling a canvas area, produced by
// ChartLayout and consumed by the slint native chart view
struct ChartFrame
{
    // frame (full chart area) and plot rect in logical px, relative to the
    // chart top-left
    float frame_x = 0.0f;

    float frame_y = 0.0f;

    float frame_w = 0.0f;

    float frame_h = 0.0f;

    float plot_x = 0.0f;

    float plot_y = 0.0f;

    float plot_w = 0.0f;

    float plot_h = 0.0f;

    // x axis datum line position (tick marks and labels start here)
    float x_datum = 0.0f;

    std::vector<ChartTick> x_ticks;

    std::vector<ChartGridLine> x_grid;

    std::array<ChartAxisFrame, 3> y_axes;

    std::vector<ChartLegendItem> legend;

    // legend block geometry in logical px
    float legend_x = 0.0f;

    float legend_y = 0.0f;

    float legend_w = 0.0f;

    float legend_h = 0.0f;

    std::vector<ChartSeriesFrame> series;
};

// abscissa axis limits clamped above zero for logarithmic scales
std::pair<double, double> clamped_abscissa_limits(const ChartEngine& engine);

// builds chart frames from engine state: the locator, padding and transform
// algorithms are ported from implot v1.0 so the layout keeps the established
// geometry
class ChartLayout
{
public:
    // measures the rendered width of one single-line text in logical px
    using TextMeasurer = std::function<float(const std::string&)>;

    explicit ChartLayout(TextMeasurer measure);

    // frame for one chart filling the given logical canvas size
    ChartFrame build(const ChartEngine& engine, float width, float height) const;

private:
    TextMeasurer m_measure;
};

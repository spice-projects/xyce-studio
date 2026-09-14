#pragma once

// layout metrics shared by every chart render path; the values mirror the
// implot style applied by apply_slint_style (src/ui/charts_renderer.cpp) and
// the implot defaults, so the implot and slint views cannot drift apart

// logical text line height of the charts font (inter regular, 14 px)
inline constexpr float CHART_TEXT_HEIGHT = 14.0f;

// padding between the chart frame and the canvas area
inline constexpr float CHART_PLOT_PADDING = 10.0f;

// padding between tick labels and the axis datum line
inline constexpr float CHART_LABEL_PADDING = 6.0f;

// padding between the chart frame and the legend block
inline constexpr float CHART_LEGEND_PADDING_Y = 6.0f;

// padding around the legend content inside the legend block
inline constexpr float CHART_LEGEND_INNER_PADDING_X = 8.0f;

inline constexpr float CHART_LEGEND_INNER_PADDING_Y = 4.0f;

// spacing between legend entries
inline constexpr float CHART_LEGEND_SPACING_X = 12.0f;

inline constexpr float CHART_LEGEND_SPACING_Y = 4.0f;

// outward tick mark lengths on the axis datum lines
inline constexpr float CHART_MAJOR_TICK_LEN = 4.0f;

inline constexpr float CHART_MINOR_TICK_LEN = 2.0f;

// grid line thickness
inline constexpr float CHART_MAJOR_GRID_SIZE = 1.0f;

inline constexpr float CHART_MINOR_GRID_SIZE = 0.5f;

// opacity multiplier applied to minor grid lines
inline constexpr float CHART_MINOR_ALPHA = 0.35f;

// plotted series line width
inline constexpr float CHART_LINE_WEIGHT = 2.0f;

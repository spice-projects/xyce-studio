#pragma once

// layout metrics of the chart layout engine; the values mirror the implot
// style that defined the chart appearance before the implot engine was
// retired, so the charts keep their established look

// logical text line height of the charts font (inter regular, 14 px)
inline constexpr float CHART_TEXT_HEIGHT = 14.0f;

// glyph em size of the axis and legend labels: imgui loaded inter at
// CHART_TEXT_HEIGHT but stb scaled glyph metrics by 1/(hhea ascent+descent)
// (2048/2478 for inter), so the rendered em was smaller than the layout line
// height; the slint labels use the same em to keep the established metrics
inline constexpr float CHART_LABEL_FONT_SIZE = 11.56f;

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

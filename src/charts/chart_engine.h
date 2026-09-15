#pragma once

#include <array>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../core/step_information.h"
#include "../expression/expression.h"
#include "../expression/expression_manager.h"
#include "../io/xyce_output_file.h"

// rgba color in 0..1 float components, shared by every chart render path
struct ChartColor
{
    float r = 0.0f;

    float g = 0.0f;

    float b = 0.0f;

    float a = 0.0f;
};

// one plotted series: expression, assigned y axis index (0..2), rendered step
// data views, value range and assigned palette color
using OrdinateSeries = std::tuple<AnyExpression*, int, std::unordered_map<size_t, std::pair<View<double>, View<double>>>, double, double, ChartColor>;

// chart series keyed by expression name so entries stay in deterministic order
using Series = std::map<std::string, OrdinateSeries>;

// default series color palette (Apple / Cupertino inspired)
inline const std::vector<ChartColor> SERIES_COLOR_PALETTE = {
    {0.00f, 0.48f, 1.00f, 1.0f}, // #007AFF Vibrant Blue
    {1.00f, 0.58f, 0.00f, 1.0f}, // #FF9500 Vibrant Orange
    {0.20f, 0.78f, 0.35f, 1.0f}, // #34C759 Vibrant Green
    {0.69f, 0.32f, 0.87f, 1.0f}, // #AF52DE Vibrant Purple
    {1.00f, 0.18f, 0.33f, 1.0f}, // #FF2D55 Vibrant Pink
    {0.19f, 0.69f, 0.78f, 1.0f}, // #30B0C7 Vibrant Teal
    {0.35f, 0.34f, 0.84f, 1.0f}, // #5856D6 Vibrant Indigo
    {1.00f, 0.80f, 0.00f, 1.0f}, // #FFCC00 Vibrant Yellow
    {0.00f, 0.78f, 0.75f, 1.0f}, // #00C7BE Vibrant Mint
    {1.00f, 0.23f, 0.19f, 1.0f} // #FF3B30 Vibrant Coral Red
};

// y axis state of one chart; axis is the engine index 0..2
struct AxisInformation
{
    int axis;

    double plot_min_value;

    double plot_max_value;

    std::string unit;

    int plots;

    double min_value;

    double max_value;
};

// platform-neutral chart state machine; render paths consume this state and
// translate it into their own drawing calls.
// the state is organized so future chart types (e.g. smith charts) can reuse
// the chart-type-agnostic parts (series data model, step selection, color
// assignment, decimation, hover mapping) with their own axis and rendering
// semantics instead of forking the data model
class ChartEngine
{
public:
    ChartEngine() = delete;

    ChartEngine(ExpressionManager* expression_manager, StepInformation const* step_information, AbscissaScale abscissa_scale, size_t decimate_target);

    ChartEngine(const ChartEngine&) = delete;

    ChartEngine& operator=(const ChartEngine&) = delete;

    const std::set<size_t>& selected_steps();

    void set_selected_steps(const std::set<size_t>& steps);

    std::vector<AnyExpression*> selected_expressions();

    void plot_series(const std::set<AnyExpression*>& expressions);

    void auto_range();

    void clear();

    void reset_zoom_window(bool horizontal, bool vertical);

    void update_zoom_window(double x_left_ratio, double x_right_ratio, double y_top_ratio, double y_bottom_ratio);

    void update(ExpressionManager* expression_manager, const StepInformation* step_information, AbscissaScale abscissa_scale);

    void set_decimate_target(size_t decimate_target);

    [[nodiscard]] const std::tuple<double, double, double, double>& zoom_window() const { return m_zoom_window; }

    [[nodiscard]] double ratio_to_abscissa_value(double x_ratio) const;

    [[nodiscard]] double plot_ratio_to_abscissa_value(double x_ratio) const;

    [[nodiscard]] std::string hovered_series_text(double abscissa_value) const;

    [[nodiscard]] const Series& series() const { return m_series; }

    [[nodiscard]] const std::array<AxisInformation, 3>& axes() const { return m_axes; }

    [[nodiscard]] AbscissaScale abscissa_scale() const { return m_abscissa_scale; }

    [[nodiscard]] const std::string& abscissa_unit() const { return m_abscissa_unit; }

    [[nodiscard]] double abscissa_left_value() const { return m_abscissa_left_value; }

    [[nodiscard]] double abscissa_right_value() const { return m_abscissa_right_value; }

    static std::string format_metric(double value, std::string_view unit);

    // compute power-of-two major tick values for a log2-scaled axis
    static std::vector<double> log2_major_ticks(double x_left, double x_right, int max_ticks);

private:
    std::tuple<bool, View<double>, View<double>, double, double> plot_step(Expression<double>& ordinate_variant, size_t step, double min_value, double max_value, double x_right_ratio, double x_left_ratio) const;

    int get_y_axis(const std::string& unit);

    bool release_y_axis(int axis);

    [[nodiscard]] std::pair<size_t, size_t> find_abscissa_indexes(const std::span<const double>& abscissa, double left_value, double right_value) const;

    void redraw_all_series();

    // chart-type-agnostic series state: the plotted data model shared by every
    // render path
    ExpressionManager* m_expression_manager;

    const StepInformation* m_step_information;

    Series m_series;

    std::set<size_t> m_selected_steps = {0};

    size_t m_next_color_index = 0;

    // cartesian xy specific state: rectangular zoom windows and per-unit y
    // axis assignment; future chart types replace these semantics (e.g. fixed
    // +-1 smith plane) while reusing the series state above
    AbscissaScale m_abscissa_scale;

    size_t m_decimate_target;

    std::string m_abscissa_name;

    std::string m_abscissa_unit;

    double m_abscissa_left_value = 0.0;

    double m_abscissa_right_value = 1.0;

    std::tuple<double, double, double, double> m_zoom_window = {-1, -1, -1, -1};

    std::array<AxisInformation, 3> m_axes = {{{0, 0.0, 1.0, "", 0, 0.0, 1.0}, {1, 0.0, 1.0, "", 0, 0.0, 1.0}, {2, 0.0, 1.0, "", 0, 0.0, 1.0}}};
};

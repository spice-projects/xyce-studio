#pragma once

#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "chart_engine.h"

// cartesian xy line chart: owns the shared chart engine state and renders it
// through the implot drawing stack; the public api is unchanged for all hosts
class Chart
{
public:
    Chart() = delete;

    Chart(ExpressionManager* expression_manager, StepInformation const* step_information, AbscissaScale abscissa_scale, size_t decimate_target);

    Chart(const Chart&) = delete;

    Chart& operator=(const Chart&) = delete;

    const std::set<size_t>& selected_steps();

    void set_selected_steps(const std::set<size_t>& steps);

    std::vector<AnyExpression*> selected_expressions();

    void render();

    void plot_series(const std::set<AnyExpression*>& expressions);

    void auto_range();

    void clear();

    void reset_zoom_window(bool horizontal, bool vertical);

    void update_zoom_window(double x_left_ratio, double x_right_ratio, double y_top_ratio, double y_bottom_ratio);

    void update(ExpressionManager* expression_manager, const StepInformation* step_information, AbscissaScale abscissa_scale);

    void set_decimate_target(size_t decimate_target);

    [[nodiscard]] const std::tuple<float, float, float, float>& get_plot_rect() const;

    [[nodiscard]] const std::tuple<double, double, double, double>& zoom_window() const;

    [[nodiscard]] double ratio_to_abscissa_value(double x_ratio) const;

    [[nodiscard]] double plot_ratio_to_abscissa_value(double x_ratio) const;

    [[nodiscard]] std::string hovered_series_text(double abscissa_value) const;

    // platform-neutral engine state; alternative render paths (e.g. the slint
    // native view) consume the same state without touching implot
    [[nodiscard]] const ChartEngine& engine() const;

    static std::string format_metric(double value, std::string_view unit);

private:
    ChartEngine m_engine;

    std::tuple<float, float, float, float> m_plot_rect = {-1.0f, -1.0f, -1.0f, -1.0f};
};

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <set>
#include <tuple>
#include <vector>

#include <imgui.h>
#include <implot.h>
#include <spdlog/spdlog.h>

#include "chart.h"
#include "chart_layout.h"

namespace
{
    constexpr ImPlotFlags PLOT_FLAGS = (ImPlotFlags_CanvasOnly ^ ImPlotFlags_NoLegend) | ImPlotFlags_NoInputs | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;

    // ImPlot forward transform for base-2 logarithmic axes (mirrors ImPlot's Log10 transform clamping)
    static double log2_forward_transform(const double value, void*) {
        // clamp non-positive values to the smallest positive double
        return std::log2(value <= 0.0 ? std::numeric_limits<double>::min() : value);
    }

    // ImPlot inverse transform for base-2 logarithmic axes
    static double exp2_inverse_transform(const double value, void*) {
        // convert plot space value to abscissa value
        return std::exp2(value);
    }

    // ImPlot tick formatter rendering value, space, prefix and unit (SI)
    static int metric_formatter(const double value, char* buff, const int size, const void* data) {
        // unit
        const auto unit = static_cast<const char*>(data);
        // shared formatter from the chart engine
        const std::string formatted = Chart::format_metric(value, unit);
        return snprintf(buff, size, "%s", formatted.c_str());
    }

    // convert an engine color to the implot color type
    static ImVec4 to_imvec(const ChartColor& color) { return ImVec4(color.r, color.g, color.b, color.a); }
} // namespace

std::string Chart::format_metric(const double value, const std::string_view unit) {
    // delegate to the shared engine formatter
    return ChartEngine::format_metric(value, unit);
}

Chart::Chart(ExpressionManager* expression_manager, const StepInformation* step_information, const AbscissaScale abscissa_scale, const size_t decimate_target) :
    m_engine(expression_manager, step_information, abscissa_scale, decimate_target) {}

const std::set<size_t>& Chart::selected_steps() {
    // delegate to the engine
    return m_engine.selected_steps();
}

void Chart::set_selected_steps(const std::set<size_t>& steps) {
    // delegate to the engine
    m_engine.set_selected_steps(steps);
}

std::vector<AnyExpression*> Chart::selected_expressions() {
    // delegate to the engine
    return m_engine.selected_expressions();
}

void Chart::plot_series(const std::set<AnyExpression*>& expressions) {
    // delegate to the engine
    m_engine.plot_series(expressions);
}

void Chart::auto_range() {
    // delegate to the engine
    m_engine.auto_range();
}

void Chart::clear() {
    // delegate to the engine
    m_engine.clear();
}

void Chart::reset_zoom_window(const bool horizontal, const bool vertical) {
    // delegate to the engine
    m_engine.reset_zoom_window(horizontal, vertical);
}

void Chart::update_zoom_window(const double x_left_ratio, const double x_right_ratio, const double y_top_ratio, const double y_bottom_ratio) {
    // delegate to the engine
    m_engine.update_zoom_window(x_left_ratio, x_right_ratio, y_top_ratio, y_bottom_ratio);
}

void Chart::update(ExpressionManager* expression_manager, const StepInformation* step_information, const AbscissaScale abscissa_scale) {
    // delegate to the engine
    m_engine.update(expression_manager, step_information, abscissa_scale);
}

void Chart::set_decimate_target(const size_t decimate_target) {
    // delegate to the engine
    m_engine.set_decimate_target(decimate_target);
}

const std::tuple<double, double, double, double>& Chart::zoom_window() const {
    // delegate to the engine
    return m_engine.zoom_window();
}

double Chart::ratio_to_abscissa_value(const double x_ratio) const {
    // delegate to the engine
    return m_engine.ratio_to_abscissa_value(x_ratio);
}

double Chart::plot_ratio_to_abscissa_value(const double x_ratio) const {
    // delegate to the engine
    return m_engine.plot_ratio_to_abscissa_value(x_ratio);
}

std::string Chart::hovered_series_text(const double abscissa_value) const {
    // delegate to the engine
    return m_engine.hovered_series_text(abscissa_value);
}

const ChartEngine& Chart::engine() const {
    // expose the shared engine state
    return m_engine;
}

const std::tuple<float, float, float, float>& Chart::get_plot_rect() const { return m_plot_rect; }

void Chart::render() {
    // push matching slint colormap
    ImPlot::PushColormap("SlintCupertino");
    // initialize plot, full area
    if (ImPlot::BeginPlot("My First Plot", ImVec2(-1, -1), PLOT_FLAGS)) {
        // x axis
        ImPlot::SetupAxis(ImAxis_X1);
        // format
        ImPlot::SetupAxisFormat(ImAxis_X1, reinterpret_cast<ImPlotFormatter>(metric_formatter), (void*)m_engine.abscissa_unit().c_str());
        // abscissa scale
        if (m_engine.abscissa_scale() == AbscissaScale::DECADE) {
            // log10 abscissa axis
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        }
        else if (m_engine.abscissa_scale() == AbscissaScale::OCTAVE) {
            // log2 abscissa axis
            ImPlot::SetupAxisScale(ImAxis_X1, log2_forward_transform, exp2_inverse_transform);
        }
        // abscissa limits clamped above zero for logarithmic scales
        const auto [x_left_value, x_right_value] = clamped_abscissa_limits(m_engine);
        // min and max values
        ImPlot::SetupAxisLimits(ImAxis_X1, x_left_value, x_right_value, ImPlotCond_Always);
        // log2 custom ticks for OCTAVE scale
        if (m_engine.abscissa_scale() == AbscissaScale::OCTAVE) {
            // estimate plot width from last frame (fallback to 800 pixels)
            const float plot_width = std::get<2>(m_plot_rect) - std::get<0>(m_plot_rect);
            const int max_ticks = std::max(2, static_cast<int>(std::lround((plot_width > 0.0f ? plot_width : 800.0f) * 0.01f)));
            // compute log2 major ticks for the visible abscissa range
            auto log2_ticks = ChartEngine::log2_major_ticks(x_left_value, x_right_value, max_ticks);
            if (!log2_ticks.empty())
                ImPlot::SetupAxisTicks(ImAxis_X1, log2_ticks.data(), static_cast<int>(log2_ticks.size()), nullptr, false);
        }
        // loop axis information
        for (const auto& axis_info : m_engine.axes()) {
            // axis info at i (Y1 is always enabled)
            if (axis_info.axis == 0 || axis_info.plots > 0) {
                // setup axis
                ImPlot::SetupAxis(static_cast<ImAxis>(ImAxis_Y1 + axis_info.axis), nullptr, axis_info.axis != 0 ? ImPlotAxisFlags_Opposite : ImPlotAxisFlags_None);
                // format
                ImPlot::SetupAxisFormat(static_cast<ImAxis>(ImAxis_Y1 + axis_info.axis), reinterpret_cast<ImPlotFormatter>(metric_formatter), (void*)axis_info.unit.c_str());
                // min and max values
                ImPlot::SetupAxisLimits(static_cast<ImAxis>(ImAxis_Y1 + axis_info.axis), axis_info.plot_min_value, axis_info.plot_max_value, ImPlotCond_Always);
            }
        }
        ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
        // finish setup
        ImPlot::SetupFinish();
        // loop series to render
        for (const auto& [name, ordinate_series] : m_engine.series()) {
            // extract axis, steps and color
            const int y_axis = std::get<1>(ordinate_series);
            const auto& steps = std::get<2>(ordinate_series);
            const auto& color = std::get<5>(ordinate_series);
            // set current y axis
            ImPlot::SetAxis(static_cast<ImAxis>(ImAxis_Y1 + y_axis));
            // style
            ImPlotSpec spec;
            spec.LineColor = to_imvec(color);
            spec.LineWeight = 2.0f;
            // loop steps
            for (const auto& [x, y] : steps | std::views::values) {
                // draw the line chart, use data() directly since the data is contiguous in memory
                ImPlot::PlotLine(name.c_str(), x.data(), y.data(), static_cast<int>(x.size()), spec);
            }
        }
        // plot position and size
        ImVec2 plot_position = ImPlot::GetPlotPos();
        ImVec2 plot_size = ImPlot::GetPlotSize();
        // update plot rect
        m_plot_rect = {plot_position.x, plot_position.y, plot_position.x + plot_size.x, plot_position.y + plot_size.y};
        // finalize the plot block
        ImPlot::EndPlot();
    }
    // pop colormap
    ImPlot::PopColormap();
}

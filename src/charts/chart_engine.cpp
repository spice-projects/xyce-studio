#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <ranges>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

#include "chart_engine.h"
#include "decimate.h"

namespace
{
    // linear interpolation of the y value at x over contiguous (x, y) views
    double interpolate_y(const View<double>& x_data, const View<double>& y_data, const double x, const bool ascending) {
        const size_t n = x_data.size();
        if (n == 0)
            return 0.0;
        // clamp to range
        if (ascending) {
            // check if x is lower than the first x value
            if (x <= x_data[0])
                return y_data[0];
            // check if x is greater than the last x value
            if (x >= x_data[n - 1])
                return y_data[n - 1];
        }
        else {
            // check if x is greater than the first x value
            if (x >= x_data[0])
                return y_data[0];
            // check if x is lower than the last x value
            if (x <= x_data[n - 1])
                return y_data[n - 1];
        }
        // index-based binary search for the bracketing interval (stride 1, contiguous data)
        size_t idx;
        {
            // initialize low and high indexes for binary search
            size_t low = 1;
            size_t high = n - 1;
            // binary search loop
            while (low < high) {
                // middle element
                const size_t mid = low + (high - low) / 2;
                // ascending data: first index whose value is >= x, descending data: first index whose value is <= x
                if (ascending ? (x_data[mid] < x) : (x_data[mid] > x))
                    low = mid + 1;
                else
                    high = mid;
            }
            // the first sample at or past x (the bracketing index)
            idx = low;
        }
        // linear interpolation
        const double x0 = x_data[idx - 1];
        const double x1 = x_data[idx];
        const double y0 = y_data[idx - 1];
        const double y1 = y_data[idx];
        const double t = (x - x0) / (x1 - x0);
        return y0 + t * (y1 - y0);
    }

    // scale-aware interpolation of an abscissa value at a window ratio
    double interpolate_abscissa(const double ratio, const double left_value, const double right_value, const AbscissaScale scale) {
        // logarithmic scale: interpolate geometrically over the clamped range;
        // non-positive bounds are clamped like the layout so the mapped
        // interaction values match the drawn logarithmic plot
        if (scale != AbscissaScale::LINEAR) {
            // clamp non-positive bounds into the logarithmic domain
            double lo = std::min(left_value, right_value);
            double hi = std::max(left_value, right_value);
            if (lo <= 0.0)
                lo = hi > 0.0 ? hi / 1e6 : 1.0;
            if (hi <= 0.0)
                hi = lo > 0.0 ? lo * 1e6 : 1.0;
            // avoid a degenerate zero-width range
            if (lo == hi)
                hi = lo * 2.0;
            // a descending sweep maps larger values toward the west edge
            const bool descending = left_value > right_value;
            return lo * std::pow(hi / lo, descending ? 1.0 - ratio : ratio);
        }
        // linear scale: interpolate linearly over the range
        return left_value + ratio * (right_value - left_value);
    }

    // resolve the expressions to plot for one ordinate; complex expressions expand to magnitude and phase
    std::vector<AnyExpression*> get_expressions_to_plot(ExpressionManager* expression_manager, AnyExpression* expression) {
        // nothing to do on double expressions
        if (std::holds_alternative<Expression<double>>(*expression)) {
            // exit
            return {expression};
        }
        // complex expression
        const auto& complex_expression = std::get<Expression<std::complex<double>>>(*expression);
        // magnitude
        auto magnitude_expression = expression_manager->evaluate(std::format("db({})", complex_expression.name()));
        if (!magnitude_expression)
            return {};
        // phase
        auto phase_expression = expression_manager->evaluate(std::format("phase({})", complex_expression.name()));
        if (!phase_expression)
            return {};
        // exit
        return {magnitude_expression, phase_expression};
    }
} // namespace

std::string ChartEngine::format_metric(const double value, const std::string_view unit) {
    // dividers
    static constexpr double v[] = {1e9, 1e6, 1e3, 1.0, 1e-3, 1e-6, 1e-9, 1e-12};
    // prefixes (SI, µ for micro)
    static constexpr const char* p[] = {"G", "M", "k", "", "m", "µ", "n", "p"};
    // zero value: prefix and unit, no separator
    if (std::fabs(value) < 1e-12)
        return std::format("0{}", unit);
    // loop scales
    for (int i = 0; i < 8; ++i) {
        // check we should format value with this scale
        if (std::fabs(value) >= v[i]) {
            // scaled mantissa for this divider
            double scaled = value / v[i];
            // mantissa rounding up to the next decade belongs to the next prefix (avoids 1e+03uA for 1mA)
            if (std::fabs(scaled) >= 999.5 && i > 0) {
                scaled /= 1000.0;
                return std::format("{:.3g} {}{}", scaled, p[i - 1], unit);
            }
            return std::format("{:.3g} {}{}", scaled, p[i], unit);
        }
    }
    return std::format("{:.3g} {}{}", value / v[7], p[7], unit);
}

std::vector<double> ChartEngine::log2_major_ticks(const double x_left, const double x_right, const int max_ticks) {
    // exit with empty vector if range is invalid
    if (x_left <= 0.0 || x_right <= 0.0)
        return {};
    // compute exponent range covering the visible window
    const double log_min = std::log2(x_left);
    const double log_max = std::log2(x_right);
    const int exp_min = static_cast<int>(std::floor(log_min));
    const int exp_max = static_cast<int>(std::ceil(log_max));
    const int num_octaves = exp_max - exp_min;
    // step: number of octaves between major ticks, adapt to available pixel budget
    int exp_step = 1;
    if (num_octaves > max_ticks)
        exp_step = static_cast<int>(std::ceil(static_cast<double>(num_octaves) / max_ticks));
    // build major tick list
    std::vector<double> result;
    for (int e = exp_min; e <= exp_max; e += exp_step) {
        const double tick_value = std::exp2(e);
        if (tick_value >= x_left - 1e-15 && tick_value <= x_right + 1e-15)
            result.push_back(tick_value);
    }
    return result;
}

ChartEngine::ChartEngine(ExpressionManager* expression_manager, const StepInformation* step_information, const AbscissaScale abscissa_scale, const size_t decimate_target) :
    m_expression_manager(expression_manager), m_step_information(step_information), m_abscissa_scale(abscissa_scale), m_decimate_target(decimate_target) {
    // abscissa
    auto& abscissa = expression_manager->abscissa();
    // abscissa name & unit
    m_abscissa_name = abscissa.name();
    m_abscissa_unit = abscissa.unit();
    // default to selecting every step so new charts show all available data
    for (size_t step = 0; step < m_step_information->length(); ++step)
        m_selected_steps.insert(step);
}

const std::set<size_t>& ChartEngine::selected_steps() {
    // return selected steps
    return m_selected_steps;
}

void ChartEngine::set_selected_steps(const std::set<size_t>& steps) {
    // store the new step selection
    m_selected_steps = steps;
    // re-plot the current expressions so only the selected steps remain
    const auto current = selected_expressions();
    plot_series(std::set<AnyExpression*>(current.begin(), current.end()));
}

std::vector<AnyExpression*> ChartEngine::selected_expressions() {
    // result
    std::vector<AnyExpression*> result;
    // allocate space
    result.reserve(m_series.size());
    // loop series
    for (auto& name : m_series | std::views::keys) {
        // use expression manager to find the expression by name
        if (auto expression = m_expression_manager->evaluate(name); expression != nullptr)
            result.push_back(expression);
    }
    // exit
    return result;
}

void ChartEngine::plot_series(const std::set<AnyExpression*>& expressions) {
    // loop existing series to find those that need to be removed (those whose ordinate expression is not in the new expressions list)
    for (auto it = m_series.begin(); it != m_series.end();) {
        // plotted expression
        auto& plotted_expression = std::get<0>(it->second);
        // check ordinate variant should be removed
        if (!expressions.contains(plotted_expression)) {
            // release axis
            release_y_axis(std::get<1>(it->second));
            // remove from map
            it = m_series.erase(it);
            // next
            continue;
        }
        // next
        ++it;
    }
    // current zoom window in abscissa values, None if not set
    const double x_left_ratio = std::get<0>(m_zoom_window);
    const double x_right_ratio = std::get<2>(m_zoom_window);
    // x0 and x1
    m_abscissa_left_value = x_left_ratio != -1 ? ratio_to_abscissa_value(x_left_ratio) : m_step_information->abscissa_left_value();
    m_abscissa_right_value = x_right_ratio != -1 ? ratio_to_abscissa_value(x_right_ratio) : m_step_information->abscissa_right_value();
    // loop expressions that need to be rendered
    for (AnyExpression* ordinate : expressions) {
        // process ordinate and find expressions to plot
        for (AnyExpression* ordinate_variant : get_expressions_to_plot(m_expression_manager, ordinate)) {
            // ordinate expression is always an Expression<double> at this point
            Expression<double>& double_ordinate_variant = std::get<Expression<double>>(*ordinate_variant);
            // lookup ordinate variant in series, create default if it does not exist;
            // the unassigned axis is the -1 sentinel because 0 is a valid axis index
            auto [it0, inserted0] = m_series.try_emplace(double_ordinate_variant.name(), OrdinateSeries(ordinate_variant, -1, std::unordered_map<size_t, std::pair<View<double>, View<double>>>(), std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), ChartColor()));
            // ordinate series data
            auto& [_, y_axis, rendered_series, min_value, max_value, color] = (it0->second);
            // loop rendered steps
            for (auto it2 = rendered_series.begin(); it2 != rendered_series.end();) {
                // check step in selected steps
                if (!m_selected_steps.contains(it2->first)) {
                    // remove it
                    it2 = rendered_series.erase(it2);
                    // next
                    continue;
                }
                // next
                ++it2;
            }
            // recompute the extrema from the retained steps so a deselected
            // outlier step no longer stretches the axis range
            min_value = std::numeric_limits<double>::max();
            max_value = -std::numeric_limits<double>::max();
            const bool log_axis = m_abscissa_scale != AbscissaScale::LINEAR;
            for (const auto& [step, views] : rendered_series) {
                // retained decimated views of this step
                const auto& [x_view, y_view] = views;
                for (size_t i = 0; i < x_view.size(); ++i) {
                    // only plottable samples contribute to the extrema
                    if (!std::isfinite(x_view[i]) || !std::isfinite(y_view[i]) || (log_axis && x_view[i] <= 0.0))
                        continue;
                    min_value = std::min(min_value, y_view[i]);
                    max_value = std::max(max_value, y_view[i]);
                }
            }
            // process axis as needed
            if (y_axis < 0) {
                // find an axis for this unit
                y_axis = get_y_axis(double_ordinate_variant.unit());
                // no axis is available
                if (y_axis < 0) {
                    // log information
                    spdlog::warn("Cannot add series '{}' of measurement type '{}' to chart — maximum number of Y axes reached", double_ordinate_variant.name(), double_ordinate_variant.unit());
                    // remove ordinate variant from map since we are not plot it
                    m_series.erase(it0);
                    // exit loop
                    break;
                }
                // update axis
                std::get<1>(it0->second) = y_axis;
            }
            // process color
            if (color.r == 0 && color.g == 0 && color.b == 0 && color.a == 0) {
                // assign next color in palette
                color = SERIES_COLOR_PALETTE[m_next_color_index % SERIES_COLOR_PALETTE.size()];
                // increment color index
                m_next_color_index++;
            }
            // loop steps to render
            for (size_t step : m_selected_steps) {
                // skip step if already rendered
                if (rendered_series.contains(step))
                    continue;
                // plot step
                if (auto [ok, x, y, y_min_value, y_max_value] = plot_step(double_ordinate_variant, step, min_value, max_value, x_right_ratio, x_left_ratio); ok) {
                    // update min & max values
                    min_value = y_min_value;
                    max_value = y_max_value;
                    // append to rendered series
                    rendered_series.emplace(step, std::pair(std::move(x), std::move(y)));
                }
            }
            // update min & max values
            std::get<3>(it0->second) = min_value;
            std::get<4>(it0->second) = max_value;
            // update color
            std::get<5>(it0->second) = color;
        }
    }
    // auto range axes
    auto_range();
}

void ChartEngine::auto_range() {
    // skip if no series to render
    if (!m_series.empty()) {
        // loop axes
        for (auto& axis_info : m_axes) {
            // reset min & max values for axes
            axis_info.min_value = std::numeric_limits<double>::max();
            axis_info.max_value = -std::numeric_limits<double>::max();
        }
        // loop rendered series
        for (auto& v : m_series | std::views::values) {
            // extract axis, min and max values
            const int y_axis = std::get<1>(v);
            const double min_value = std::get<3>(v);
            const double max_value = std::get<4>(v);
            // loop axes
            for (auto& axis_info : m_axes) {
                // check this is the axis
                if (axis_info.axis == y_axis) {
                    // update min & max values for axis
                    axis_info.min_value = std::min(axis_info.min_value, min_value);
                    axis_info.max_value = std::max(axis_info.max_value, max_value);
                    // exit
                    break;
                }
            }
        }
    }
    // loop axes
    for (auto& axis_info : m_axes) {
        // skip axis if not in use
        if (axis_info.plots == 0) {
            // ensure valid range is set in plot (Y1 is always visible even if no series are assigned to it)
            axis_info.plot_min_value = 0.0;
            axis_info.plot_max_value = 1.0;
            // next
            continue;
        }
        // no plottable data on this axis (all steps deselected): keep a finite
        // default range and reset the stored extrema so a later vertical
        // autorange or zoom reset computes a finite window
        if (axis_info.min_value > axis_info.max_value) {
            axis_info.min_value = 0.0;
            axis_info.max_value = 1.0;
            axis_info.plot_min_value = 0.0;
            axis_info.plot_max_value = 1.0;
            // next
            continue;
        }
        // range
        const double range = axis_info.max_value - axis_info.min_value;
        // delta
        const double delta = 0.03 * range;
        // update min & max values
        axis_info.plot_min_value = axis_info.min_value - delta;
        axis_info.plot_max_value = axis_info.max_value + delta;
    }
}

std::tuple<bool, View<double>, View<double>, double, double> ChartEngine::plot_step(Expression<double>& ordinate_variant, size_t step, const double min_value, const double max_value, const double x_right_ratio, const double x_left_ratio) const {
    // abscissa
    auto& abscissa = m_expression_manager->abscissa();
    // step abscissa & ordinate values
    auto abscissa_values = abscissa.step_data(step);
    auto ordinate_values = ordinate_variant.step_data(step);
    // empty spans carry no plottable data
    if (abscissa_values.empty() || ordinate_values.empty())
        return {};
    // check we have a zoom to apply
    if (x_left_ratio >= 0 && x_right_ratio >= 0) {
        // find indexes for the new zoom window
        const auto& [first, last] = find_abscissa_indexes(abscissa_values, m_abscissa_left_value, m_abscissa_right_value);
        // abscissa values
        abscissa_values = abscissa_values | std::views::drop(first) | std::views::take(last - first);
        // ordinate values
        ordinate_values = ordinate_values | std::views::drop(first) | std::views::take(last - first);
    }
    // check vector length
    if (abscissa_values.empty())
        return {};
    // decimate x and y values
    auto [x_np, y_np] = decimate_xy(abscissa_values, ordinate_values, m_decimate_target, DECIMATE_M4);
    // collect the extrema over the plottable samples: finite pairs with a
    // positive abscissa on logarithmic scales; non-finite samples stay in the
    // rendered views so the layout can break the polyline across the gap
    const bool log_axis = m_abscissa_scale != AbscissaScale::LINEAR;
    double step_min_value = std::numeric_limits<double>::max();
    double step_max_value = -std::numeric_limits<double>::max();
    bool plottable = false;
    for (size_t i = 0; i < x_np.size(); ++i) {
        // sample pair
        const double x_value = x_np[i];
        const double y_value = y_np[i];
        // only plottable samples contribute to the extrema
        if (!std::isfinite(x_value) || !std::isfinite(y_value) || (log_axis && x_value <= 0.0))
            continue;
        plottable = true;
        step_min_value = std::min(step_min_value, y_value);
        step_max_value = std::max(step_max_value, y_value);
    }
    // check all samples were non-plottable
    if (!plottable)
        return {};
    // exit
    return {true, std::move(x_np), std::move(y_np), std::min(min_value, step_min_value), std::max(max_value, step_max_value)};
}

void ChartEngine::clear() {
    // clear internal structures
    m_series.clear();
    m_next_color_index = 0;
    // release axes
    for (auto& current : m_axes) {
        // set it as not in use
        current.plots = 0;
        // unit
        current.unit = "";
        // reset plot ranges
        current.plot_min_value = 0;
        current.plot_max_value = 1.0;
    }
}

int ChartEngine::get_y_axis(const std::string& unit) {
    // use pointer
    AxisInformation* available = nullptr;
    // loop axis information
    for (auto& axis_info : m_axes) {
        // check it is in use
        if (axis_info.plots > 0) {
            // check unit
            if (axis_info.unit == unit) {
                // increase ref count
                axis_info.plots += 1;
                // use it
                return axis_info.axis;
            }
            continue;
        }
        // use this axis if none exist for the unit
        if (available == nullptr)
            available = &axis_info;
    }
    // check we have an available axis
    if (available) {
        // log information
        // use it
        available->plots = 1;
        available->unit = unit;
        available->min_value = std::numeric_limits<double>::max();
        available->max_value = -std::numeric_limits<double>::max();
        available->plot_min_value = 0.0;
        available->plot_max_value = 1.0;
        // exit
        return available->axis;
    }
    return -1;
}

bool ChartEngine::release_y_axis(const int axis) {
    // loop axis information
    for (auto& axis_info : m_axes) {
        // check this is the axis to release
        if (axis_info.axis == axis) {
            // decrease ref counter
            axis_info.plots--;
            // check axis is no longer in use
            if (axis_info.plots == 0) {
                // log information
                // reset plot range
                axis_info.plot_min_value = 0.0;
                axis_info.plot_max_value = 1.0;
                // reset unit
                axis_info.unit = "";
                // remove it from chart
                return true;
            }
            // keep it in chart
            return false;
        }
    }
    return false;
}

double ChartEngine::ratio_to_abscissa_value(const double x_ratio) const {
    // make sure ratio is in the interval [0, 1]
    const double percentage = std::max(0.0, std::min(1.0, x_ratio));
    // abscissa range
    const double left_value = m_step_information->abscissa_left_value();
    const double right_value = m_step_information->abscissa_right_value();
    // scale-aware interpolation over the full abscissa range
    return interpolate_abscissa(percentage, left_value, right_value, m_abscissa_scale);
}

double ChartEngine::plot_ratio_to_abscissa_value(const double x_ratio) const {
    // make sure ratio is in the interval [0, 1]
    const double percentage = std::max(0.0, std::min(1.0, x_ratio));
    // scale-aware interpolation over the visible (zoomed) abscissa range
    return interpolate_abscissa(percentage, m_abscissa_left_value, m_abscissa_right_value, m_abscissa_scale);
}

void ChartEngine::reset_zoom_window(const bool horizontal, const bool vertical) {
    // check horizontal reset
    if (horizontal) {
        // update zoom window
        std::get<0>(m_zoom_window) = -1;
        std::get<2>(m_zoom_window) = -1;
        // process all series to apply the new zoom window, full redraw if horizontal zoom changed
        redraw_all_series();
    }
    // check vertical reset
    if (vertical) {
        // update zoom window
        std::get<1>(m_zoom_window) = -1;
        std::get<3>(m_zoom_window) = -1;
        // update axis ranges based on collected min and max values for each variable type
        for (auto& axis_info : m_axes) {
            // range
            const auto y_range = axis_info.max_value - axis_info.min_value;
            // delta
            const auto delta = 0.03 * y_range;
            // set y axis range
            axis_info.plot_min_value = axis_info.min_value - delta;
            axis_info.plot_max_value = axis_info.max_value + delta;
        }
    }
}

void ChartEngine::update_zoom_window(double x_left_ratio, double x_right_ratio, double y_top_ratio, double y_bottom_ratio) {
    // check horizontal zoom ratios were provided
    if (x_left_ratio >= 0 && x_right_ratio >= 0) {
        // current zoom window
        auto& [current_x_left_ratio, current_y_top_ratio, current_x_right_ratio, current_y_bottom_ratio] = m_zoom_window;
        // use defaults
        current_x_left_ratio = current_x_left_ratio >= 0 ? current_x_left_ratio : 0.0;
        current_x_right_ratio = current_x_right_ratio >= 0 ? current_x_right_ratio : 1.0;
        // calculate new ratios based on the position of the mouse within the chart panel and the current zoom window
        x_left_ratio = current_x_left_ratio + x_left_ratio * (current_x_right_ratio - current_x_left_ratio);
        x_right_ratio = current_x_left_ratio + x_right_ratio * (current_x_right_ratio - current_x_left_ratio);
        // update zoom window
        m_zoom_window = {x_left_ratio, current_y_top_ratio, x_right_ratio, current_y_bottom_ratio};
        // process all series to apply the new zoom window, full redraw if horizontal zoom changed
        redraw_all_series();
    }
    // check vertical zoom ratios were provided
    if (y_top_ratio >= 0 && y_bottom_ratio >= 0) {
        // current zoom window
        auto& [current_x_left_ratio, current_y_top_ratio, current_x_right_ratio, current_y_bottom_ratio] = m_zoom_window;
        // use defaults
        current_y_top_ratio = current_y_top_ratio >= 0 ? current_y_top_ratio : 0.0;
        current_y_bottom_ratio = current_y_bottom_ratio >= 0 ? current_y_bottom_ratio : 1.0;
        // calculate new ratios based on the position of the mouse within the chart panel and the current zoom window
        y_top_ratio = current_y_top_ratio + y_top_ratio * (current_y_bottom_ratio - current_y_top_ratio);
        y_bottom_ratio = current_y_top_ratio + y_bottom_ratio * (current_y_bottom_ratio - current_y_top_ratio);
        // update zoom window
        m_zoom_window = {current_x_left_ratio, y_top_ratio, current_x_right_ratio, y_bottom_ratio};
        // update axis ranges based on collected min and max values for each variable type
        for (auto& axis_info : m_axes) {
            // skip axis if not in use
            if (axis_info.plots == 0) {
                // reset
                axis_info.plot_min_value = 0.0;
                axis_info.plot_max_value = 1.0;
                // next
                continue;
            }
            // range
            const double range = axis_info.max_value - axis_info.min_value;
            // delta
            const double delta = 0.03 * range;
            // actual axis min/max values (see auto_range)
            const double visual_y_min = axis_info.min_value - delta;
            const double visual_y_max = axis_info.max_value + delta;
            // calculate visual axis range
            const double visual_y_range = visual_y_max - visual_y_min;
            // y ratios measure downward from the top of the plot, so the band
            // between them maps to an ascending value window (implot swaps
            // inverted ranges on setup; the engine stores them ascending)
            axis_info.plot_min_value = visual_y_max - y_bottom_ratio * visual_y_range;
            axis_info.plot_max_value = visual_y_max - y_top_ratio * visual_y_range;
        }
    }
}

void ChartEngine::update(ExpressionManager* expression_manager, const StepInformation* step_information, AbscissaScale abscissa_scale) {
    // remember the plotted series names so the same plots can be restored
    // against the new data (e.g. after a simulation re-run)
    std::vector<std::string> plotted_names;
    plotted_names.reserve(m_series.size());
    for (const auto& name : m_series | std::views::keys)
        plotted_names.push_back(name);
    // drop the rendered series: their expression pointers and data views point
    // into the previous file, while zoom window, colors and step selection survive
    clear();
    // update internal references
    m_expression_manager = expression_manager;
    m_step_information = step_information;
    // prune step selections beyond the new step count so the replot never
    // reaches a stale step index of the replaced dataset
    while (!m_selected_steps.empty() && *std::prev(m_selected_steps.end()) >= step_information->length())
        m_selected_steps.erase(std::prev(m_selected_steps.end()));
    // abscissa
    auto& abscissa = expression_manager->abscissa();
    // abscissa name & unit
    m_abscissa_name = abscissa.name();
    m_abscissa_unit = abscissa.unit();
    // scale
    m_abscissa_scale = abscissa_scale;
    // re-plot the previous expressions against the new data for the preserved
    // step selection; names that no longer resolve are silently skipped
    if (!plotted_names.empty()) {
        // resolved expressions from the new manager
        std::set<AnyExpression*> expressions;
        for (const auto& name : plotted_names) {
            if (auto* expression = m_expression_manager->evaluate(name); expression != nullptr)
                expressions.insert(expression);
        }
        plot_series(expressions);
    }
}

void ChartEngine::set_decimate_target(const size_t decimate_target) {
    // store the new target; existing series are re-decimated lazily on the next redraw/zoom
    m_decimate_target = decimate_target;
}

std::pair<size_t, size_t> ChartEngine::find_abscissa_indexes(const std::span<const double>& abscissa, double left_value, double right_value) const {
    // ascending or descending abscissa
    if (m_step_information->is_abscissa_ascending()) {
        // check abscissa is not within the zoom window
        if (abscissa[0] > right_value || abscissa[abscissa.size() - 1] < left_value)
            return {0, 0};
        // find left and right indexes using binary search
        size_t left_index = std::ranges::lower_bound(abscissa, left_value) - abscissa.begin();
        size_t right_index = std::ranges::upper_bound(abscissa, right_value) - abscissa.begin();
        // expand by one point on each side to include the bracketing line segments
        if (left_index > 0)
            left_index--;
        if (right_index < abscissa.size())
            right_index++;
        // return slice for values within the zoom window
        return {left_index, right_index};
    }
    // check abscissa is not within the zoom window
    if (abscissa[0] < right_value || abscissa[abscissa.size() - 1] > left_value)
        return {0, 0};
    // find left and right indexes using binary search
    size_t left_index = std::ranges::lower_bound(abscissa, left_value, std::greater{}) - abscissa.begin();
    size_t right_index = std::ranges::upper_bound(abscissa, right_value, std::greater{}) - abscissa.begin();
    // expand by one point on each side to include the bracketing line segments
    if (left_index > 0)
        left_index--;
    if (right_index < abscissa.size())
        right_index++;
    // return slice for values within the zoom window
    return {left_index, right_index};
}

void ChartEngine::redraw_all_series() {
    // current zoom window
    auto& [x_left_ratio, y_top_ratio, x_right_ratio, current_y_bottom_ratio] = m_zoom_window;
    // x0 and x1
    m_abscissa_left_value = x_left_ratio >= 0 ? ratio_to_abscissa_value(x_left_ratio) : m_step_information->abscissa_left_value();
    m_abscissa_right_value = x_right_ratio >= 0 ? ratio_to_abscissa_value(x_right_ratio) : m_step_information->abscissa_right_value();
    // log information
    // abscissa
    auto& abscissa = m_expression_manager->abscissa();
    // loop existing series
    for (auto& ordinate_series : m_series | std::views::values) {
        // ordinate variant is always an Expression<double> at this point, extract it
        auto& ordinate_variant = std::get<Expression<double>>(*std::get<0>(ordinate_series));
        // steps
        auto& rendered_series = std::get<2>(ordinate_series);
        // min and max value recalculation for the new zoom window
        double min_value = std::numeric_limits<double>::max();
        double max_value = -std::numeric_limits<double>::max();
        // loop steps
        for (auto& [step, series] : rendered_series) {
            // step abscissa & ordinate values — zero copy
            auto abscissa_values = abscissa.step_data(step);
            auto ordinate_values = ordinate_variant.step_data(step);
            // empty spans carry no plottable data
            if (abscissa_values.empty() || ordinate_values.empty())
                continue;
            // check we have a zoom window to apply
            if (x_left_ratio >= 0 && x_right_ratio >= 0) {
                // find indexes for the new zoom window
                const auto& [first, last] = find_abscissa_indexes(abscissa_values, m_abscissa_left_value, m_abscissa_right_value);
                // abscissa values
                abscissa_values = abscissa_values | std::views::drop(first) | std::views::take(last - first);
                // ordinate values
                ordinate_values = ordinate_values | std::views::drop(first) | std::views::take(last - first);
            }
            // decimate x and y values
            auto [x, y] = decimate_xy(abscissa_values, ordinate_values, m_decimate_target, DECIMATE_M4);
            // collect the extrema over the plottable samples: finite pairs
            // with a positive abscissa on logarithmic scales
            const bool log_axis = m_abscissa_scale != AbscissaScale::LINEAR;
            bool plottable = false;
            double step_min_value = std::numeric_limits<double>::max();
            double step_max_value = -std::numeric_limits<double>::max();
            for (size_t i = 0; i < x.size(); ++i) {
                // sample pair
                const double x_value = x[i];
                const double y_value = y[i];
                // only plottable samples contribute to the extrema
                if (!std::isfinite(x_value) || !std::isfinite(y_value) || (log_axis && x_value <= 0.0))
                    continue;
                plottable = true;
                step_min_value = std::min(step_min_value, y_value);
                step_max_value = std::max(step_max_value, y_value);
            }
            // update min and max values
            if (plottable) {
                min_value = std::min(min_value, step_min_value);
                max_value = std::max(max_value, step_max_value);
            }
            // update map value
            series = std::make_pair(std::move(x), std::move(y));
        }
        // update min & max values
        std::get<3>(ordinate_series) = min_value;
        std::get<4>(ordinate_series) = max_value;
    }
}

std::string ChartEngine::hovered_series_text(const double abscissa_value) const {
    // abscissa prefix
    std::string result = m_abscissa_name + "=" + format_metric(abscissa_value, m_abscissa_unit);
    // do not evaluate if no series are present
    if (m_series.empty())
        return {};
    // collect series names
    std::vector<std::string> names;
    // allocate space
    names.reserve(m_series.size());
    // append names from series map
    for (const auto& [name, _] : m_series)
        names.push_back(name);
    // sort names, deterministic order for the hover text
    std::ranges::sort(names);
    // abscissa direction
    const bool ascending = m_step_information->is_abscissa_ascending();
    // loop series in sorted order
    for (const auto& name : names) {
        // lookup ordinate series
        const auto& ordinate_series = m_series.at(name);
        // ordinate variant is always an Expression<double> at this point, extract it
        auto& ordinate_variant = std::get<Expression<double>>(*std::get<0>(ordinate_series));
        // steps
        const auto& rendered_series = std::get<2>(ordinate_series);
        // step values at the hovered abscissa, joined in ascending step order
        std::string values;
        size_t value_count = 0;
        // collect steps and sort them, deterministic order for the hover text
        std::vector<size_t> steps;
        for (const auto& [step, _] : rendered_series)
            steps.push_back(step);
        std::ranges::sort(steps);
        // loop steps in ascending order
        for (const size_t step : steps) {
            // actual x and y values for the hovered abscissa value
            const auto& [x_view, y_view] = rendered_series.at(step);
            if (x_view.empty() || y_view.empty())
                continue;
            // interpolate y value at the hovered abscissa value
            const double y = interpolate_y(x_view, y_view, abscissa_value, ascending);
            // append separator
            if (value_count > 0)
                values += ", ";
            // append formatted value
            values += format_metric(y, ordinate_variant.unit());
            value_count++;
        }
        // no plotable data for any step
        if (value_count == 0)
            continue;
        // single step: plain value, multiple steps: grouped values
        if (value_count > 1)
            result += " " + ordinate_variant.name() + "=[" + values + "]";
        else
            result += " " + ordinate_variant.name() + "=" + values;
    }
    return result;
}

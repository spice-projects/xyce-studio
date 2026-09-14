#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "chart_layout.h"
#include "chart_style.h"

namespace
{
    // implot NiceNum: round x to a nice 1/2/5 x 10^n number
    double nice_num(const double x, const bool round) {
        // exponent and fraction of x in scientific form
        const int expv = static_cast<int>(std::floor(std::log10(x)));
        const double f = x / std::pow(10.0, expv);
        // rounded variant snaps to the nearest of 1/2/5/10
        double nf;
        if (round) {
            if (f < 1.5)
                nf = 1;
            else if (f < 3)
                nf = 2;
            else if (f < 7)
                nf = 5;
            else
                nf = 10;
        }
        else if (f <= 1)
            nf = 1;
        else if (f <= 2)
            nf = 2;
        else if (f <= 5)
            nf = 5;
        else
            nf = 10;
        // nice number in the requested magnitude
        return nf * std::pow(10.0, expv);
    }

    // implot ImRemap: linear remap of v from [v0, v1] into [r0, r1]
    float im_remap(const float v, const float v0, const float v1, const float r0, const float r1) { return r0 + (v - v0) * (r1 - r0) / (v1 - v0); }

    // format one tick label through the shared si metric formatter
    std::string format_tick_label(const double value, const std::string& unit) { return ChartEngine::format_metric(value, unit); }

    // implot Locator_Default: evenly spaced nice ticks with minor subdivisions
    void locator_default(std::vector<ChartTick>& ticks, const double range_min, const double range_max, const float pixels, const bool vertical, const std::string& unit, const ChartLayout::TextMeasurer& measure) {
        // no ticks on a degenerate range
        if (range_min == range_max)
            return;
        // implot tick budget constants
        constexpr float TICK_FILL_X = 0.8f;
        constexpr float TICK_FILL_Y = 1.0f;
        constexpr int N_MINOR = 10;
        // major tick count from the available pixel budget
        const int n_major = std::max(2, static_cast<int>(std::lround(pixels / (vertical ? 300.0f : 400.0f))));
        // nice interval covering the visible range
        const double nice_range = nice_num((range_max - range_min) * 0.99, false);
        const double interval = nice_num(nice_range / (n_major - 1), true);
        // expand the tick span to whole intervals on both ends
        const double graphmin = std::floor(range_min / interval) * interval;
        const double graphmax = std::ceil(range_max / interval) * interval;
        // label prune state: remember the first major index in the ticker
        bool first_major_set = false;
        int first_major_idx = 0;
        // ticks added before this locator (tickers are always empty here)
        const int idx0 = static_cast<int>(ticks.size());
        // total label extent accumulated for the prune decision; horizontal
        // axes compare widths, vertical axes compare text heights
        float total_size = 0.0f;
        // loop majors and their minor subdivisions
        for (double major = graphmin; major < graphmax + 0.5 * interval; major += interval) {
            // snap a major landing next to zero exactly to zero
            if (major - interval < 0 && major + interval > 0)
                major = 0;
            // add the major tick when inside the visible range
            if (major >= range_min && major <= range_max) {
                if (!first_major_set) {
                    first_major_idx = static_cast<int>(ticks.size());
                    first_major_set = true;
                }
                // major tick with a formatted label
                ChartTick tick;
                tick.value = major;
                tick.major = true;
                tick.show_label = true;
                tick.label = format_tick_label(major, unit);
                tick.label_width = measure(tick.label);
                total_size += vertical ? CHART_TEXT_HEIGHT : tick.label_width;
                ticks.push_back(tick);
            }
            // add the minor subdivisions between this major and the next
            for (int i = 1; i < N_MINOR; ++i) {
                const double minor = major + i * interval / N_MINOR;
                // skip minors outside the visible range
                if (minor >= range_min && minor <= range_max) {
                    // minor tick with a formatted label
                    ChartTick tick;
                    tick.value = minor;
                    tick.major = false;
                    tick.show_label = true;
                    tick.label = format_tick_label(minor, unit);
                    tick.label_width = measure(tick.label);
                    total_size += vertical ? CHART_TEXT_HEIGHT : tick.label_width;
                    ticks.push_back(tick);
                }
            }
        }
        // prune every other label when the labels would crowd the axis
        if ((!vertical && total_size > pixels * TICK_FILL_X) || (vertical && total_size > pixels * TICK_FILL_Y)) {
            // hide labels left of the first major
            for (int i = first_major_idx - 1; i >= idx0; i -= 2)
                ticks[static_cast<size_t>(i)].show_label = false;
            // hide labels right of the first major
            for (int i = first_major_idx + 1; i < static_cast<int>(ticks.size()); i += 2)
                ticks[static_cast<size_t>(i)].show_label = false;
        }
    }

    // implot CalcLogarithmicExponents: major budget and exponent span for log axes
    bool calc_logarithmic_exponents(const double range_min, const double range_max, const float pixels, const bool vertical, int& exp_min, int& exp_max, int& exp_step) {
        // the range must not cross zero
        if (range_min * range_max > 0) {
            // major tick budget from the available pixels
            const int n_major = vertical ? std::max(2, static_cast<int>(std::lround(pixels * 0.02f))) : std::max(2, static_cast<int>(std::lround(pixels * 0.01f)));
            // exponent span covering the visible range
            const double log_min = std::log10(std::fabs(range_min));
            const double log_max = std::log10(std::fabs(range_max));
            const double log_a = std::min(log_min, log_max);
            const double log_b = std::max(log_min, log_max);
            // octave step derived from the pixel budget
            exp_step = std::max(1, static_cast<int>(log_b - log_a) / n_major);
            exp_min = static_cast<int>(log_a);
            exp_max = static_cast<int>(log_b);
            // steps other than 1 must be multiples of three
            if (exp_step != 1) {
                while (exp_step % 3 != 0)
                    exp_step++;
                while (exp_min % exp_step != 0)
                    exp_min--;
            }
            return true;
        }
        return false;
    }

    // implot AddTicksLogarithmic: decade majors with subdivision minors
    void add_ticks_logarithmic(std::vector<ChartTick>& ticks, const double range_min, const double range_max, const int exp_min, const int exp_max, const int exp_step, const std::string& unit, const ChartLayout::TextMeasurer& measure) {
        // sign of the range (both bounds share it after the zero-crossing check)
        const double sign = range_max >= 0.0 ? 1.0 : -1.0;
        // loop decade groups
        for (int e = exp_min - exp_step; e < exp_max + exp_step; e += exp_step) {
            // decade bounds of the group
            double major1 = sign * std::pow(10.0, e);
            double major2 = sign * std::pow(10.0, e + 1);
            // subdivision width inside one decade
            double interval = (major2 - major1) / 9;
            // add the group major when inside the visible range
            if (major1 >= range_min - DBL_EPSILON && major1 <= range_max + DBL_EPSILON) {
                // major tick with a formatted label
                ChartTick tick;
                tick.value = major1;
                tick.major = true;
                tick.show_label = true;
                tick.label = format_tick_label(major1, unit);
                tick.label_width = measure(tick.label);
                ticks.push_back(tick);
            }
            // loop the decades covered by this group
            for (int j = 0; j < exp_step; ++j) {
                // decade bounds of this sub-decade
                major1 = sign * std::pow(10.0, e + j);
                major2 = sign * std::pow(10.0, e + j + 1);
                // subdivision width inside this sub-decade
                interval = (major2 - major1) / 9;
                // loop minors; the last sub-decade keeps 8 minors so the next group major is not duplicated
                for (int i = 1; i < 9 + static_cast<int>(j < exp_step - 1); ++i) {
                    // minor value inside the sub-decade
                    const double minor = major1 + i * interval;
                    // skip minors outside the visible range
                    if (minor >= range_min - DBL_EPSILON && minor <= range_max + DBL_EPSILON) {
                        // minor tick without a label
                        ChartTick tick;
                        tick.value = minor;
                        tick.major = false;
                        tick.show_label = false;
                        ticks.push_back(tick);
                    }
                }
            }
        }
    }

    // implot Locator_Log10 for decimal log axes
    void locator_log10(std::vector<ChartTick>& ticks, const double range_min, const double range_max, const float pixels, const bool vertical, const std::string& unit, const ChartLayout::TextMeasurer& measure) {
        // exponent bounds and step
        int exp_min = 0;
        int exp_max = 0;
        int exp_step = 1;
        if (calc_logarithmic_exponents(range_min, range_max, pixels, vertical, exp_min, exp_max, exp_step))
            add_ticks_logarithmic(ticks, range_min, range_max, exp_min, exp_max, exp_step, unit, measure);
    }

    // grid line alpha of minor ticks from the implot density gate
    float minor_grid_alpha(const size_t tick_count, const float span_pixels) {
        // tick density along the axis
        const float density = static_cast<float>(tick_count) / span_pixels;
        // density gate: no minor grid on dense axes
        if (density >= 0.2f)
            return 0.0f;
        // density fade between 0.1 and 0.2 multiplied by the style alpha
        const float density_alpha = std::clamp(im_remap(density, 0.1f, 0.2f, 1.0f, 0.0f), 0.0f, 1.0f);
        return CHART_MINOR_ALPHA * density_alpha;
    }
} // namespace

std::pair<double, double> clamped_abscissa_limits(const ChartEngine& engine) {
    // visible abscissa range from the engine
    double left_value = engine.abscissa_left_value();
    double right_value = engine.abscissa_right_value();
    // clamped above zero for logarithmic scales
    if (engine.abscissa_scale() != AbscissaScale::LINEAR) {
        // clamp non-positive left limit to a fraction of the right limit
        if (left_value <= 0.0)
            left_value = right_value > 0.0 ? right_value / 1e6 : 1.0;
        // clamp non-positive right limit to a multiple of the left limit
        if (right_value <= 0.0)
            right_value = left_value > 0.0 ? left_value * 1e6 : 1.0;
        // avoid a degenerate zero-width range
        if (left_value == right_value)
            right_value = left_value * 2.0;
    }
    return {left_value, right_value};
}

ChartLayout::ChartLayout(TextMeasurer measure) :
    m_measure(std::move(measure)) {}

ChartFrame ChartLayout::build(const ChartEngine& engine, const float width, const float height) const {
    // frame snapshot under construction
    ChartFrame frame;
    frame.frame_w = width;
    frame.frame_h = height;
    // canvas area inset by the plot padding
    const float canvas_x = CHART_PLOT_PADDING;
    const float canvas_y = CHART_PLOT_PADDING;
    const float canvas_w = std::max(0.0f, width - 2.0f * CHART_PLOT_PADDING);
    const float canvas_h = std::max(0.0f, height - 2.0f * CHART_PLOT_PADDING);
    // legend entries in series order (the engine series map is name ordered)
    for (const auto& [name, ordinate_series] : engine.series()) {
        // legend entry with the assigned series color
        frame.legend.push_back({name, std::get<5>(ordinate_series)});
    }
    // outside legend shrinks the canvas vertically when there are entries
    if (!frame.legend.empty()) {
        // horizontal legend: icons plus labels and spacing between entries
        float sum_label_width = 0.0f;
        for (const auto& item : frame.legend)
            sum_label_width += m_measure(item.name);
        // legend block size
        const size_t count = frame.legend.size();
        frame.legend_w = 2.0f * CHART_LEGEND_INNER_PADDING_X + CHART_TEXT_HEIGHT * static_cast<float>(count) + sum_label_width + CHART_LEGEND_SPACING_X * static_cast<float>(count - 1);
        frame.legend_h = 2.0f * CHART_LEGEND_INNER_PADDING_Y + CHART_TEXT_HEIGHT;
        // shrink the canvas below the legend block
        const float canvas_h_after = std::max(0.0f, canvas_h - frame.legend_h - CHART_LEGEND_PADDING_Y);
        // plot rect vertical span
        frame.plot_y = canvas_y;
        frame.plot_h = std::max(0.0f, canvas_h_after - (CHART_TEXT_HEIGHT + CHART_LABEL_PADDING));
        // x axis datum line at the plot bottom edge
        frame.x_datum = frame.plot_y + frame.plot_h;
        // legend centered horizontally, south of the canvas
        frame.legend_x = std::max(0.0f, (width - frame.legend_w) * 0.5f);
        frame.legend_y = canvas_y + canvas_h - CHART_LEGEND_PADDING_Y - frame.legend_h;
    }
    else {
        // no legend: plain vertical span with the x axis strip
        frame.plot_y = canvas_y;
        frame.plot_h = std::max(0.0f, canvas_h - (CHART_TEXT_HEIGHT + CHART_LABEL_PADDING));
        frame.x_datum = frame.plot_y + frame.plot_h;
    }
    // abscissa limits clamped for logarithmic scales
    const auto [x_left_value, x_right_value] = clamped_abscissa_limits(engine);
    // y axis locator budget
    const float plot_height = frame.plot_h;
    // enabled y axes: y1 always on the west side, y2 and y3 opposite (east) when in use
    frame.y_axes[0].enabled = true;
    frame.y_axes[1].enabled = engine.axes()[1].plots > 0;
    frame.y_axes[1].opposite = frame.y_axes[1].enabled;
    frame.y_axes[2].enabled = engine.axes()[2].plots > 0;
    frame.y_axes[2].opposite = frame.y_axes[2].enabled;
    // y tick ranges and locators (linear axes, vertical orientation)
    for (size_t i = 0; i < frame.y_axes.size(); ++i) {
        // skip disabled axes and degenerate heights
        if (!frame.y_axes[i].enabled || plot_height <= 0.0f)
            continue;
        // engine range of this axis
        const auto& axis_info = engine.axes()[i];
        // locator ticks for the visible range
        locator_default(frame.y_axes[i].ticks, axis_info.plot_min_value, axis_info.plot_max_value, plot_height, true, axis_info.unit, m_measure);
    }
    // y axis label widths per axis
    float max_label_width[3] = {0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < frame.y_axes.size(); ++i) {
        // label width is the maximum over shown labels
        for (const auto& tick : frame.y_axes[i].ticks) {
            if (tick.show_label && tick.label_width > max_label_width[i])
                max_label_width[i] = tick.label_width;
        }
    }
    // left pad from the west axis (y1)
    float pad_left = 0.0f;
    if (!frame.y_axes[0].ticks.empty())
        pad_left += max_label_width[0] + CHART_LABEL_PADDING;
    // right pad from the east axes (y2, y3); y3 accumulates after y2 in implot order
    float pad_right = 0.0f;
    if (frame.y_axes[2].enabled && !frame.y_axes[2].ticks.empty())
        pad_right += max_label_width[2] + CHART_LABEL_PADDING;
    if (frame.y_axes[1].enabled && !frame.y_axes[1].ticks.empty())
        pad_right += (frame.y_axes[2].enabled && !frame.y_axes[2].ticks.empty() ? CHART_MINOR_TICK_LEN + CHART_LABEL_PADDING : 0.0f) + max_label_width[1] + CHART_LABEL_PADDING;
    // plot rect horizontal span
    frame.plot_x = canvas_x + pad_left;
    frame.plot_w = std::max(0.0f, canvas_w - pad_left - pad_right);
    // axis datums: y1 west datum, y2 and y3 east datums stacked outward
    frame.y_axes[0].datum = canvas_x + pad_left;
    frame.y_axes[1].datum = canvas_x + canvas_w - pad_right;
    frame.y_axes[2].datum = canvas_x + canvas_w - (frame.y_axes[2].enabled && !frame.y_axes[2].ticks.empty() ? max_label_width[2] + CHART_LABEL_PADDING : 0.0f);
    // abscissa tick locator on the horizontal span
    if (frame.plot_w > 0.0f) {
        // abscissa scale drives the locator choice
        const AbscissaScale scale = engine.abscissa_scale();
        if (scale == AbscissaScale::DECADE) {
            // log10 locator on the clamped range
            locator_log10(frame.x_ticks, x_left_value, x_right_value, frame.plot_w, false, engine.abscissa_unit(), m_measure);
        }
        else if (scale == AbscissaScale::OCTAVE) {
            // custom log2 major ticks sized from the plot width
            const int max_ticks = std::max(2, static_cast<int>(std::lround(frame.plot_w * 0.01f)));
            const auto log2_ticks = ChartEngine::log2_major_ticks(x_left_value, x_right_value, max_ticks);
            // custom ticks carry labels but are classified as minor
            for (const double tick_value : log2_ticks) {
                // custom tick with a formatted label
                ChartTick tick;
                tick.value = tick_value;
                tick.major = false;
                tick.show_label = true;
                tick.label = format_tick_label(tick_value, engine.abscissa_unit());
                tick.label_width = m_measure(tick.label);
                frame.x_ticks.push_back(tick);
            }
        }
        else {
            // linear locator
            locator_default(frame.x_ticks, x_left_value, x_right_value, frame.plot_w, false, engine.abscissa_unit(), m_measure);
        }
    }
    // pixel mapping of the x ticks through the abscissa scale
    const bool log_scale = engine.abscissa_scale() != AbscissaScale::LINEAR;
    for (auto& tick : frame.x_ticks) {
        // fraction of the tick along the visible abscissa range
        double t;
        if (log_scale) {
            // logarithmic fraction in log10 space (base cancels in the ratio, so it also covers log2)
            const double log_left = x_left_value <= 0.0 ? -DBL_MAX : std::log10(x_left_value);
            const double log_right = x_right_value <= 0.0 ? DBL_MAX : std::log10(x_right_value);
            const double log_value = tick.value <= 0.0 ? log_left : std::log10(tick.value);
            t = log_right > log_left ? (log_value - log_left) / (log_right - log_left) : 0.0;
        }
        else {
            // linear fraction over the visible range
            t = x_right_value > x_left_value ? (tick.value - x_left_value) / (x_right_value - x_left_value) : 0.0;
        }
        // pixel position inside the plot rect
        tick.pixel_pos = static_cast<float>(frame.plot_x + t * frame.plot_w);
    }
    // pixel mapping and grid subsets of the y axes
    for (size_t i = 0; i < frame.y_axes.size(); ++i) {
        // skip disabled axes
        if (!frame.y_axes[i].enabled)
            continue;
        // engine range of this axis
        const auto& axis_info = engine.axes()[i];
        // invert the vertical fraction so larger values are higher
        for (auto& tick : frame.y_axes[i].ticks) {
            // fraction of the tick inside the axis range (unclamped like implot)
            const double t = axis_info.plot_max_value > axis_info.plot_min_value ? (tick.value - axis_info.plot_min_value) / (axis_info.plot_max_value - axis_info.plot_min_value) : 0.0;
            // pixel position inside the plot rect (bottom to top)
            tick.pixel_pos = static_cast<float>(frame.plot_y + frame.plot_h - t * frame.plot_h);
        }
        // grid lines from the ticks: majors always, minors density gated
        const float alpha = minor_grid_alpha(frame.y_axes[i].ticks.size(), frame.plot_h);
        for (const auto& tick : frame.y_axes[i].ticks) {
            // only ticks inside the plot vertical span render
            if (tick.pixel_pos < frame.plot_y || tick.pixel_pos > frame.plot_y + frame.plot_h)
                continue;
            // major grid is unconditional, minor grid uses the density alpha
            if (tick.major)
                frame.y_axes[i].grid_lines.push_back({tick.pixel_pos, true, 1.0f});
            else if (alpha > 0.0f)
                frame.y_axes[i].grid_lines.push_back({tick.pixel_pos, false, alpha});
        }
    }
    // grid lines from the x ticks
    const float x_alpha = minor_grid_alpha(frame.x_ticks.size(), frame.plot_w);
    for (const auto& tick : frame.x_ticks) {
        // only ticks inside the plot horizontal span render
        if (tick.pixel_pos < frame.plot_x || tick.pixel_pos > frame.plot_x + frame.plot_w)
            continue;
        // major grid is unconditional, minor grid uses the density alpha
        if (tick.major)
            frame.x_grid.push_back({tick.pixel_pos, true, 1.0f});
        else if (x_alpha > 0.0f)
            frame.x_grid.push_back({tick.pixel_pos, false, x_alpha});
    }
    // series polylines in pixel coordinates
    for (const auto& [name, ordinate_series] : engine.series()) {
        // extract axis, steps and color
        const int axis = std::get<1>(ordinate_series);
        const auto& steps = std::get<2>(ordinate_series);
        const auto& color = std::get<5>(ordinate_series);
        // loop rendered steps
        for (const auto& [step, data] : steps) {
            // step x and y views
            const auto& [x_view, y_view] = data;
            // polyline run under construction
            ChartSeriesFrame run;
            run.name = name;
            run.color = color;
            run.axis = axis;
            run.step = step;
            run.points.reserve(x_view.size());
            // map each sample to pixel coordinates
            for (size_t p = 0; p < x_view.size(); ++p) {
                // sample x fraction through the abscissa scale
                const double x_value = x_view[p];
                const double y_value = y_view[p];
                // x fraction (linear or logarithmic, unclamped like implot)
                double tx;
                if (log_scale) {
                    const double log_left = x_left_value <= 0.0 ? -DBL_MAX : std::log10(x_left_value);
                    const double log_right = x_right_value <= 0.0 ? DBL_MAX : std::log10(x_right_value);
                    const double log_value = x_value <= 0.0 ? log_left : std::log10(x_value);
                    tx = log_right > log_left ? (log_value - log_left) / (log_right - log_left) : 0.0;
                }
                else {
                    tx = x_right_value > x_left_value ? (x_value - x_left_value) / (x_right_value - x_left_value) : 0.0;
                }
                // y fraction of the owning axis range
                const auto& axis_info = engine.axes()[static_cast<size_t>(axis)];
                const double ty = axis_info.plot_max_value > axis_info.plot_min_value ? (y_value - axis_info.plot_min_value) / (axis_info.plot_max_value - axis_info.plot_min_value) : 0.0;
                // append the mapped point
                run.points.push_back({static_cast<float>(frame.plot_x + tx * frame.plot_w), static_cast<float>(frame.plot_y + frame.plot_h - ty * frame.plot_h)});
            }
            // append the run
            frame.series.push_back(std::move(run));
        }
    }
    return frame;
}

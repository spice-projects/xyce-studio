#include <algorithm>
#include <cmath>
#include <set>

#include <spdlog/spdlog.h>

#include "chart_text_measurer.h"
#include "charts_renderer.h"

ChartsRenderer::DatasetCharts* ChartsRenderer::active_dataset() {
    // look up the state of the active tab
    const auto it = m_datasets.find(m_active_dataset_id);
    // no state when the tab has none
    return it != m_datasets.end() ? &it->second : nullptr;
}

const ChartsRenderer::DatasetCharts* ChartsRenderer::active_dataset() const {
    // look up the state of the active tab
    const auto it = m_datasets.find(m_active_dataset_id);
    // no state when the tab has none
    return it != m_datasets.end() ? &it->second : nullptr;
}

ChartsRenderer::ChartsRenderer(PublishFramesFunction publish) :
    m_publish(std::move(publish)), m_layout(make_chart_text_measurer()) {}

void ChartsRenderer::set_viewport(const float width, const float height) {
    // reject degenerate viewports; the panel publishes nothing while hidden or collapsed
    if (width <= 0.0f || height <= 0.0f)
        return;
    // ignore no-op updates fired by unrelated relayouts
    if (m_viewport_width == width && m_viewport_height == height)
        return;
    // store the geometry driving the frame layout
    m_viewport_width = width;
    m_viewport_height = height;
    // republish the frames for the new geometry
    publish_frames();
}

void ChartsRenderer::set_dark_mode(const bool dark_mode) {
    // ignore no-op updates fired by unrelated relayouts
    if (m_dark_mode == dark_mode)
        return;
    // store the new mode and republish the frames with the other palette
    m_dark_mode = dark_mode;
    publish_frames();
}

void ChartsRenderer::reset_viewport() {
    // clear the active hover readout so a hidden panel cannot retain the
    // status text or publish a pending readout against the hidden dataset
    hover_ended();
    // zero the stored geometry so publish_frames() short-circuits on the next call
    m_viewport_width = 0.0f;
    m_viewport_height = 0.0f;
    // drop the hit-test rects so a hidden panel reports no plot areas
    m_plot_rects.clear();
}

void ChartsRenderer::publish_frames() {
    // nothing to publish without a sink or a valid viewport
    if (!m_publish || m_viewport_width <= 0.0f || m_viewport_height <= 0.0f)
        return;
    // active dataset state
    auto* dataset = active_dataset();
    // frames under construction
    std::vector<ChartFrame> frames;
    // hit-test rects under construction, panel-relative per chart index
    std::vector<std::tuple<float, float, float, float>> plot_rects;
    // build one frame per chart of the active dataset, stacked top to bottom
    if (dataset != nullptr && !dataset->charts.empty()) {
        // per chart canvas size
        const float chart_height = m_viewport_height / static_cast<float>(dataset->charts.size());
        // build the frame snapshot from the chart engine state
        for (const auto& chart : dataset->charts) {
            frames.push_back(m_layout.build(*chart, m_viewport_width, chart_height));
            // panel-relative plot rect of this chart for zoom and hover hit tests
            const auto& built = frames.back();
            const float offset = static_cast<float>(plot_rects.size()) * chart_height;
            plot_rects.emplace_back(built.plot_x, built.plot_y + offset, built.plot_x + built.plot_w, built.plot_y + built.plot_h + offset);
        }
    }
    // remember the rects for the interaction hit tests
    m_plot_rects = std::move(plot_rects);
    // hand the frames to the slint layer
    m_publish(frames);
}

void ChartsRenderer::update(const int dataset_id, ExpressionManager& expression_manager, const StepInformation& step_information, const AbscissaScale abscissa_scale, const std::vector<std::vector<std::string>>& suggested_plots) {
    // fetch or create the state of this tab
    auto& dataset = m_datasets[dataset_id];
    // remember whether the charts must be re-pointed to a replaced file
    const bool file_changed = dataset.expression_manager != &expression_manager;
    // refresh the dataset data references
    dataset.expression_manager = &expression_manager;
    dataset.step_information = &step_information;
    dataset.abscissa_scale = abscissa_scale;
    dataset.suggested_plots = suggested_plots;
    // activation or new data, clear any hover readout from the previous tab
    hover_ended();
    // make this dataset the active tab
    m_active_dataset_id = dataset_id;
    // check charts already exist for this dataset
    if (!dataset.charts.empty()) {
        // a replaced file (simulation re-run) re-points the charts to the new
        // data while zoom windows, plotted series names and step selections
        // survive; a plain tab switch reuses the charts untouched
        if (file_changed) {
            // loop charts
            for (const auto& chart : dataset.charts)
                // update chart with the new information
                chart->update(dataset.expression_manager, dataset.step_information, dataset.abscissa_scale);
        }
    }
    else if (!suggested_plots.empty()) {
        // create one chart per suggested plot group
        for (const auto& plot_names : suggested_plots) {
            // add a chart for the group
            auto* chart = add_chart();
            // resolved expressions for this group
            std::set<AnyExpression*> resolved_expressions;
            // resolve each expression name to its pointer
            for (const auto& name : plot_names) {
                // evaluate the expression name
                auto* expression = dataset.expression_manager->evaluate(name);
                // check it resolved to an existing expression
                if (expression == nullptr) {
                    // log information
                    spdlog::warn("Suggested plot expression '{}' not found in expression manager", name);
                    // skip unresolved expression
                    continue;
                }
                // append resolved expression
                resolved_expressions.insert(expression);
            }
            // plot the resolved expressions on the chart
            chart->plot_series(resolved_expressions);
        }
    }
    else {
        // add a new chart with no pre-populated expressions
        add_chart();
    }
    // republish the frames for the activated dataset
    publish_frames();
}

ChartEngine* ChartsRenderer::add_chart() {
    // append at the end of the active dataset stack
    const auto* dataset = active_dataset();
    // no active dataset means the indexed overload inserts the first chart
    return add_chart(dataset != nullptr ? dataset->charts.size() : 0);
}

ChartEngine* ChartsRenderer::add_chart(const size_t after_index) {
    // active dataset state
    auto* dataset = active_dataset();
    // no charts can be added without an active dataset
    if (dataset == nullptr)
        return nullptr;
    // insertion position: directly after the given chart, clamped to the end
    const size_t index = std::min(after_index + 1, dataset->charts.size());
    // create the chart and insert it into the dataset vector
    const auto chart_it = dataset->charts.insert(dataset->charts.begin() + static_cast<std::ptrdiff_t>(index), std::make_unique<ChartEngine>(dataset->expression_manager, dataset->step_information, dataset->abscissa_scale, k_decimate_target));
    // chart
    auto& chart = *chart_it;
    // all charts share the abscissa range: a chart added after a zoom joins
    // the shared horizontal zoom window of the panel (vertical zoom stays per
    // chart); unset ratios pass through unchanged
    if (dataset->charts.size() > 1) {
        const auto& shared = dataset->charts.front()->zoom_window();
        chart->update_zoom_window(std::get<0>(shared), std::get<2>(shared), -1, -1);
    }
    // plot series (will do nothing, but will set the correct abscissa for the zoom window)
    chart->plot_series({});
    // exit
    return chart.get();
}

void ChartsRenderer::move_chart(const size_t from, const size_t to) {
    // active dataset state
    auto* dataset = active_dataset();
    // log information
    spdlog::debug("User requested moving chart {} to {}", from, to);
    // a same index or an out of range index cannot reorder anything
    if (dataset == nullptr || from == to || from >= dataset->charts.size() || to >= dataset->charts.size())
        return;
    // lift the chart state out of the stack and drop it at the target
    // position; per chart state (zoom window, plotted series, step
    // selection) lives in the engine and moves with the element
    auto chart = std::move(dataset->charts[from]);
    dataset->charts.erase(dataset->charts.begin() + static_cast<std::ptrdiff_t>(from));
    dataset->charts.insert(dataset->charts.begin() + static_cast<std::ptrdiff_t>(to), std::move(chart));
    // republish the reordered frames
    publish_frames();
}

void ChartsRenderer::release_dataset(const int dataset_id) {
    // clear the active hover readout when the released tab was active
    if (m_active_dataset_id == dataset_id) {
        // clear hover state
        hover_ended();
        // no dataset is active anymore
        m_active_dataset_id = -1;
    }
    // drop the chart state of the tab
    m_datasets.erase(dataset_id);
    // republish
    publish_frames();
}

void ChartsRenderer::release_all_datasets() {
    // clear active hover readout
    hover_ended();
    // drop every dataset chart state
    m_datasets.clear();
    // no dataset is active anymore
    m_active_dataset_id = -1;
    // republish
    publish_frames();
}

size_t ChartsRenderer::chart_count() const {
    // active dataset state
    const auto* dataset = active_dataset();
    // no charts without an active dataset
    return dataset != nullptr ? dataset->charts.size() : 0;
}

std::vector<AnyExpression*> ChartsRenderer::all_expressions() const {
    // active dataset state
    const auto* dataset = active_dataset();
    // nothing to list without an active dataset
    if (dataset == nullptr || dataset->expression_manager == nullptr)
        return {};
    // delegate to the expression manager
    return dataset->expression_manager->expressions();
}

std::vector<AnyExpression*> ChartsRenderer::chart_selected_expressions(const size_t chart_index) const {
    // active dataset state
    const auto* dataset = active_dataset();
    // guard against a missing dataset or an invalid chart index
    if (dataset == nullptr || chart_index >= dataset->charts.size())
        return {};
    // delegate to the chart
    return dataset->charts[chart_index]->selected_expressions();
}

std::pair<double, double> ChartsRenderer::abscissa_range() const {
    // active dataset state
    const auto* dataset = active_dataset();
    // no step information loaded yet, fall back to a safe default
    if (dataset == nullptr || dataset->step_information == nullptr)
        return {0.0, 1.0};
    // full abscissa value range of the loaded file
    return {dataset->step_information->abscissa_left_value(), dataset->step_information->abscissa_right_value()};
}

AnyExpression* ChartsRenderer::evaluate_expression(const std::string& expression) {
    // active dataset state
    auto* dataset = active_dataset();
    // nothing to evaluate without an active dataset
    if (dataset == nullptr || dataset->expression_manager == nullptr)
        return nullptr;
    // delegate to the expression manager
    return dataset->expression_manager->evaluate(expression, expression);
}

std::set<size_t> ChartsRenderer::chart_selected_steps(const size_t chart_index) const {
    // active dataset state
    const auto* dataset = active_dataset();
    // guard against a missing dataset or an invalid chart index
    if (dataset == nullptr || chart_index >= dataset->charts.size())
        return {};
    // delegate to the chart
    return dataset->charts[chart_index]->selected_steps();
}

void ChartsRenderer::set_chart_selected_steps(const size_t chart_index, const std::set<size_t>& steps) {
    // active dataset state
    auto* dataset = active_dataset();
    // guard against a missing dataset or an invalid chart index
    if (dataset == nullptr || chart_index >= dataset->charts.size())
        return;
    // apply the given step selection to the chart
    dataset->charts[chart_index]->set_selected_steps(steps);
    // republish to show the updated selection
    publish_frames();
}

const StepInformation* ChartsRenderer::step_information() const {
    // expose the step information of the active dataset
    const auto* dataset = active_dataset();
    // nothing loaded yet
    return dataset != nullptr ? dataset->step_information : nullptr;
}

void ChartsRenderer::plot_chart_expressions(const size_t chart_index, const std::set<AnyExpression*>& expressions) {
    // active dataset state
    auto* dataset = active_dataset();
    // guard against a missing dataset or an invalid chart index
    if (dataset == nullptr || chart_index >= dataset->charts.size())
        return;
    // plot the given expressions on the chart
    dataset->charts[chart_index]->plot_series(expressions);
    // republish to show the updated selection
    publish_frames();
}

size_t ChartsRenderer::position_to_index(const float position) const {
    // charts of the active dataset
    const auto* dataset = active_dataset();
    // nothing to translate without an active dataset
    if (dataset == nullptr || dataset->charts.empty())
        return 0;
    // clamp the position to [0, 1] and compute the corresponding chart index
    const float clamped = std::clamp(position, 0.0f, 1.0f);
    const size_t index = static_cast<size_t>(clamped * static_cast<float>(dataset->charts.size()));
    return std::min(index, dataset->charts.size() - 1);
}

void ChartsRenderer::zoom_to_fit(const float chart_position) {
    // find the chart index corresponding to the position in the panel
    const size_t chart_index = position_to_index(chart_position);
    // active dataset state
    auto* dataset = active_dataset();
    // log information
    spdlog::debug("User requested zoom to fit on chart at position {} (index {})", chart_position, chart_index);
    // nothing to do without an active dataset
    if (dataset == nullptr)
        return;
    // loop charts
    for (size_t i = 0; i < dataset->charts.size(); i++) {
        // chart at i
        const auto& chart = dataset->charts[i];
        // check if this is the chart that triggered the zoom to fit action
        if (i == chart_index) {
            // reset zoom window
            chart->reset_zoom_window(true, true);
            // next
            continue;
        }
        // update horizontal zoom window only, keep vertical zoom as is
        chart->reset_zoom_window(true, false);
    }
    // republish
    publish_frames();
}

void ChartsRenderer::autorange(const float chart_position) {
    // find the chart index corresponding to the position in the panel
    const size_t chart_index = position_to_index(chart_position);
    // active dataset state
    auto* dataset = active_dataset();
    // log information
    spdlog::debug("User requested autorange on chart at position {} (index {})", chart_position, chart_index);
    // reset vertical zoom only on the selected chart
    if (dataset != nullptr && chart_index < dataset->charts.size()) {
        // reset vertical zoom window
        dataset->charts[chart_index]->reset_zoom_window(false, true);
        // republish
        publish_frames();
    }
}

void ChartsRenderer::zoom_abscissa_extent(const float chart_position) {
    // find the chart index corresponding to the position in the panel
    const size_t chart_index = position_to_index(chart_position);
    // active dataset state
    auto* dataset = active_dataset();
    // log information
    spdlog::debug("User requested zoom abscissa extent on chart at position {} (index {})", chart_position, chart_index);
    // nothing to do without an active dataset
    if (dataset == nullptr)
        return;
    // loop all charts
    for (const auto& chart : dataset->charts) {
        // reset zoom window
        chart->reset_zoom_window(true, false);
    }
    // republish
    publish_frames();
}

void ChartsRenderer::delete_all_plots(const float chart_position) {
    // find the chart index corresponding to the position in the panel
    const size_t chart_index = position_to_index(chart_position);
    // active dataset state
    auto* dataset = active_dataset();
    // log information
    spdlog::debug("User requested deleting all plots on chart at position {} (index {})", chart_position, chart_index);
    // selected chart
    if (dataset != nullptr && chart_index < dataset->charts.size()) {
        // clear chart
        dataset->charts[chart_index]->clear();
        // republish
        publish_frames();
    }
}

void ChartsRenderer::delete_chart(const float chart_position) {
    // find the chart index corresponding to the position in the panel
    const size_t chart_index = position_to_index(chart_position);
    // active dataset state
    auto* dataset = active_dataset();
    // log information
    spdlog::debug("User requested deleting chart at position {} (index {})", chart_position, chart_index);
    // nothing to delete without an active dataset
    if (dataset == nullptr)
        return;
    // delete chart at index
    dataset->charts.erase(dataset->charts.begin() + static_cast<int>(chart_index));
    // ensure at least one chart in panel
    if (dataset->charts.empty())
        add_chart();
    // republish
    publish_frames();
}

void ChartsRenderer::zoom_drag_started(const float x, const float y) {
    // clear active hover readout during drag interactions
    hover_ended();
    // charts of the active dataset
    auto* dataset = active_dataset();
    // nothing to select when no charts exist
    if (dataset == nullptr || dataset->charts.empty())
        return;
    // find the chart whose plot area contains the start point
    for (size_t i = 0; i < dataset->charts.size(); ++i) {
        // plot bounding box for chart i
        const auto [x_min, y_min, x_max, y_max] = plot_rect(i);
        // check if click is inside the plot rectangle
        if (x >= x_min && x <= x_max && y >= y_min && y <= y_max) {
            // retain active chart index
            m_selected_chart_index = i;
            // anchor the selection start point
            m_zoom_selection = {x, y, -1.0f, -1.0f};
            // exit loop
            return;
        }
    }
    // click was outside any plot area
    m_zoom_selection = {-1.0f, -1.0f, -1.0f, -1.0f};
}

void ChartsRenderer::zoom_drag_moved(const float x, const float y) {
    // charts of the active dataset
    auto* dataset = active_dataset();
    // check a drag selection was initiated inside a valid chart
    const auto [x1, y1, x2, y2] = m_zoom_selection;
    if (x1 < 0.0f || y1 < 0.0f || dataset == nullptr || m_selected_chart_index >= dataset->charts.size())
        return;
    // current chart plot bounds
    const auto [x_min, y_min, x_max, y_max] = plot_rect(m_selected_chart_index);
    // clamp mouse coordinates to the active plot area
    const float clamped_x = std::clamp(x, x_min, x_max);
    const float clamped_y = std::clamp(y, y_min, y_max);
    // update zoom selection box
    m_zoom_selection = {x1, y1, clamped_x, clamped_y};
}

void ChartsRenderer::zoom_drag_ended() {
    // charts of the active dataset
    auto* dataset = active_dataset();
    // check a valid zoom selection exists
    const auto [x1, y1, x2, y2] = m_zoom_selection;
    const bool valid_zoom = dataset != nullptr && m_selected_chart_index < dataset->charts.size() && x1 >= 0.0f && y1 >= 0.0f && x2 >= 0.0f && y2 >= 0.0f && std::abs(x2 - x1) > 10.0f && std::abs(y2 - y1) > 10.0f;
    if (valid_zoom) {
        // current chart plot bounds
        const auto [x_min, y_min, x_max, y_max] = plot_rect(m_selected_chart_index);
        // plot dimensions
        const float width = x_max - x_min;
        const float height = y_max - y_min;
        // ensure non-zero plot dimensions
        if (width > 0.0f && height > 0.0f) {
            // compute normalized zoom window ratios [0..1]
            const double z_x1 = (static_cast<double>(std::min(x1, x2)) - x_min) / width;
            const double z_y1 = (static_cast<double>(std::min(y1, y2)) - y_min) / height;
            const double z_x2 = (static_cast<double>(std::max(x1, x2)) - x_min) / width;
            const double z_y2 = (static_cast<double>(std::max(y1, y2)) - y_min) / height;
            // loop all charts to apply zoom
            for (size_t i = 0; i < dataset->charts.size(); ++i) {
                // chart at index i
                const auto& chart = dataset->charts[i];
                // check if this is the chart that triggered the zoom action
                if (i == m_selected_chart_index) {
                    // log information
                    spdlog::debug("Updating zoom window in chart at index [{}] to [{}, {}, {}, {}]", i, z_x1, z_y1, z_x2, z_y2);
                    // update 2D zoom window in selected chart
                    chart->update_zoom_window(z_x1, z_x2, z_y1, z_y2);
                }
                else {
                    // log information
                    spdlog::debug("Updating zoom window in chart at index [{}] to [{}, {}, {}, {}]", i, z_x1, z_y1, -1, -1);
                    // update horizontal zoom window only, keep vertical zoom as is
                    chart->update_zoom_window(z_x1, z_x2, -1, -1);
                }
            }
        }
    }
    // reset zoom selection
    m_zoom_selection = {-1.0f, -1.0f, -1.0f, -1.0f};
    // republish
    publish_frames();
    // update hover readout at the release position when zoom was performed
    if (valid_zoom)
        hover_moved(x2, y2);
}

void ChartsRenderer::zoom_drag_canceled() {
    // reset zoom selection
    m_zoom_selection = {-1.0f, -1.0f, -1.0f, -1.0f};
    // republish
    publish_frames();
}

void ChartsRenderer::hover_moved(const float x, const float y) {
    // charts of the active dataset
    auto* dataset = active_dataset();
    // nothing to hover without charts
    if (dataset == nullptr || dataset->charts.empty()) {
        // clear hover state
        hover_ended();
        // exit
        return;
    }
    // look for the chart whose plot area contains the cursor
    m_hover_in_plot = false;
    // loop charts
    for (size_t i = 0; i < dataset->charts.size(); ++i) {
        // current chart plot bounds
        const auto [px_min, py_min, px_max, py_max] = plot_rect(i);
        // check the cursor is inside the plot area
        if (x >= px_min && x <= px_max && y >= py_min && y <= py_max && (px_max - px_min) > 0.0f) {
            // chart index being hovered
            m_hover_chart_index = i;
            // set plot flag
            m_hover_in_plot = true;
            // ratio of the cursor within the plot area
            const double ratio = static_cast<double>(x - px_min) / static_cast<double>(px_max - px_min);
            // scale-aware abscissa value at the ratio
            m_hover_abscissa_value = dataset->charts[i]->plot_ratio_to_abscissa_value(ratio);
            // only the first matching chart is considered
            break;
        }
    }
    // cursor is inside a plot area
    if (m_hover_in_plot) {
        // restart the debounce timer (one-shot, fires 20ms after the last move)
        m_hover_timer.start(slint::TimerMode::SingleShot, std::chrono::milliseconds(20), [this] { publish_hover(); });
    }
    else {
        // cursor is outside any plot area
        hover_ended();
    }
}

void ChartsRenderer::hover_ended() {
    // cancel any pending hover publication timer
    m_hover_timer.stop();
    // clear plot hover flag
    m_hover_in_plot = false;
    // no hover text is currently active
    if (m_last_hover_text.empty())
        return;
    // clear last hover text
    m_last_hover_text.clear();
    // notify callback with empty string to restore status text
    if (m_hover_callback)
        m_hover_callback("");
}

void ChartsRenderer::publish_hover() {
    // hover text to publish, empty restores the previous status bar text
    std::string text;
    // active dataset state
    auto* dataset = active_dataset();
    // cursor is inside the plot area of a valid chart
    if (dataset != nullptr && m_hover_in_plot && m_hover_chart_index < dataset->charts.size()) {
        // series values as a single string for the current abscissa value
        text = dataset->charts[m_hover_chart_index]->hovered_series_text(m_hover_abscissa_value);
    }
    // publish when the hover text has changed
    if (text != m_last_hover_text) {
        // update last hover text
        m_last_hover_text = text;
        // invoke hover callback
        if (m_hover_callback)
            m_hover_callback(text);
    }
}

std::tuple<float, float, float, float> ChartsRenderer::plot_rect(const size_t chart_index) const {
    // invalid rect when no frames are published or the index is out of range
    if (chart_index >= m_plot_rects.size())
        return {-1.0f, -1.0f, -1.0f, -1.0f};
    // last published panel-relative plot rect
    return m_plot_rects[chart_index];
}

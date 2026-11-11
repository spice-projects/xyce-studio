#include "charts/charts_controller.h"

#include <algorithm>
#include <cmath>
#include <utility>

studio::ChartsController::ChartsController(const float width, const float height, TextMeasurer measure) :
    m_layout(std::move(measure)), m_width(width), m_height(height) {}

void studio::ChartsController::set_dataset(ExpressionManager& expression_manager, const StepInformation& step_information, const AbscissaScale abscissa_scale) {
    // remember the dataset every new chart reads
    m_expression_manager = &expression_manager;
    m_step_information = &step_information;
    m_abscissa_scale = abscissa_scale;
    // drop the charts of the previous dataset
    clear_charts();
}

void studio::ChartsController::set_viewport(const float width, const float height) {
    // ignore a degenerate panel, which no chart can fill
    if (width <= 0.0f || height <= 0.0f)
        return;
    // remember the panel size the frames are laid out for
    m_width = width;
    m_height = height;
    // rebuild every frame for the new geometry
    rebuild_frames();
}

float studio::ChartsController::width() const { return m_width; }

float studio::ChartsController::height() const { return m_height; }

ChartEngine* studio::ChartsController::add_chart(const ChartKind kind) {
    // a chart needs a dataset to resolve its abscissa and steps
    if (m_expression_manager == nullptr || m_step_information == nullptr)
        return nullptr;
    // build the engine over the active dataset
    auto engine = std::make_unique<ChartEngine>(m_expression_manager, m_step_information, m_abscissa_scale, 4000, kind);
    // keep the engine and hand its address to the caller
    ChartEngine* chart = engine.get();
    // append the chart to the stack
    m_charts.push_back(std::move(engine));
    // lay the new chart out with its siblings
    rebuild_frames();
    // hand the caller the chart it drives
    return chart;
}

void studio::ChartsController::clear_charts() {
    // release every engine and its frame
    m_charts.clear();
    m_frames.clear();
    // clear the pointer state that named a chart
    zoom_drag_canceled();
    hover_ended();
    // the dropped chart index belongs to the previous dataset
    m_zoom_chart = 0;
}

std::size_t studio::ChartsController::chart_count() const { return m_charts.size(); }

ChartEngine* studio::ChartsController::chart(const std::size_t index) {
    // a chart index past the stack has no engine
    if (index >= m_charts.size())
        return nullptr;
    // hand out the engine the caller drives
    return m_charts[index].get();
}

const ChartEngine* studio::ChartsController::chart(const std::size_t index) const {
    // a chart index past the stack has no engine
    if (index >= m_charts.size())
        return nullptr;
    // hand out the engine the caller reads
    return m_charts[index].get();
}

void studio::ChartsController::plot_expressions(const std::size_t index, const std::set<AnyExpression*>& expressions) {
    // a chart index past the stack has no chart to plot on
    ChartEngine* engine = chart(index);
    // nothing to plot without an engine
    if (engine == nullptr)
        return;
    // replace the plotted series of this chart
    engine->plot_series(expressions);
    // republish the frame the series changed
    rebuild_frames();
}

void studio::ChartsController::set_selected_steps(const std::size_t index, const std::set<std::size_t>& steps) {
    // a chart index past the stack has no chart to drive
    ChartEngine* engine = chart(index);
    // nothing to select without an engine
    if (engine == nullptr)
        return;
    // apply the step selection and replot the retained series
    engine->set_selected_steps(steps);
    // republish the frame the selection changed
    rebuild_frames();
}

void studio::ChartsController::reset_zoom(const std::size_t index, const bool horizontal, const bool vertical) {
    // a chart index past the stack has no zoom to reset
    ChartEngine* engine = chart(index);
    // nothing to reset without an engine
    if (engine == nullptr)
        return;
    // reset the requested zoom axes and replot
    engine->reset_zoom_window(horizontal, vertical);
    // republish the frame the zoom changed
    rebuild_frames();
}

void studio::ChartsController::move_chart(const std::size_t from, const std::size_t to) {
    // a move needs two distinct positions inside the stack
    if (from >= m_charts.size() || to >= m_charts.size() || from == to)
        return;
    // a reordered chart invalidates any readout and drag that named its old index
    hover_ended();
    // cancel a drag before the indices move
    zoom_drag_canceled();
    // take the chart out of its old position
    std::unique_ptr<ChartEngine> moved = std::move(m_charts[from]);
    // close the hole the chart left
    m_charts.erase(m_charts.begin() + static_cast<std::ptrdiff_t>(from));
    // insert the chart at its final position
    m_charts.insert(m_charts.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    // republish the frames in their new order
    rebuild_frames();
}

std::size_t studio::ChartsController::position_to_index(const float y) const {
    // no chart means there is no index to name
    if (m_charts.empty() || m_height <= 0.0f)
        return 0;
    // clamp the position into the panel before slicing it
    const float ratio = std::clamp(y / m_height, 0.0f, 0.999999f);
    // the slice is the chart the position lands in
    return std::min(static_cast<std::size_t>(ratio * static_cast<float>(m_charts.size())), m_charts.size() - 1);
}

std::optional<std::size_t> studio::ChartsController::chart_at(const float x, const float y) const {
    // try every chart, because the plots are stacked and may have gaps
    for (std::size_t index = 0; index < m_frames.size(); ++index) {
        // read the plot rect this chart was laid out into
        const auto [left, top, right, bottom] = plot_rect(index);
        // a point inside the plot belongs to this chart
        if (x >= left && x <= right && y >= top && y <= bottom)
            return index;
    }
    // a point outside every plot belongs to no chart
    return std::nullopt;
}

std::tuple<float, float, float, float> studio::ChartsController::plot_rect(const std::size_t index) const {
    // a chart index past the built frames has no plot rect
    if (index >= m_frames.size() || m_charts.empty())
        return {0.0f, 0.0f, 0.0f, 0.0f};
    // the chart slot starts at its share of the panel height
    const float offset = m_height / static_cast<float>(m_charts.size()) * static_cast<float>(index);
    // the frame carries plot geometry relative to the chart slot
    const ChartFrame& frame = m_frames[index];
    // map the frame rect into panel coordinates
    return {frame.plot_x, frame.plot_y + offset, frame.plot_x + frame.plot_w, frame.plot_y + frame.plot_h + offset};
}

bool studio::ChartsController::zoom_drag_started(const float x, const float y) {
    // a press outside a plot starts no zoom
    const std::optional<std::size_t> index = chart_at(x, y);
    // nothing to anchor without a chart
    if (!index)
        return false;
    // the smith plane is fixed at +-1, so it never zooms
    if (m_charts[*index]->kind() == ChartKind::SMITH)
        return false;
    // remember the chart the drag belongs to
    m_zoom_chart = *index;
    // anchor the drag at the press point
    m_zoom_start = std::make_pair(x, y);
    // start the selection as a point
    m_zoom_current = m_zoom_start;
    // the drag owns the pointer until it ends
    m_zoom_active = true;
    // report the press was claimed
    return true;
}

bool studio::ChartsController::zoom_drag_moved(const float x, const float y) {
    // a move without an active drag selects nothing
    if (!m_zoom_active || !m_zoom_start)
        return false;
    // read the plot rect the drag is clamped to
    const auto [left, top, right, bottom] = plot_rect(m_zoom_chart);
    // clamp the moving corner into the active plot
    m_zoom_current = std::make_pair(std::clamp(x, left, right), std::clamp(y, top, bottom));
    // report the move was consumed
    return true;
}

bool studio::ChartsController::zoom_drag_ended() {
    // a release without an active drag changes nothing
    if (!m_zoom_active || !m_zoom_start || !m_zoom_current) {
        // drop any partial selection the release found
        zoom_drag_canceled();
        // report no chart changed
        return false;
    }
    // read the drag corners before the selection is cleared
    const auto [x1, y1] = *m_zoom_start;
    // read the moving corner before the selection is cleared
    const auto [x2, y2] = *m_zoom_current;
    // read the active chart before the pointer state is reset
    const std::size_t chart_index = m_zoom_chart;
    // clear the drag so a failed validation leaves no selection
    m_zoom_active = false;
    // drop the anchor the completed drag no longer needs
    m_zoom_start.reset();
    // drop the moving corner the completed drag no longer needs
    m_zoom_current.reset();
    // a tiny drag is a click, not a zoom window
    if (std::abs(x2 - x1) < 10.0f || std::abs(y2 - y1) < 10.0f)
        return false;
    // read the plot rect the drag measured against
    const auto [left, top, right, bottom] = plot_rect(chart_index);
    // the plot needs a non zero extent to normalize the drag
    if (right <= left || bottom <= top)
        return false;
    // normalize the horizontal window into the plot
    const double x_left_ratio = (static_cast<double>(std::min(x1, x2)) - left) / (right - left);
    // normalize the right horizontal edge into the plot
    const double x_right_ratio = (static_cast<double>(std::max(x1, x2)) - left) / (right - left);
    // normalize the top vertical edge into the plot
    const double y_top_ratio = (static_cast<double>(std::min(y1, y2)) - top) / (bottom - top);
    // normalize the bottom vertical edge into the plot
    const double y_bottom_ratio = (static_cast<double>(std::max(y1, y2)) - top) / (bottom - top);
    // apply the selection to every chart of the dataset
    for (std::size_t index = 0; index < m_charts.size(); ++index) {
        // the dragged chart takes the full two dimensional window
        if (index == chart_index)
            m_charts[index]->update_zoom_window(x_left_ratio, x_right_ratio, y_top_ratio, y_bottom_ratio);
        // every other chart joins the shared abscissa range only
        else
            m_charts[index]->update_zoom_window(x_left_ratio, x_right_ratio, -1.0, -1.0);
    }
    // republish the frames the zoom changed
    rebuild_frames();
    // report a chart changed
    return true;
}

void studio::ChartsController::zoom_drag_canceled() {
    // the drag no longer owns the pointer
    m_zoom_active = false;
    // drop the drag anchor
    m_zoom_start.reset();
    // drop the moving corner
    m_zoom_current.reset();
}

std::optional<std::tuple<float, float, float, float>> studio::ChartsController::zoom_selection() const {
    // a drag that never moved has no rectangle to draw
    if (!m_zoom_active || !m_zoom_start || !m_zoom_current)
        return std::nullopt;
    // read the anchor corner
    const auto [x1, y1] = *m_zoom_start;
    // read the moving corner
    const auto [x2, y2] = *m_zoom_current;
    // hand out the normalized rectangle
    return std::make_tuple(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));
}

std::string studio::ChartsController::hover_moved(const float x, const float y) {
    // a pointer outside every plot clears the readout
    const std::optional<std::size_t> index = chart_at(x, y);
    // nothing to read out without a chart
    if (!index) {
        // clear the stale readout from the previous chart
        hover_ended();
        // report no hover text
        return {};
    }
    // read the plot rect the pointer landed in
    const auto [left, top, right, bottom] = plot_rect(*index);
    // a degenerate plot has no position to read
    if (right <= left || bottom <= top) {
        // clear the stale readout from the previous chart
        hover_ended();
        // report no hover text
        return {};
    }
    // the chart that owns the readout
    ChartEngine* engine = m_charts[*index].get();
    // the horizontal fraction of the pointer inside the plot
    const double x_ratio = (static_cast<double>(x) - left) / (right - left);
    // smith charts read out the gamma plane instead of an abscissa value
    if (engine->kind() == ChartKind::SMITH) {
        // the vertical fraction of the pointer inside the plot
        const double y_ratio = (static_cast<double>(y) - top) / (bottom - top);
        // map the fractions onto the fixed +-1 gamma plane
        const double gamma_r = x_ratio * 2.0 - 1.0;
        // map the vertical fraction with the positive imaginary axis at the top
        const double gamma_i = 1.0 - y_ratio * 2.0;
        // remember the chart the readout belongs to
        m_hover_chart = index;
        // compute the nearest trace readout
        m_hover_text = engine->hovered_smith_text(gamma_r, gamma_i);
        // hand out the readout
        return m_hover_text;
    }
    // map the horizontal fraction onto the visible abscissa window
    const double abscissa = engine->plot_ratio_to_abscissa_value(x_ratio);
    // remember the chart the readout belongs to
    m_hover_chart = index;
    // compute the interpolated series readout
    m_hover_text = engine->hovered_series_text(abscissa);
    // hand out the readout
    return m_hover_text;
}

void studio::ChartsController::hover_ended() {
    // drop the readout text
    m_hover_text.clear();
    // drop the chart the readout belonged to
    m_hover_chart.reset();
}

const std::string& studio::ChartsController::hover_text() const { return m_hover_text; }

std::optional<std::size_t> studio::ChartsController::hover_chart() const { return m_hover_chart; }

void studio::ChartsController::rebuild_frames() {
    // drop the frames of the previous state
    m_frames.clear();
    // no chart or no panel has no frame to build
    if (m_charts.empty() || m_width <= 0.0f || m_height <= 0.0f)
        return;
    // the charts share the panel height equally
    const float chart_height = m_height / static_cast<float>(m_charts.size());
    // build one frame per chart from its engine state
    for (const auto& engine : m_charts)
        m_frames.push_back(m_layout.build(*engine, m_width, chart_height));
}

const std::vector<ChartFrame>& studio::ChartsController::frames() const { return m_frames; }

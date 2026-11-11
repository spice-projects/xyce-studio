#include "presenters/charts_presenter.h"

#include <cctype>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/util.h"
#include "io/touchstone_file.h"
#include "io/xyce_csd_file.h"
#include "io/xyce_csv_file.h"
#include "io/xyce_output_file.h"
#include "io/xyce_prn_file.h"
#include "io/xyce_raw_file.h"
#include "io/xyce_tecplot_file.h"
#include "views/charts/charts_view.h"

studio::ChartsPresenter::ChartsPresenter(ChartsView& view, albor::TextEngine& text) :
    m_text(text), m_controller(800.0f, 600.0f, [this](const std::string& label) { return m_text.measure(label); }), m_builder(m_resources, m_text), m_view(&view) {
    // install the zoom callbacks the view reports
    m_view->set_on_zoom_drag_started([this](const float x, const float y) {
        // a press inside a plot starts the selection overlay
        if (m_controller.zoom_drag_started(x, y))
            publish_overlay();
    });
    // install the moving corner callback
    m_view->set_on_zoom_drag_moved([this](const float x, const float y) {
        // a move updates the selection overlay
        if (m_controller.zoom_drag_moved(x, y))
            publish_overlay();
    });
    // install the release callback
    m_view->set_on_zoom_drag_ended([this] {
        // apply the completed window before the overlay clears
        const bool changed = m_controller.zoom_drag_ended();
        // drop the transient selection
        m_view->set_overlay(nullptr);
        // republish the charts when the window changed
        if (changed)
            publish_scene();
    });
    // install the cancel callback
    m_view->set_on_zoom_drag_canceled([this] {
        // drop the selection without changing the charts
        m_controller.zoom_drag_canceled();
        // remove the overlay from the view
        m_view->set_overlay(nullptr);
    });
    // install the hover callback
    m_view->set_on_hover_moved([this](const float x, const float y) {
        // publish the readout the pointer resolves
        if (m_on_hover)
            m_on_hover(m_controller.hover_moved(x, y));
    });
    // install the hover leave callback
    m_view->set_on_hover_ended([this] {
        // clear the controller state and the shell readout
        m_controller.hover_ended();
        // restore the status bar text
        if (m_on_hover)
            m_on_hover({});
    });
    // install the resize settle callback
    m_view->set_on_resize_settled([this](const float width, const float height) { set_viewport(width, height); });
    // the view clears the borrowed pointer before the window tree frees it
    m_view->set_on_destroyed([this] { m_view = nullptr; });
}

studio::ChartsPresenter::~ChartsPresenter() { detach(); }

void studio::ChartsPresenter::detach() {
    // the window teardown may already have dropped the view
    if (m_view == nullptr)
        return;
    // clear every callback the presenter installed
    m_view->set_on_zoom_drag_started({});
    // clear the moving corner callback
    m_view->set_on_zoom_drag_moved({});
    // clear the release callback
    m_view->set_on_zoom_drag_ended({});
    // clear the cancel callback
    m_view->set_on_zoom_drag_canceled({});
    // clear the hover callback
    m_view->set_on_hover_moved({});
    // clear the hover leave callback
    m_view->set_on_hover_ended({});
    // clear the resize callback
    m_view->set_on_resize_settled({});
    // clear the destructor callback the view would otherwise call into a detached presenter
    m_view->set_on_destroyed({});
    // the view is no longer reachable from the presenter
    m_view = nullptr;
}

bool studio::ChartsPresenter::show_file(const std::filesystem::path& path) {
    // a detached presenter has no surface to show the dataset on
    if (m_view == nullptr)
        return false;
    // the extension decides which output parser reads the file
    const std::string extension = to_lower(path.extension().string());
    // the parsed dataset under construction
    std::optional<std::shared_ptr<XyceOutputFile>> parsed;
    // a raw binary plot file carries the analysis blocks
    if (extension == ".raw")
        parsed = xyce_raw_file_parser(path);
    // a print output file carries the column table
    else if (extension == ".prn")
        parsed = xyce_prn_file_parser(path);
    // a probe output file carries the complex columns
    else if (extension == ".csd")
        parsed = xyce_csd_file_parser(path);
    // a csv output file carries the delimiter separated table
    else if (extension == ".csv")
        parsed = xyce_csv_file_parser(path);
    // a tecplot output file carries the dat table
    else if (extension == ".dat")
        parsed = xyce_tecplot_file_parser(path);
    // a touchstone file carries s parameter data, which plots on the smith plane
    else if (extension == ".ts" || (extension.size() == 4 && extension[1] == 's' && extension[3] == 'p'))
        parsed = touchstone_file_parser(path);
    // a failed or unsupported parse leaves the shell untouched
    if (!parsed || !*parsed) {
        // report the file the charts refused
        spdlog::warn("Charts: unsupported or unreadable output file {}", path.string());
        // report the failure to the caller
        return false;
    }
    // touchstone data plots on the smith plane, every other format plots cartesian
    load_dataset(std::move(*parsed), extension == ".ts" || (extension.size() == 4 && extension[1] == 's' && extension[3] == 'p') ? ChartKind::SMITH : ChartKind::XY);
    // report the dataset loaded
    return true;
}

void studio::ChartsPresenter::load_dataset(std::shared_ptr<XyceOutputFile> file, const ChartKind kind) {
    // release the engines before the dataset they read leaves
    m_controller.clear_charts();
    // a null dataset leaves the panel empty
    if (file == nullptr) {
        // drop the dataset that owned the expressions
        m_file.reset();
        // drop the title the empty panel no longer shows
        m_title.clear();
        // drop the retained picture of the old dataset
        m_scene.reset();
        // a live view swaps to the empty panel
        if (m_view != nullptr) {
            m_view->set_overlay(nullptr);
            m_view->set_scene(nullptr);
            m_view->set_accessible_summary("0 charts");
        }
        // the empty dataset is settled
        return;
    }
    // keep the dataset alive for as long as its charts reference it
    m_file = std::move(file);
    // remember the title the shell may show
    m_title = m_file->title();
    // point the controller at the dataset expressions and steps
    m_controller.set_dataset(m_file->expression_manager(), m_file->step_information(), m_file->abscissa_scale());
    // use the arranged panel size, or a useful default before the first arrange
    const float width = m_view != nullptr && m_view->viewport_width() > 0.0f ? m_view->viewport_width() : 800.0f;
    // use the arranged panel height, or a useful default before the first arrange
    const float height = m_view != nullptr && m_view->viewport_height() > 0.0f ? m_view->viewport_height() : 600.0f;
    // lay the charts out for the panel size
    m_controller.set_viewport(width, height);
    // build the charts the dataset suggests
    build_charts(kind);
    // publish the base picture
    publish_scene();
}

void studio::ChartsPresenter::build_charts(const ChartKind kind) {
    // read the manager the dataset expressions resolve through
    ExpressionManager& manager = m_file->expression_manager();
    // a smith chart plots the diagonal complex reflection entries, not the suggested db() magnitudes
    if (kind == ChartKind::SMITH) {
        // create the single smith chart the dataset describes
        ChartEngine* chart = m_controller.add_chart(kind);
        // a refused chart leaves the panel empty
        if (chart == nullptr)
            return;
        // the diagonal s parameter entries the smith plane plots
        std::set<AnyExpression*> expressions;
        // scan the expression names in their stored order
        for (const std::string& name : manager.expression_names()) {
            // a diagonal entry is named S11, S22, and so on
            if (name.size() < 3 || !std::isupper(static_cast<unsigned char>(name[0])) || name[1] != name[2])
                continue;
            // resolve the entry by name
            AnyExpression* expression = manager.evaluate(name);
            // only a complex reflection coefficient belongs on the smith plane
            if (expression != nullptr && std::holds_alternative<Expression<std::complex<double>>>(*expression))
                expressions.insert(expression);
        }
        // a dataset without diagonal entries falls back to every complex s parameter
        if (expressions.empty()) {
            // scan the expression names again
            for (const std::string& name : manager.expression_names()) {
                // only an s parameter belongs on the smith plane
                if (name.size() < 3 || !std::isupper(static_cast<unsigned char>(name[0])))
                    continue;
                // resolve the entry by name
                AnyExpression* expression = manager.evaluate(name);
                // only a complex reflection coefficient belongs on the smith plane
                if (expression != nullptr && std::holds_alternative<Expression<std::complex<double>>>(*expression))
                    expressions.insert(expression);
            }
        }
        // plot the diagonal entries on the single chart
        m_controller.plot_expressions(0, expressions);
        // the smith chart is complete
        return;
    }
    // a dataset with suggested plots builds one chart per group
    if (!m_file->suggested_plots().empty()) {
        // build one chart per suggested group
        for (const std::vector<std::string>& group : m_file->suggested_plots()) {
            // create the chart before resolving its expressions
            ChartEngine* chart = m_controller.add_chart(kind);
            // a refused chart ends the build
            if (chart == nullptr)
                break;
            // the expressions the group resolves to
            std::set<AnyExpression*> expressions;
            // resolve every name the group carries
            for (const std::string& name : group) {
                // a name the manager cannot resolve is skipped
                if (AnyExpression* expression = manager.evaluate(name); expression != nullptr)
                    expressions.insert(expression);
            }
            // plot the resolved group on the new chart
            m_controller.plot_expressions(m_controller.chart_count() - 1, expressions);
        }
        // the suggested charts are complete
        return;
    }
    // no suggestion means one chart over the first few plottable expressions
    ChartEngine* chart = m_controller.add_chart(kind);
    // a refused chart leaves the panel empty
    if (chart == nullptr)
        return;
    // the expressions the default chart plots
    std::set<AnyExpression*> expressions;
    // scan the dataset expressions in their stored order
    for (const std::string& name : manager.expression_names()) {
        // stop when the default chart carries enough series
        if (expressions.size() >= 3)
            break;
        // the abscissa is not a plotted series
        if (name == manager.abscissa().name())
            continue;
        // resolve the expression by name
        AnyExpression* expression = manager.evaluate(name);
        // an unresolved expression is skipped
        if (expression == nullptr)
            continue;
        // a smith chart only plots complex reflection data
        if (kind == ChartKind::SMITH && !std::holds_alternative<Expression<std::complex<double>>>(*expression))
            continue;
        // keep the expression for the default plot
        expressions.insert(expression);
    }
    // plot the default expressions on the single chart
    m_controller.plot_expressions(0, expressions);
}

void studio::ChartsPresenter::set_viewport(const float width, const float height) {
    // ignore a degenerate panel the layout cannot use
    if (width <= 0.0f || height <= 0.0f)
        return;
    // resize the chart layout
    m_controller.set_viewport(width, height);
    // republish the scene at the new size
    if (m_file != nullptr)
        publish_scene();
}

void studio::ChartsPresenter::set_on_hover(std::function<void(const std::string&)> handler) { m_on_hover = std::move(handler); }

void studio::ChartsPresenter::set_dark_mode(const bool dark) {
    // an unchanged scheme republishes nothing
    if (m_dark_mode == dark)
        return;
    // remember the scheme the next scene records
    m_dark_mode = dark;
    // repaint the active dataset with the new palette
    if (m_file != nullptr && m_view != nullptr)
        publish_scene();
}

bool studio::ChartsPresenter::has_dataset() const { return m_file != nullptr; }

std::size_t studio::ChartsPresenter::chart_count() const { return m_controller.chart_count(); }

const std::string& studio::ChartsPresenter::title() const { return m_title; }

const studio::ChartScene* studio::ChartsPresenter::scene() const { return m_scene.get(); }

std::size_t studio::ChartsPresenter::series_count(const std::size_t chart_index) const {
    // a chart index past the frames has no series
    const std::vector<ChartFrame>& frames = m_controller.frames();
    // a chart index past the built frames has no series
    if (chart_index >= frames.size())
        return 0;
    // hand out the runs the chart frame carries
    return frames[chart_index].series.size();
}

void studio::ChartsPresenter::publish_scene() {
    // a detached presenter has no view to publish to
    if (m_view == nullptr)
        return;
    // the retained scene for the current controller state
    m_scene = m_builder.build(m_controller.frames(), m_controller.width(), m_controller.height(), chart_palette(m_dark_mode));
    // name the chart kinds for automation and assistive technology
    const std::size_t count = m_controller.chart_count();
    // smith and cartesian scenes read out the kind of their first chart
    const bool smith = !m_controller.frames().empty() && m_controller.frames().front().smith;
    // the summary carries the count and the chart kind
    m_view->set_accessible_summary(std::to_string(count) + (smith ? " smith chart" : " line chart") + (count == 1 ? "" : "s"));
    // hand the retained picture to the view
    m_view->set_scene(m_scene);
}

void studio::ChartsPresenter::publish_overlay() {
    // a detached presenter has no view to publish to
    if (m_view == nullptr)
        return;
    // the selection the controller currently reports
    const std::optional<std::tuple<float, float, float, float>> selection = m_controller.zoom_selection();
    // a drag that has not moved has no overlay
    if (!selection) {
        // remove the previous overlay from the view
        m_view->set_overlay(nullptr);
        // nothing else to record
        return;
    }
    // read the normalized corners of the selection
    const auto [left, top, right, bottom] = *selection;
    // build the rectangle the overlay fills
    const albor::Rect rect = albor::Rect::from_origin_size(albor::Point{.x = left, .y = top}, albor::Size{.width = right - left, .height = bottom - top});
    // record the overlay and hand it to the view
    m_view->set_overlay(m_builder.build_zoom_overlay(rect));
}

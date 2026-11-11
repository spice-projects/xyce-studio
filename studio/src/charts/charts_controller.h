#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "charts/chart_engine.h"
#include "charts/chart_layout.h"

namespace studio
{
    // owns the chart engines of one dataset and maps pointer interactions onto them
    class ChartsController final
    {
    public:
        // the label measurer the layout uses for tick, legend and smith labels
        using TextMeasurer = ChartLayout::TextMeasurer;

        // build a controller for a panel of the given logical size
        ChartsController(float width, float height, TextMeasurer measure);

        ChartsController(const ChartsController&) = delete;

        ChartsController& operator=(const ChartsController&) = delete;

        // point the controller at the dataset every chart of this tab reads
        void set_dataset(ExpressionManager& expression_manager, const StepInformation& step_information, AbscissaScale abscissa_scale);

        // repoint the charts at a replacement dataset, preserving zoom windows and step selections
        void update_dataset(ExpressionManager& expression_manager, const StepInformation& step_information, AbscissaScale abscissa_scale);

        // replace the panel size and rebuild every frame
        void set_viewport(float width, float height);

        // hand out the panel size the frames were built for
        [[nodiscard]] float width() const;

        [[nodiscard]] float height() const;

        // create a chart in the active dataset, returning null when no dataset is set
        ChartEngine* add_chart(ChartKind kind = ChartKind::XY);

        // drop every chart and its frame
        void clear_charts();

        // hand out how many charts the active dataset holds
        [[nodiscard]] std::size_t chart_count() const;

        // hand out the chart at an index, or null when the index is past the end
        [[nodiscard]] ChartEngine* chart(std::size_t index);

        [[nodiscard]] const ChartEngine* chart(std::size_t index) const;

        // plot the given expressions on one chart and rebuild its frame
        void plot_expressions(std::size_t index, const std::set<AnyExpression*>& expressions);

        // apply a step selection to one chart and rebuild its frame
        void set_selected_steps(std::size_t index, const std::set<std::size_t>& steps);

        // reset the zoom window of one chart
        void reset_zoom(std::size_t index, bool horizontal, bool vertical);

        // move a chart to another position in the stack
        void move_chart(std::size_t from, std::size_t to);

        // map a panel relative Y position onto a chart index
        [[nodiscard]] std::size_t position_to_index(float y) const;

        // find the chart whose plot rect contains a panel relative point
        [[nodiscard]] std::optional<std::size_t> chart_at(float x, float y) const;

        // hand out the panel relative plot rect of one chart as left, top, right, bottom
        [[nodiscard]] std::tuple<float, float, float, float> plot_rect(std::size_t index) const;

        // start a zoom drag inside a plot, returning whether a chart claimed the press
        bool zoom_drag_started(float x, float y);

        // move the zoom drag selection, clamped to the active plot
        bool zoom_drag_moved(float x, float y);

        // apply the zoom window of the completed drag, returning whether it changed a chart
        bool zoom_drag_ended();

        // drop the zoom drag selection without changing the charts
        void zoom_drag_canceled();

        // hand out the zoom selection rectangle in panel coordinates
        [[nodiscard]] std::optional<std::tuple<float, float, float, float>> zoom_selection() const;

        // compute the hover readout at a panel relative point, or an empty string outside a plot
        [[nodiscard]] std::string hover_moved(float x, float y);

        // clear the hover readout
        void hover_ended();

        // hand out the last hover readout, which the shell status bar renders
        [[nodiscard]] const std::string& hover_text() const;

        // hand out the chart the last hover readout belongs to
        [[nodiscard]] std::optional<std::size_t> hover_chart() const;

        // rebuild every frame from the chart engine state
        void rebuild_frames();

        // hand out the last built frames
        [[nodiscard]] const std::vector<ChartFrame>& frames() const;

    private:
        // the label measurer the layout resolves through
        ChartLayout m_layout;

        // the engines of the active dataset, one per chart
        std::vector<std::unique_ptr<ChartEngine>> m_charts;

        // the frames the layout built for the current viewport and state
        std::vector<ChartFrame> m_frames;

        // the dataset the engines read
        ExpressionManager* m_expression_manager = nullptr;

        // the step information of the active dataset
        const StepInformation* m_step_information = nullptr;

        // the abscissa scale of the active dataset
        AbscissaScale m_abscissa_scale = AbscissaScale::LINEAR;

        // the panel size the frames were built for
        float m_width = 0.0f;

        float m_height = 0.0f;

        // the chart the active zoom drag started in
        std::size_t m_zoom_chart = 0;

        // the zoom drag anchor in panel coordinates
        std::optional<std::pair<float, float>> m_zoom_start;

        // the zoom drag current point in panel coordinates
        std::optional<std::pair<float, float>> m_zoom_current;

        // whether a zoom drag owns the pointer
        bool m_zoom_active = false;

        // the last hover readout, which the shell renders
        std::string m_hover_text;

        // the chart the last hover readout belongs to
        std::optional<std::size_t> m_hover_chart;
    };
} // namespace studio

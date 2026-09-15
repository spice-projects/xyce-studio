#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <slint.h>

#include "../charts/chart_engine.h"
#include "../charts/chart_layout.h"

class ExpressionManager;
class StepInformation;

// composes slint native chart frames from the chart engine state and publishes
// them into the slint ui; one implementation for every platform
class ChartsRenderer
{
public:
    // sink for slint native chart frames of the active dataset
    using PublishFramesFunction = std::function<void(const std::vector<ChartFrame>&)>;

    // construct with the publish sink for slint native chart frames
    explicit ChartsRenderer(PublishFramesFunction publish);

    ChartsRenderer(const ChartsRenderer&) = delete;

    ChartsRenderer& operator=(const ChartsRenderer&) = delete;

    // logical panel size; frames recompute for the new size
    void set_viewport(float width, float height);

    // adapt the frame palette to light/dark theme changes
    void set_dark_mode(bool dark_mode);

    // clear the viewport so no frames are published until a valid size arrives
    void reset_viewport();

    // data path, wired on dataset activation; datasets are identified by the
    // owning plot tab id so each tab keeps its own charts and zoom state and
    // switching tabs restores the previous state instead of rebuilding
    void update(int dataset_id, ExpressionManager& expression_manager, const StepInformation& step_information, AbscissaScale abscissa_scale, const std::vector<std::vector<std::string>>& suggested_plots);

    ChartEngine* add_chart();

    // add a new chart directly after the chart at the given index; an out of
    // range index appends the chart at the end of the stack
    ChartEngine* add_chart(size_t after_index);

    // drop the chart state of the dataset with the given tab id
    void release_dataset(int dataset_id);

    // drop the chart state of every dataset
    void release_all_datasets();

    // compose slint native chart frames for the active dataset and publish them
    void publish_frames();

    // number of charts of the active dataset
    [[nodiscard]] size_t chart_count() const;

    // all expressions known to the loaded file, from the expression manager
    [[nodiscard]] std::vector<AnyExpression*> all_expressions() const;

    // current abscissa value range [min, max] from the loaded step information
    [[nodiscard]] std::pair<double, double> abscissa_range() const;

    // expressions currently plotted on the chart at the given index
    [[nodiscard]] std::vector<AnyExpression*> chart_selected_expressions(size_t chart_index) const;

    // steps currently selected on the chart at the given index
    [[nodiscard]] std::set<size_t> chart_selected_steps(size_t chart_index) const;

    // apply the given step selection to the chart at the given index and refresh
    void set_chart_selected_steps(size_t chart_index, const std::set<size_t>& steps);

    // step information of the loaded file, or nullptr when not loaded
    [[nodiscard]] const StepInformation* step_information() const;

    // evaluate a custom expression against the expression manager, or nullptr when invalid
    AnyExpression* evaluate_expression(const std::string& expression);

    // plot the given expressions on the chart at the given index and refresh
    void plot_chart_expressions(size_t chart_index, const std::set<AnyExpression*>& expressions);

    // translate a relative Y position [0..1] to a chart index, clamped to valid range
    [[nodiscard]] size_t position_to_index(float position) const;

    // position is a float [0..1] relative Y from the context menu
    void zoom_to_fit(float chart_position);

    void autorange(float chart_position);

    void zoom_abscissa_extent(float chart_position);

    void delete_all_plots(float chart_position);

    void delete_chart(float chart_position);

    // interactive drag-zoom lifecycle
    void zoom_drag_started(float x, float y);

    void zoom_drag_moved(float x, float y);

    void zoom_drag_ended();

    void zoom_drag_canceled();

    // hover readout callback and interaction
    using HoverCallback = std::function<void(const std::string&)>;

    void set_hover_callback(HoverCallback callback) { m_hover_callback = std::move(callback); }

    void hover_moved(float x, float y);

    void hover_ended();

    // publish the debounced hover readout text
    void publish_hover();

private:
    // chart state owned by one plot tab; every dataset holds its own chart
    // list so zoom windows, plotted series and step selections survive tab
    // switches, and its own data references for the active rendering pass
    struct DatasetCharts
    {
        ExpressionManager* expression_manager = nullptr;

        StepInformation const* step_information = nullptr;

        AbscissaScale abscissa_scale = AbscissaScale::LINEAR;

        std::vector<std::vector<std::string>> suggested_plots;

        std::vector<std::unique_ptr<ChartEngine>> charts;
    };

    // active dataset state, or nullptr when no dataset is active
    [[nodiscard]] DatasetCharts* active_dataset();

    [[nodiscard]] const DatasetCharts* active_dataset() const;

    // panel-relative plot rect of the chart at the given index, from the last
    // published frames; invalid rect when the index is out of range
    [[nodiscard]] std::tuple<float, float, float, float> plot_rect(size_t chart_index) const;

    // publish sink for slint native chart frames
    PublishFramesFunction m_publish;

    // chart layout engine building frame snapshots
    ChartLayout m_layout;

    // logical viewport of the charts panel
    float m_viewport_width = 0;

    float m_viewport_height = 0;

    static constexpr size_t k_decimate_target = 4000;

    // chart state per plot tab, keyed by the dataset id assigned by the presenter
    std::unordered_map<int, DatasetCharts> m_datasets;

    // dataset id of the active tab, or -1 when no dataset is active
    int m_active_dataset_id = -1;

    // panel-relative plot rects of the last published frames, per chart index
    std::vector<std::tuple<float, float, float, float>> m_plot_rects;

    size_t m_selected_chart_index = 0;

    std::tuple<float, float, float, float> m_zoom_selection = {-1.0f, -1.0f, -1.0f, -1.0f};

    // hover readout state
    slint::Timer m_hover_timer;

    double m_hover_abscissa_value = 0.0;

    size_t m_hover_chart_index = 0;

    bool m_hover_in_plot = false;

    std::string m_last_hover_text;

    HoverCallback m_hover_callback;

    bool m_dark_mode = false;
};

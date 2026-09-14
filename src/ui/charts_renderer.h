#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <core/SkRefCnt.h>
#include <imgui.h>
#include <slint.h>

#include "../charts/chart.h"

class SkSurface;
class ImGuiSkiaRenderer;
class ExpressionManager;
class StepInformation;
class GrDirectContext;

// scope guard that activates a ChartsRenderer's isolated ImGui/ImPlot
// contexts for the duration of a scope
class ChartsRenderer;

class ChartsContextScope
{
public:
    explicit ChartsContextScope(const ChartsRenderer& charts_renderer);

    ~ChartsContextScope();

    ChartsContextScope(const ChartsContextScope&) = delete;

    ChartsContextScope& operator=(const ChartsContextScope&) = delete;

private:
    void* m_imgui_context = nullptr;

    void* m_implot_context = nullptr;
};

// renders the charts panel offscreen through an isolated Dear ImGui/ImPlot
// stack into a skia raster surface and publishes every finished frame as a
// slint image; one implementation for every platform
class ChartsRenderer
{
public:
    // sink for rendered frames; invoked from the render timer with a fresh image
    using PublishFunction = std::function<void(slint::Image)>;

    // construct with the publish sink for rendered frames
    explicit ChartsRenderer(PublishFunction publish);

    // tear down the backend and release the isolated contexts
    ~ChartsRenderer();

    ChartsRenderer(const ChartsRenderer&) = delete;

    ChartsRenderer& operator=(const ChartsRenderer&) = delete;

    // logical panel size plus device scale; the surface follows physical pixels
    void set_viewport(float width, float height, double scale);

    // adapt the color palette to light/dark theme changes
    void set_dark_mode(bool dark_mode);

    // clear the viewport so no frames are published until a valid size arrives
    void reset_viewport();

    // data path, wired on dataset activation; datasets are identified by the
    // owning plot tab id so each tab keeps its own charts and zoom state and
    // switching tabs restores the previous state instead of rebuilding
    void update(int dataset_id, ExpressionManager& expression_manager, const StepInformation& step_information, AbscissaScale abscissa_scale, const std::vector<std::vector<std::string>>& suggested_plots);

    Chart* add_chart();

    // drop the chart state of the dataset with the given tab id
    void release_dataset(int dataset_id);

    // drop the chart state of every dataset
    void release_all_datasets();

    // schedule the given number of frames on the render timer
    void refresh_charts(int frames = 3);

    // number of charts of the active dataset
    [[nodiscard]] size_t chart_count() const;

    // render one pending frame into the offscreen surface and publish it
    void render();

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

private:
    friend class ChartsContextScope;

    // chart state owned by one plot tab; every dataset holds its own chart
    // list so zoom windows, plotted series and step selections survive tab
    // switches, and its own data references for the active rendering pass
    struct DatasetCharts
    {
        ExpressionManager* expression_manager = nullptr;

        StepInformation const* step_information = nullptr;

        AbscissaScale abscissa_scale = AbscissaScale::LINEAR;

        std::vector<std::vector<std::string>> suggested_plots;

        std::vector<std::unique_ptr<Chart>> charts;
    };

    // active dataset state, or nullptr when no dataset is active
    [[nodiscard]] DatasetCharts* active_dataset();

    [[nodiscard]] const DatasetCharts* active_dataset() const;

    // one-time backend bring-up: isolated contexts, palette style, scaled fonts
    void initialize_backend();

    // shut the backend down and release the isolated contexts
    void terminate_backend();

    // allocate the raster surface whenever the physical pixel size changed
    void ensure_surface(int width, int height);

    // compose the charts panel window content inside an active imgui frame
    void render_panel();

    void update_delta_time();

    void on_idle();

    void publish_hover();

    sk_sp<GrDirectContext> create_gpu_context();

    // publish sink for rendered frames
    PublishFunction m_publish;

    void* m_imgui_context = nullptr;

    void* m_implot_context = nullptr;

    // skia replay backend for the attached imgui context
    std::unique_ptr<ImGuiSkiaRenderer> m_skia_renderer;

    // offscreen target surface in physical pixels
    sk_sp<SkSurface> m_surface;

    // platform-native gpu backend context, or nullptr for cpu raster
    sk_sp<GrDirectContext> m_direct_context;

    // surface dimensions the current allocation was built for
    int m_surface_width = 0;

    int m_surface_height = 0;

    // logical viewport and device scale of the charts panel
    float m_viewport_width = 0;

    float m_viewport_height = 0;

    double m_scale = 1.0;

    // device scale the font atlas was baked for
    double m_font_scale = 0.0;

    // whether contexts, fonts and backend are up
    bool m_initialized = false;

    std::chrono::steady_clock::time_point m_last_frame_time;

    slint::Timer m_render_timer;

    int m_render_chart_frames = 0;

    static constexpr size_t k_decimate_target = 4000;

    // chart state per plot tab, keyed by the dataset id assigned by the presenter
    std::unordered_map<int, DatasetCharts> m_datasets;

    // dataset id of the active tab, or -1 when no dataset is active
    int m_active_dataset_id = -1;

    size_t m_selected_chart_index = 0;

    std::tuple<float, float, float, float> m_zoom_selection = {-1.0f, -1.0f, -1.0f, -1.0f};

    // placeholder series shown while no simulation data is loaded
    std::vector<float> m_demo_series;

    // hover readout state
    slint::Timer m_hover_timer;

    double m_hover_abscissa_value = 0.0;

    size_t m_hover_chart_index = 0;

    bool m_hover_in_plot = false;

    std::string m_last_hover_text;

    HoverCallback m_hover_callback;

    ImVec4 m_background_color;

    bool m_dark_mode = false;
};

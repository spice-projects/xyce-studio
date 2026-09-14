#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <set>

#include <imgui.h>
#include <implot.h>
#include <spdlog/spdlog.h>

#include <core/SkCanvas.h>
#include <core/SkImageInfo.h>
#include <core/SkSurface.h>
#include <gpu/ganesh/GrDirectContext.h>
#include <gpu/ganesh/SkSurfaceGanesh.h>

#include "chart_text_measurer.h"
#include "charts_renderer.h"
#include "font_data.h"
#include "imgui_impl_skia.h"

namespace
{
    // imgui/implot palette matching the slint cupertino widgets, ported from
    // the retired metal overlay so both render paths look identical
    void apply_slint_style(bool is_dark) {
        // macOS Cupertino Theme Palette
        const ImVec4 text = is_dark ? ImVec4(0.92f, 0.92f, 0.94f, 1.00f) : ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
        const ImVec4 muted = is_dark ? ImVec4(0.60f, 0.60f, 0.64f, 1.00f) : ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
        const ImVec4 background = is_dark ? ImVec4(0.12f, 0.12f, 0.12f, 1.00f) : ImVec4(0.93f, 0.93f, 0.93f, 1.00f);
        const ImVec4 panel = is_dark ? ImVec4(0.17f, 0.17f, 0.18f, 1.00f) : ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        const ImVec4 border = is_dark ? ImVec4(0.28f, 0.28f, 0.30f, 1.00f) : ImVec4(0.82f, 0.82f, 0.84f, 1.00f);
        const ImVec4 grid = is_dark ? ImVec4(0.22f, 0.22f, 0.24f, 1.00f) : ImVec4(0.91f, 0.91f, 0.93f, 1.00f);
        const ImVec4 transparent = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        const ImVec4 accent = is_dark ? ImVec4(0.04f, 0.52f, 1.00f, 1.00f) : ImVec4(0.00f, 0.48f, 1.00f, 1.00f);
        // imgui style
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(0.0f, 0.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.WindowRounding = 8.0f;
        style.ChildRounding = 8.0f;
        style.FrameRounding = 6.0f;
        style.PopupRounding = 6.0f;
        // imgui colors
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = text;
        colors[ImGuiCol_TextDisabled] = muted;
        colors[ImGuiCol_WindowBg] = background;
        colors[ImGuiCol_ChildBg] = transparent;
        colors[ImGuiCol_PopupBg] = panel;
        colors[ImGuiCol_Border] = border;
        colors[ImGuiCol_BorderShadow] = transparent;
        colors[ImGuiCol_FrameBg] = panel;
        colors[ImGuiCol_FrameBgHovered] = is_dark ? ImVec4(0.25f, 0.25f, 0.28f, 1.00f) : ImVec4(0.85f, 0.90f, 0.98f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = is_dark ? ImVec4(0.30f, 0.30f, 0.35f, 1.00f) : ImVec4(0.75f, 0.82f, 0.95f, 1.00f);
        colors[ImGuiCol_TitleBg] = panel;
        colors[ImGuiCol_TitleBgActive] = panel;
        colors[ImGuiCol_Button] = is_dark ? ImVec4(0.22f, 0.22f, 0.24f, 1.00f) : ImVec4(0.88f, 0.90f, 0.94f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = is_dark ? ImVec4(0.28f, 0.28f, 0.32f, 1.00f) : ImVec4(0.78f, 0.84f, 0.95f, 1.00f);
        colors[ImGuiCol_ButtonActive] = accent;
        colors[ImGuiCol_Header] = is_dark ? ImVec4(0.22f, 0.22f, 0.26f, 1.00f) : ImVec4(0.85f, 0.90f, 0.98f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = is_dark ? ImVec4(0.28f, 0.28f, 0.32f, 1.00f) : ImVec4(0.75f, 0.82f, 0.95f, 1.00f);
        colors[ImGuiCol_HeaderActive] = accent;
        colors[ImGuiCol_PlotLines] = accent;
        colors[ImGuiCol_PlotHistogram] = accent;
        colors[ImGuiCol_TextSelectedBg] = is_dark ? ImVec4(0.04f, 0.52f, 1.00f, 0.40f) : ImVec4(0.75f, 0.82f, 0.95f, 1.00f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
        // implot style
        ImPlotStyle& plot_style = ImPlot::GetStyle();
        plot_style.PlotBorderSize = 1.0f;
        plot_style.MinorAlpha = 0.35f;
        plot_style.MajorGridSize = ImVec2(1.0f, 1.0f);
        plot_style.MinorGridSize = ImVec2(0.5f, 0.5f);
        plot_style.MajorTickLen = ImVec2(4.0f, 4.0f);
        plot_style.MinorTickLen = ImVec2(2.0f, 2.0f);
        plot_style.MajorTickSize = ImVec2(1.0f, 1.0f);
        plot_style.MinorTickSize = ImVec2(1.0f, 1.0f);
        plot_style.PlotPadding = ImVec2(10.0f, 10.0f);
        plot_style.LabelPadding = ImVec2(6.0f, 6.0f);
        plot_style.LegendPadding = ImVec2(8.0f, 6.0f);
        plot_style.LegendInnerPadding = ImVec2(8.0f, 4.0f);
        plot_style.LegendSpacing = ImVec2(12.0f, 4.0f);
        // implot colors
        ImVec4* plot_colors = plot_style.Colors;
        plot_colors[ImPlotCol_FrameBg] = transparent;
        plot_colors[ImPlotCol_PlotBg] = panel;
        plot_colors[ImPlotCol_PlotBorder] = border;
        plot_colors[ImPlotCol_LegendBg] = transparent;
        plot_colors[ImPlotCol_LegendBorder] = transparent;
        plot_colors[ImPlotCol_LegendText] = text;
        plot_colors[ImPlotCol_TitleText] = text;
        plot_colors[ImPlotCol_InlayText] = muted;
        plot_colors[ImPlotCol_AxisText] = text;
        plot_colors[ImPlotCol_AxisGrid] = grid;
        plot_colors[ImPlotCol_AxisTick] = border;
        plot_colors[ImPlotCol_AxisBg] = transparent;
        plot_colors[ImPlotCol_AxisBgHovered] = is_dark ? ImVec4(0.04f, 0.52f, 1.00f, 0.12f) : ImVec4(0.00f, 0.48f, 1.00f, 0.08f);
        plot_colors[ImPlotCol_AxisBgActive] = is_dark ? ImVec4(0.04f, 0.52f, 1.00f, 0.25f) : ImVec4(0.00f, 0.48f, 1.00f, 0.16f);
        plot_colors[ImPlotCol_Crosshairs] = is_dark ? ImVec4(0.60f, 0.60f, 0.64f, 0.80f) : ImVec4(0.55f, 0.55f, 0.58f, 0.80f);
        plot_colors[ImPlotCol_Selection] = is_dark ? ImVec4(0.04f, 0.52f, 1.00f, 0.35f) : ImVec4(0.00f, 0.48f, 1.00f, 0.25f);
        // add slint colormap if not already registered; the shared engine palette
        // is converted to implot color values on first use
        if (ImPlot::GetColormapIndex("SlintCupertino") == -1) {
            // converted colormap entries in implot format
            static const std::vector<ImVec4> colormap = [] {
                // converted palette
                std::vector<ImVec4> converted;
                converted.reserve(SERIES_COLOR_PALETTE.size());
                // convert each engine color
                for (const auto& color : SERIES_COLOR_PALETTE)
                    converted.push_back(ImVec4(color.r, color.g, color.b, color.a));
                return converted;
            }();
            ImPlot::AddColormap("SlintCupertino", colormap.data(), static_cast<int>(colormap.size()), false);
        }
    }
} // namespace

ChartsContextScope::ChartsContextScope(const ChartsRenderer& charts_renderer) {
    // preserve the active contexts while this renderer owns backend calls
    m_imgui_context = ImGui::GetCurrentContext();
    m_implot_context = ImPlot::GetCurrentContext();
    // activate this renderer's isolated contexts
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(charts_renderer.m_imgui_context));
    ImPlot::SetCurrentContext(static_cast<ImPlotContext*>(charts_renderer.m_implot_context));
}

ChartsContextScope::~ChartsContextScope() {
    // restore the contexts active before this scope
    ImPlot::SetCurrentContext(static_cast<ImPlotContext*>(m_implot_context));
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_context));
}

ChartsRenderer::ChartsRenderer(PublishFunction publish) :
    m_publish(std::move(publish)), m_layout(make_chart_text_measurer()) {
    // start the render timer immediately; idle ticks without pending frames are cheap
    m_render_timer.start(slint::TimerMode::Repeated, std::chrono::milliseconds(16), [this]() { on_idle(); });
}

ChartsRenderer::~ChartsRenderer() {
    // stop scheduling frames before anything else is released
    m_render_timer.stop();
    // tear down the backend and release the isolated contexts
    terminate_backend();
}

void ChartsRenderer::initialize_backend() {
    // contexts are created exactly once per backend generation
    if (m_initialized)
        return;
    // preserve the context that was active before this renderer initialized
    auto* previous_imgui_context = ImGui::GetCurrentContext();
    auto* previous_implot_context = ImPlot::GetCurrentContext();
    // create and retain the imgui context
    IMGUI_CHECKVERSION();
    m_imgui_context = ImGui::CreateContext();
    // create and retain the matching implot context
    m_implot_context = ImPlot::CreateContext();
    // restore the prior active contexts
    ImPlot::SetCurrentContext(previous_implot_context);
    ImGui::SetCurrentContext(previous_imgui_context);
    // activate this renderer's isolated contexts for backend initialization
    ChartsContextScope context_scope(*this);
    // palette shared with the slint cupertino theme
    apply_slint_style(m_dark_mode);
    // imgui configuration
    ImGuiIO& io = ImGui::GetIO();
    // disable ini file noise from the headless stack
    io.IniFilename = nullptr;
    // font base size before the device scale applies
    constexpr float base_font_size = 14.0f;
    // keep the embedded font data owned by the generated array
    ImFontConfig font_cfg{};
    font_cfg.FontDataOwnedByAtlas = false;
    // add the embedded font scaled for the current display scale
    io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(static_cast<const void*>(Inter_Regular_ttf)), static_cast<int>(Inter_Regular_ttf_len), base_font_size * static_cast<float>(m_scale), &font_cfg);
    // undo the atlas scaling so logical coordinates stay unchanged
    io.FontGlobalScale = 1.0f / static_cast<float>(m_scale);
    // anti-aliased lines through the atlas texture like the overlay used
    ImGuiStyle& style = ImGui::GetStyle();
    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;
    // panel clear color matching the slint alternate background
    m_background_color = m_dark_mode ? ImVec4(0.12f, 0.12f, 0.12f, 1.00f) : ImVec4(0.93f, 0.93f, 0.93f, 1.00f);
    // create the platform-native gpu context; may be null, in which case the cpu raster path is used
    m_direct_context = create_gpu_context();
    // bring up the skia replay backend for this context
    m_skia_renderer = std::make_unique<ImGuiSkiaRenderer>();
    // remember the scale fonts were baked for
    m_font_scale = m_scale;
    m_initialized = true;
}

void ChartsRenderer::terminate_backend() {
    // nothing to release when the backend never came up
    if (!m_initialized)
        return;
    // preserve the active contexts while releasing this renderer's backend
    auto* previous_imgui_context = ImGui::GetCurrentContext();
    auto* previous_implot_context = ImPlot::GetCurrentContext();
    // retain this renderer's context addresses before releasing them
    auto* imgui_context = static_cast<ImGuiContext*>(m_imgui_context);
    auto* implot_context = static_cast<ImPlotContext*>(m_implot_context);
    // activate this renderer's isolated contexts for renderer teardown
    ImGui::SetCurrentContext(imgui_context);
    ImPlot::SetCurrentContext(implot_context);
    // release the skia replay backend while its context is active
    m_skia_renderer.reset();
    // release the offscreen surface; must happen before the gpu context
    m_surface.reset();
    // check we are using a gpu context
    if (m_direct_context) {
        // abandon the gpu context so no pending resources trigger
        m_direct_context->abandonContext();
        // release the gpu context; must happen after the surface is released
        m_direct_context.reset();
    }
    // destroy the plot context before its imgui dependency
    ImPlot::DestroyContext(implot_context);
    m_implot_context = nullptr;
    // destroy the imgui context
    ImGui::DestroyContext(imgui_context);
    m_imgui_context = nullptr;
    // never restore a context that was released above
    ImPlot::SetCurrentContext(previous_implot_context == implot_context ? nullptr : previous_implot_context);
    ImGui::SetCurrentContext(previous_imgui_context == imgui_context ? nullptr : previous_imgui_context);
    // next render starts a fresh backend generation
    m_initialized = false;
    m_font_scale = 0.0;
}

void ChartsRenderer::ensure_surface(int width, int height) {
    // keep the existing surface when the pixel size did not change
    if (m_surface != nullptr && m_surface_width == width && m_surface_height == height)
        return;
    // create a new surface for the current pixel size, discard the previous one
    const auto info = SkImageInfo::Make(width, height, kN32_SkColorType, kPremul_SkAlphaType);
    // create surface, prefer gpu render target when available, otherwise fall back to cpu raster
    m_surface = m_direct_context ? SkSurfaces::RenderTarget(m_direct_context.get(), skgpu::Budgeted::kNo, info) : SkSurfaces::Raster(info);
    // store the pixel size for future no-op checks
    m_surface_width = width;
    m_surface_height = height;
}

void ChartsRenderer::update_delta_time() {
    // read the monotonic clock once for this renderer's frame
    const auto current_time = std::chrono::steady_clock::now();
    // use a conventional initial duration before a prior frame exists
    if (m_last_frame_time.time_since_epoch().count() == 0)
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    else
        ImGui::GetIO().DeltaTime = std::chrono::duration<float>(current_time - m_last_frame_time).count();
    // retain the timestamp for the next frame
    m_last_frame_time = current_time;
}

void ChartsRenderer::set_viewport(float width, float height, double scale) {
    // reject degenerate viewports; the panel publishes nothing while hidden or collapsed
    if (width <= 0.0f || height <= 0.0f || scale <= 0.0)
        return;
    // ignore no-op updates fired by unrelated relayouts
    if (m_viewport_width == width && m_viewport_height == height && m_scale == scale)
        return;
    // store the geometry driving surface allocation
    m_viewport_width = width;
    m_viewport_height = height;
    m_scale = scale;
    // schedule a frame so the next tick picks up the new geometry
    refresh_charts(1);
}

void ChartsRenderer::set_dark_mode(bool dark_mode) {
    // ignore no-op updates fired by unrelated relayouts
    if (m_dark_mode == dark_mode)
        return;
    // store the new mode
    m_dark_mode = dark_mode;
    // check panel has been initialized
    if (m_initialized) {
        // activate this renderer's isolated contexts for style update
        ChartsContextScope context_scope(*this);
        // apply style
        apply_slint_style(m_dark_mode);
        // update the background color to match the new mode
        m_background_color = m_dark_mode ? ImVec4(0.12f, 0.12f, 0.12f, 1.00f) : ImVec4(0.93f, 0.93f, 0.93f, 1.00f);
    }
    // force refresh in charts
    refresh_charts(1);
}

void ChartsRenderer::reset_viewport() {
    // zero the stored geometry so render() short-circuits on the next idle tick
    m_viewport_width = 0.0f;
    m_viewport_height = 0.0f;
    m_scale = 0.0;
    // cancel any pending frame so a stale surface does not publish after hiding
    m_render_chart_frames = 0;
}

void ChartsRenderer::render_panel() {
    // remove padding around the panel
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    // anchor the panel window at the origin and fill the full viewport
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(m_viewport_width, m_viewport_height), ImGuiCond_Always);
    // panel window covering the whole viewport without decoration
    if (ImGui::Begin("Charts Panel", nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground)) {
        // charts of the active dataset
        auto* dataset = active_dataset();
        // check we have charts to render
        if (dataset != nullptr && !dataset->charts.empty()) {
            // available area
            const ImVec2 total_space = ImGui::GetContentRegionAvail();
            // chart height
            const float height = total_space.y / static_cast<float>(dataset->charts.size());
            // render charts within the native frame
            for (size_t i = 0; i < dataset->charts.size(); ++i) {
                // area name
                auto name = std::format("Chart {}", i);
                // create child with given height, use the whole area in the horizontal
                if (ImGui::BeginChild(name.c_str(), ImVec2(0, height), false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration)) {
                    // render chart
                    dataset->charts[i]->render();
                    // close
                    ImGui::EndChild();
                }
            }
        }
        // close
        ImGui::End();
    }
    // pop style var
    ImGui::PopStyleVar();
}

void ChartsRenderer::render() {
    // nothing to publish without a sink, a viewport or a usable scale
    if (!m_publish || m_viewport_width <= 0.0f || m_viewport_height <= 0.0f || m_scale <= 0.0)
        return;
    // physical pixel size of the offscreen target
    const int width = static_cast<int>(std::lround(m_viewport_width * m_scale));
    const int height = static_cast<int>(std::lround(m_viewport_height * m_scale));
    // guard against rounding down to nothing at tiny sizes
    if (width <= 0 || height <= 0)
        return;
    // bring the backend up on first use and rebuild it when the scale changed the fonts
    if (!m_initialized || m_font_scale != m_scale) {
        // drop the previous generation completely when rebuilding
        if (m_initialized)
            terminate_backend();
        // initialize the backend and create isolated contexts for this renderer
        initialize_backend();
    }
    // allocate the raster target for the current pixel size
    ensure_surface(width, height);
    // degenerate surfaces must not enter imgui; plotting into zero-size viewports breaks later frames
    if (m_surface == nullptr)
        return;
    // activate this renderer's isolated contexts for the complete frame
    ChartsContextScope context_scope(*this);
    // update the per-renderer frame duration
    update_delta_time();
    // imgui io configuration in logical coordinates with a framebuffer scale
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(m_viewport_width, m_viewport_height);
    io.DisplayFramebufferScale = ImVec2(static_cast<float>(m_scale), static_cast<float>(m_scale));
    // notify the skia backend that a new frame begins
    m_skia_renderer->new_frame();
    // clear the offscreen canvas to the panel background color before imgui writes its draw commands
    m_surface->getCanvas()->clear(SkColorSetARGB(static_cast<int>(m_background_color.w * 255), static_cast<int>(m_background_color.x * 255), static_cast<int>(m_background_color.y * 255), static_cast<int>(m_background_color.z * 255)));
    // start the imgui frame
    ImGui::NewFrame();
    // compose the panel content
    render_panel();
    // finish the imgui frame
    ImGui::Render();
    // replay the draw data onto the offscreen canvas at device scale
    m_skia_renderer->render_draw_data(ImGui::GetDrawData(), m_surface->getCanvas(), static_cast<float>(m_scale));
    // flush gpu work so pixels are available for cpu readback
    if (m_direct_context)
        m_direct_context->flushAndSubmit(m_surface.get());
    // fresh buffer every frame because slint may still hold the previous one
    slint::SharedPixelBuffer<slint::Rgba8Pixel> buffer(width, height);
    // explicit rgba8888 readback keeps the bytes identical to slint's pixel layout
    const auto readback_info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    // bail out without publishing when the readback fails
    if (!m_surface->readPixels(readback_info, buffer.begin(), width * static_cast<int>(sizeof(slint::Rgba8Pixel)), 0, 0))
        return;
    // hand the finished frame to the slint layer
    m_publish(slint::Image(buffer));
    // publish slint native chart frames when the native view is active
    if (m_native_view)
        publish_native_frames();
}

void ChartsRenderer::on_idle() {
    // check flag is set
    if (m_render_chart_frames > 0) {
        // decrease counter
        m_render_chart_frames--;
        // render charts panel
        render();
    }
}

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
    // refresh with a single frame; the charts are already rendered and cached
    // from the previous activation, so one repaint is enough to show the
    // switched-to tab without the cost of a full 3-frame refresh
    refresh_charts(1);
}

Chart* ChartsRenderer::add_chart() {
    // active dataset state
    auto* dataset = active_dataset();
    // no charts can be added without an active dataset
    if (dataset == nullptr)
        return nullptr;
    // create chart and append it to the dataset vector
    dataset->charts.push_back(std::make_unique<Chart>(dataset->expression_manager, dataset->step_information, dataset->abscissa_scale, k_decimate_target));
    // chart
    auto& chart = dataset->charts[dataset->charts.size() - 1];
    // plot series (will do nothing, but will set the correct abscissa for the zoom window)
    chart->plot_series({});
    // exit
    return chart.get();
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
    // refresh
    refresh_charts();
}

void ChartsRenderer::release_all_datasets() {
    // clear active hover readout
    hover_ended();
    // drop every dataset chart state
    m_datasets.clear();
    // no dataset is active anymore
    m_active_dataset_id = -1;
    // refresh
    refresh_charts();
}

void ChartsRenderer::refresh_charts(int frames) { m_render_chart_frames = frames; }

void ChartsRenderer::set_native_view(const bool native_view) {
    // ignore no-op toggles fired by re-wiring
    if (m_native_view == native_view)
        return;
    // store the new view and schedule a frame so the next tick publishes frames
    m_native_view = native_view;
    refresh_charts(1);
}

void ChartsRenderer::set_publish_frames(PublishFramesFunction publish) { m_publish_frames = std::move(publish); }

void ChartsRenderer::publish_native_frames() {
    // active dataset state
    auto* dataset = active_dataset();
    // frames under construction
    std::vector<ChartFrame> frames;
    // build one frame per chart of the active dataset, stacked like the
    // implot panel splits its charts
    if (dataset != nullptr && !dataset->charts.empty()) {
        // per chart canvas size
        const float chart_height = m_viewport_height / static_cast<float>(dataset->charts.size());
        // build the frame snapshot from the chart engine state
        for (const auto& chart : dataset->charts) {
            // log the y axis ranges the native frame will consume (debug aid)
            const auto& axes = chart->engine().axes();
            spdlog::debug("Native frame axes: Y1 [{}, {}], Y2 [{}, {}], Y3 [{}, {}]", axes[0].plot_min_value, axes[0].plot_max_value, axes[1].plot_min_value, axes[1].plot_max_value, axes[2].plot_min_value, axes[2].plot_max_value);
            frames.push_back(m_layout.build(chart->engine(), m_viewport_width, chart_height));
        }
    }
    // hand the frames to the slint layer
    if (m_publish_frames)
        m_publish_frames(frames);
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
    // refresh to show the updated selection
    refresh_charts();
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
    // refresh to show the updated selection
    refresh_charts();
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
    // refresh
    refresh_charts();
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
        // refresh
        refresh_charts();
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
    // refresh
    refresh_charts();
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
        // refresh
        refresh_charts();
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
    // refresh
    refresh_charts();
}

void ChartsRenderer::zoom_drag_started(float x, float y) {
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
        const auto [x_min, y_min, x_max, y_max] = dataset->charts[i]->get_plot_rect();
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

void ChartsRenderer::zoom_drag_moved(float x, float y) {
    // charts of the active dataset
    auto* dataset = active_dataset();
    // check a drag selection was initiated inside a valid chart
    const auto [x1, y1, x2, y2] = m_zoom_selection;
    if (x1 < 0.0f || y1 < 0.0f || dataset == nullptr || m_selected_chart_index >= dataset->charts.size())
        return;
    // current chart plot bounds
    const auto [x_min, y_min, x_max, y_max] = dataset->charts[m_selected_chart_index]->get_plot_rect();
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
        const auto [x_min, y_min, x_max, y_max] = dataset->charts[m_selected_chart_index]->get_plot_rect();
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
    // refresh
    refresh_charts();
    // update hover readout at the release position when zoom was performed
    if (valid_zoom)
        hover_moved(x2, y2);
}

void ChartsRenderer::zoom_drag_canceled() {
    // reset zoom selection
    m_zoom_selection = {-1.0f, -1.0f, -1.0f, -1.0f};
    // refresh
    refresh_charts();
}

void ChartsRenderer::hover_moved(float x, float y) {
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
        const auto [px_min, py_min, px_max, py_max] = dataset->charts[i]->get_plot_rect();
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

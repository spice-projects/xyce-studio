#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "albor/application/timer.h"
#include "albor/render/glyph_outline.h"
#include "albor/render/resource_factory.h"
#include "albor/text/text_engine.h"
#include "albor/widgets/widget.h"
#include "albor/window/window_handle.h"
#include "charts/chart_commands.h"
#include "charts/chart_layout.h"
#include "charts/chart_palette.h"

namespace albor
{
    class BindingEngine;
    class Menu;
    class TextEngine;
} // namespace albor

namespace studio
{
    // one row of the chart context menu
    struct ChartContextMenuEntry
    {
        // the label the row shows
        std::string label;

        // the command the row runs
        ChartCommand command = ChartCommand::ZoomToFit;

        // whether the row separates the groups around it
        bool separator = false;
    };

    // one dataset tab the strip paints
    struct ChartTabSpec
    {
        // the label the tab shows
        std::string title;

        // whether the tab is the active dataset
        bool active = false;

        // whether the tab carries a close affordance
        bool closable = false;
    };

    // the new chart surface, rebuilt one approved layer at a time
    class ChartView final : public albor::Widget
    {
    public:
        // build the view over the window engine, the text engine and the resize timer registry
        ChartView(albor::BindingEngine* engine, albor::TextEngine* text, albor::TimerScheduler* timers = nullptr);

        ~ChartView() override;

        ChartView(const ChartView&) = delete;

        ChartView& operator=(const ChartView&) = delete;

        // replace the palette the painted layers use
        void set_palette(const ChartPalette& palette);

        // replace the accessible summary naming the chart kinds and tabs
        void set_accessible_summary(std::string summary);

        // replace the chart frames the plot rectangles come from
        void set_frames(std::vector<ChartFrame> frames);

        // replace the panel-relative Y where the chart content starts
        void set_content_y(float content_y);

        // replace the dataset tabs the strip paints
        void set_tabs(std::vector<ChartTabSpec> tabs);

        // install the dataset tab callbacks
        void set_on_tab_selected(std::function<void(int)> handler);

        void set_on_tab_closed(std::function<void(int)> handler);

        // install the zoom drag callbacks
        void set_on_zoom_drag_started(std::function<void(float, float)> handler);

        void set_on_zoom_drag_moved(std::function<void(float, float)> handler);

        void set_on_zoom_drag_ended(std::function<void()> handler);

        void set_on_zoom_drag_canceled(std::function<void()> handler);

        // install the hover callbacks
        void set_on_hover_moved(std::function<void(float, float)> handler);

        void set_on_hover_ended(std::function<void()> handler);

        // install the callback the chart context menu commands reach
        void set_on_chart_command(std::function<void(ChartCommand, std::size_t)> handler);

        // run one chart command on the chart at an index
        void run_chart_command(ChartCommand command, std::size_t chart_index) const;

        // replace the transient zoom selection in chart-content coordinates
        void set_zoom_selection(std::optional<albor::Rect> selection);

        // cancel an active zoom drag
        void cancel_drag();

        // install the callback a settled arranged extent reaches
        void set_on_resize_settled(std::function<void(float, float)> handler);

        // install the callback the destructor runs before the widget leaves the tree
        void set_on_destroyed(std::function<void()> handler);

        // hand out the layout size the view currently fills
        [[nodiscard]] float viewport_width() const;

        [[nodiscard]] float viewport_height() const;

        // hand out how many base layers have been recorded, which the cache tests inspect
        [[nodiscard]] std::size_t base_record_count() const;

        // hand out how many series layers have been recorded, which the cache tests inspect
        [[nodiscard]] std::size_t series_record_count() const;

        // hand out how many legend layers have been recorded, which the cache tests inspect
        [[nodiscard]] std::size_t legend_record_count() const;

        // hand out how many strip pictures have been recorded, which the cache tests inspect
        [[nodiscard]] std::size_t strip_record_count() const;

        // override: fill the offered bounds so the host page gives the chart the whole area
        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override;

        // override: paint the chart background and the plot rectangles
        void paint_content(albor::RenderContext& context) override;

        // override: notice a changed extent and report it to the presenter
        void arrange(const albor::Rect& bounds) override;

        // override: select or close a dataset tab
        void dispatch_event(albor::Event& event) override;

        // override: announce the panel as a drawing surface
        [[nodiscard]] albor::SemanticNode semantic_node() const override;

    private:
        // one chart's retained base picture and the key it was recorded for
        struct BaseLayer
        {
            // the state key the picture was recorded for
            std::uint64_t key = 0;

            // the chart-local base picture
            std::shared_ptr<const albor::PictureResource> picture;
        };

        // one chart's retained series picture and the key it was recorded for
        struct SeriesLayer
        {
            // the state key the picture was recorded for
            std::uint64_t key = 0;

            // the chart-local series picture, clipped to the plot
            std::shared_ptr<const albor::PictureResource> picture;
        };

        // one chart's retained legend picture and the key it was recorded for
        struct LegendLayer
        {
            // the state key the picture was recorded for
            std::uint64_t key = 0;

            // the chart-local legend picture, recorded at its local origin
            std::shared_ptr<const albor::PictureResource> picture;
        };

        // the retained dataset tab strip
        struct StripLayer
        {
            // the state key the picture was recorded for
            std::uint64_t key = 0;

            // the panel-local strip picture
            std::shared_ptr<const albor::PictureResource> picture;

            // the panel-local tab rectangles
            std::vector<albor::Rect> tab_bounds;

            // the panel-local close rectangles, empty for a non-closable tab
            std::vector<albor::Rect> close_bounds;
        };

        // the chart-local geometry the base picture is recorded with
        struct BaseLayout
        {
            // the view width, which clamps the abscissa labels
            float view_width = 0.0f;

            // the height of the chart slot
            float slot_height = 0.0f;

            // the chart-local plot origin
            float plot_x = 0.0f;

            float plot_y = 0.0f;

            // the shared plot extent
            float plot_width = 0.0f;

            float plot_height = 0.0f;

            // the width of one y-axis column
            float column_width = 0.0f;
        };

        // hand out the shaped run for one label, shaping it only on the first use
        [[nodiscard]] const albor::TextLayout& label_layout(const std::string& text);

        // record the chart-local base layer
        [[nodiscard]] std::shared_ptr<const albor::PictureResource> record_base(const ChartFrame& frame, const BaseLayout& layout);

        // paint the base layer into the recorder
        void paint_base(albor::RenderContext& context, const ChartFrame& frame, const BaseLayout& layout);

        // paint the smith gamma plane into the recorder
        void paint_smith_base(albor::RenderContext& context, const ChartFrame& frame, const BaseLayout& layout);

        // compute the state key of one base layer
        [[nodiscard]] std::uint64_t base_key(const ChartFrame& frame, const BaseLayout& layout) const;

        // compute the state key of one series layer
        [[nodiscard]] std::uint64_t series_key(const ChartFrame& frame, const BaseLayout& layout) const;

        // record the chart-local series layer
        [[nodiscard]] std::shared_ptr<const albor::PictureResource> record_series(const ChartFrame& frame, const BaseLayout& layout);

        // paint the series layer into the recorder
        void paint_series(albor::RenderContext& context, const ChartFrame& frame, const BaseLayout& layout);

        // compute the state key of one legend layer
        [[nodiscard]] std::uint64_t legend_key(const ChartFrame& frame) const;

        // record the chart-local legend layer
        [[nodiscard]] std::shared_ptr<const albor::PictureResource> record_legend(const ChartFrame& frame);

        // paint the legend layer into the recorder at its local origin
        void paint_legend(albor::RenderContext& context, const ChartFrame& frame);

        // compute the state key of the strip
        [[nodiscard]] std::uint64_t strip_key(float width, float height) const;

        // record the panel-local strip picture and its hit rectangles
        [[nodiscard]] std::shared_ptr<const albor::PictureResource> record_strip(float width, float height);

        // paint the strip into the recorder
        void paint_strip(albor::RenderContext& context, float width, float height);

        // compute the tab widths the strip lays out at a given width
        [[nodiscard]] std::vector<float> strip_tab_widths(float width);

        // recompute the tab and close hit rectangles for the current tabs
        void refresh_strip_bounds();

        // arm one settle callback for the extent the panel stands at
        void schedule_resize_settle();

        // the rows the context menu offers for one chart
        [[nodiscard]] std::vector<ChartContextMenuEntry> context_menu_entries(std::size_t chart_index) const;

        // build the menu the popup surface shows over the given engines
        [[nodiscard]] std::unique_ptr<albor::Menu> build_context_menu(albor::BindingEngine& engine, albor::TextEngine& text, std::size_t chart_index);

        // open the context menu at a view local point
        void open_context_menu(albor::Point local_point);

        // hide the open context menu, keeping its tree for the next dismissal
        void close_context_menu();

        // destroy the menu surface a previous open left behind
        void destroy_context_menu();

        // draw one shaped run, expanding an embedded face into glyph outlines
        void draw_text(albor::RenderContext& context, const albor::TextLayout& layout, albor::Point origin, const albor::Paint& paint);

        // the text engine the labels shape through
        albor::TextEngine* m_text;

        // the shaped labels, keyed by their text
        std::unordered_map<std::string, albor::TextLayout> m_labels;

        // the resolver that turns embedded glyphs into outlines
        albor::GlyphOutlineResolver m_outline_resolver;

        // the outline cache, which keeps a label's glyphs from being rebuilt
        albor::GlyphOutlineCache m_outline_cache;

        // the palette the painted layers use
        ChartPalette m_palette = CHART_PALETTE_LIGHT;

        // the accessible summary the semantic node announces
        std::string m_summary;

        // the chart frames the plot rectangles come from
        std::vector<ChartFrame> m_frames;

        // the panel-relative Y where the chart content starts
        float m_content_y = 0.0f;

        // the callback a settled arranged extent reaches
        std::function<void(float, float)> m_on_resize_settled;

        // the timer registry the resize settle arms on, which a test may leave empty
        albor::TimerScheduler* m_timers = nullptr;

        // the one shot settle timer, where zero names none
        albor::TimerHandle m_resize_timer = 0;

        // the factory that stamps the retained base pictures
        albor::ResourceFactory m_resources;

        // the retained base layer of every chart
        std::vector<BaseLayer> m_base_layers;

        // the retained series layer of every chart
        std::vector<SeriesLayer> m_series_layers;

        // the retained legend layer of every chart
        std::vector<LegendLayer> m_legend_layers;

        // the retained dataset strip
        StripLayer m_strip_layer;

        // the dataset tabs the strip paints
        std::vector<ChartTabSpec> m_tabs;

        // how many base pictures have been recorded, which the cache tests inspect
        std::size_t m_base_record_count = 0;

        // how many series pictures have been recorded, which the cache tests inspect
        std::size_t m_series_record_count = 0;

        // how many legend pictures have been recorded, which the cache tests inspect
        std::size_t m_legend_record_count = 0;

        // how many strip pictures have been recorded, which the cache tests inspect
        std::size_t m_strip_record_count = 0;

        // the dataset tab callbacks
        std::function<void(int)> m_on_tab_selected;

        std::function<void(int)> m_on_tab_closed;

        // the zoom drag callbacks
        std::function<void(float, float)> m_on_zoom_drag_started;

        std::function<void(float, float)> m_on_zoom_drag_moved;

        std::function<void()> m_on_zoom_drag_ended;

        std::function<void()> m_on_zoom_drag_canceled;

        // the hover callbacks
        std::function<void(float, float)> m_on_hover_moved;

        std::function<void()> m_on_hover_ended;

        // the text engine the context menu measures its rows with
        albor::TextEngine* m_menu_text = nullptr;

        // the menu surface the last context menu opened
        albor::WindowHandle m_menu_popup;

        // the command sink the context menu rows report to
        std::function<void(ChartCommand, std::size_t)> m_on_chart_command;

        // the transient zoom selection in chart-content coordinates
        std::optional<albor::Rect> m_zoom_selection;

        // whether the pointer owns a zoom drag
        bool m_dragging = false;

        // the destructor callback, which drops the presenter's borrowed pointer
        std::function<void()> m_on_destroyed;

        // the arranged extent the next arrange compares against
        float m_last_width = 0.0f;

        float m_last_height = 0.0f;
    };
} // namespace studio

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "albor/application/timer.h"
#include "albor/widgets/widget.h"
#include "charts/chart_scene.h"

namespace studio
{
    // the native albor surface that paints the retained chart scene and reports chart input
    class ChartsView final : public albor::Widget
    {
    public:
        // build the view over the window engines and the ui thread timer registry
        ChartsView(albor::BindingEngine* engine, albor::TimerScheduler* timers);

        ~ChartsView() override;

        ChartsView(const ChartsView&) = delete;

        ChartsView& operator=(const ChartsView&) = delete;

        // replace the base scene, invalidating the whole panel
        void set_scene(std::shared_ptr<const ChartScene> scene);

        // replace the transient overlay, invalidating only the union of its old and new bounds
        void set_overlay(std::shared_ptr<const ChartOverlay> overlay);

        // replace the accessible summary, which names the chart kinds for automation and assistive technology
        void set_accessible_summary(std::string summary);

        // install the zoom drag callbacks
        void set_on_zoom_drag_started(std::function<void(float, float)> handler);

        void set_on_zoom_drag_moved(std::function<void(float, float)> handler);

        void set_on_zoom_drag_ended(std::function<void()> handler);

        void set_on_zoom_drag_canceled(std::function<void()> handler);

        // install the hover callbacks
        void set_on_hover_moved(std::function<void(float, float)> handler);

        void set_on_hover_ended(std::function<void()> handler);

        // install the callback the resize settle asks for a fresh scene
        void set_on_resize_settled(std::function<void(float, float)> handler);

        // install the callback the destructor runs before the widget leaves the tree
        void set_on_destroyed(std::function<void()> handler);

        // cancel an active zoom drag, which a capture loss or an escape asks for
        void cancel_drag();

        // hand out the layout size the view currently fills
        [[nodiscard]] float viewport_width() const;

        [[nodiscard]] float viewport_height() const;

        // override: record the base picture and the overlay
        void paint_content(albor::RenderContext& context) override;

        // override: report pointer transitions to the chart controller
        void dispatch_event(albor::Event& event) override;

        // override: fill the offered bounds so the host page gives the charts the whole area
        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override;

        // override: notice a settled resize and ask for a fresh scene
        void arrange(const albor::Rect& bounds) override;

        // override: announce the panel as a drawing surface with its author id
        [[nodiscard]] albor::SemanticNode semantic_node() const override;

    private:
        // schedule the one shot rebuild after a resize settles
        void schedule_resize_settle();

        // map a pointer position from window space into the view
        [[nodiscard]] albor::Point to_local(const albor::Point& window_position) const;

        // draw one picture under the scene transform
        void draw_scene_picture(albor::RenderContext& context, const std::shared_ptr<const albor::PictureResource>& picture);

        // the timer registry the resize settle is armed on
        albor::TimerScheduler* m_timers;

        // the retained base scene the view replays
        std::shared_ptr<const ChartScene> m_scene;

        // the transient zoom selection overlay
        std::shared_ptr<const ChartOverlay> m_overlay;

        // the accessible summary the semantic node announces
        std::string m_summary;

        // the zoom drag callbacks
        std::function<void(float, float)> m_on_zoom_drag_started;

        std::function<void(float, float)> m_on_zoom_drag_moved;

        std::function<void()> m_on_zoom_drag_ended;

        std::function<void()> m_on_zoom_drag_canceled;

        // the hover callbacks
        std::function<void(float, float)> m_on_hover_moved;

        std::function<void()> m_on_hover_ended;

        // the resize settle callback
        std::function<void(float, float)> m_on_resize_settled;

        // the destructor callback, which drops the presenter's borrowed pointer
        std::function<void()> m_on_destroyed;

        // the handle of the pending resize settle timer
        albor::TimerHandle m_resize_timer = 0;

        // the size the last arrange pass received
        float m_last_width = 0.0f;

        float m_last_height = 0.0f;

        // whether the pointer owns a zoom drag
        bool m_dragging = false;
    };
} // namespace studio

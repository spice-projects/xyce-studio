#include "views/charts/charts_view.h"

#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

#include "albor/core/constraints.h"

studio::ChartsView::ChartsView(albor::BindingEngine* engine, albor::TimerScheduler* timers) :
    albor::Widget(engine, "charts_view"), m_timers(timers) {}

studio::ChartsView::~ChartsView() {
    // drop the pending resize timer so it cannot reach a destroyed view
    if (m_timers != nullptr && m_timers->scheduled(m_resize_timer))
        (void)m_timers->cancel(m_resize_timer);
    // tell the presenter that borrowed this view that it is gone
    if (m_on_destroyed)
        m_on_destroyed();
}

void studio::ChartsView::set_scene(std::shared_ptr<const ChartScene> scene) {
    // keep the retained picture the view replays
    m_scene = std::move(scene);
    // a base republish drops any selection recorded against the old scene
    m_overlay.reset();
    // the whole panel changed, so the whole panel redraws
    invalidate();
}

void studio::ChartsView::set_overlay(std::shared_ptr<const ChartOverlay> overlay) {
    // the old and new overlays both own pixels that must be redrawn
    std::optional<albor::Rect> damage;
    // start from the overlay the view currently draws
    if (m_overlay != nullptr)
        damage = m_overlay->bounds;
    // join the area the replacement paints
    if (overlay != nullptr)
        damage = damage ? damage->unioned(overlay->bounds) : overlay->bounds;
    // keep the replacement so the next frame draws it
    m_overlay = std::move(overlay);
    // a replaced overlay invalidates only the area either picture touched
    if (damage) {
        // grow the damage so the overlay border and its antialias fringe erase
        const albor::Rect local = damage->inflated(1.0f);
        // map the local damage into window coordinates
        invalidate(albor::Rect::from_origin_size(albor::Point{.x = bounds().x + local.x, .y = bounds().y + local.y}, albor::Size{.width = local.width, .height = local.height}));
    }
}

void studio::ChartsView::set_accessible_summary(std::string summary) { m_summary = std::move(summary); }

void studio::ChartsView::set_on_zoom_drag_started(std::function<void(float, float)> handler) { m_on_zoom_drag_started = std::move(handler); }

void studio::ChartsView::set_on_zoom_drag_moved(std::function<void(float, float)> handler) { m_on_zoom_drag_moved = std::move(handler); }

void studio::ChartsView::set_on_zoom_drag_ended(std::function<void()> handler) { m_on_zoom_drag_ended = std::move(handler); }

void studio::ChartsView::set_on_zoom_drag_canceled(std::function<void()> handler) { m_on_zoom_drag_canceled = std::move(handler); }

void studio::ChartsView::set_on_hover_moved(std::function<void(float, float)> handler) { m_on_hover_moved = std::move(handler); }

void studio::ChartsView::set_on_hover_ended(std::function<void()> handler) { m_on_hover_ended = std::move(handler); }

void studio::ChartsView::set_on_resize_settled(std::function<void(float, float)> handler) { m_on_resize_settled = std::move(handler); }

void studio::ChartsView::set_on_destroyed(std::function<void()> handler) { m_on_destroyed = std::move(handler); }

float studio::ChartsView::viewport_width() const { return bounds().width; }

float studio::ChartsView::viewport_height() const { return bounds().height; }

void studio::ChartsView::paint_content(albor::RenderContext& context) {
    // a view with no area draws nothing
    if (bounds().empty())
        return;
    // the retained base scene is the whole chart panel
    draw_scene_picture(context, m_scene != nullptr ? m_scene->base : nullptr);
    // the transient zoom selection draws above the base
    if (m_overlay != nullptr)
        draw_scene_picture(context, m_overlay->picture);
}

void studio::ChartsView::draw_scene_picture(albor::RenderContext& context, const std::shared_ptr<const albor::PictureResource>& picture) {
    // a picture with no command draws nothing
    if (picture == nullptr)
        return;
    // the scene picture is recorded in panel local coordinates
    albor::Transform transform = albor::Transform::translation(bounds().x, bounds().y);
    // a stale scene is stretched until the resize settle rebuilds it
    if (m_scene != nullptr && m_scene->width > 0.0f && m_scene->height > 0.0f && (std::abs(m_scene->width - bounds().width) > 0.01f || std::abs(m_scene->height - bounds().height) > 0.01f)) {
        // scale the old picture into the current extent
        transform = albor::Transform::scaling(bounds().width / m_scene->width, bounds().height / m_scene->height).then(transform);
    }
    // replay the retained command list under the panel transform
    context.draw_picture(picture, transform);
}

void studio::ChartsView::cancel_drag() {
    // a view without a live drag has nothing to cancel
    if (!m_dragging)
        return;
    // the drag no longer owns the pointer
    m_dragging = false;
    // drop the selection without changing the charts
    if (m_on_zoom_drag_canceled)
        m_on_zoom_drag_canceled();
}

void studio::ChartsView::dispatch_event(albor::Event& event) {
    // a hidden view takes no input
    if (!visible())
        return;
    // an escape cancels the drag the input layer did not dispatch
    if (event.kind == albor::EventKind::KeyDown && event.key == albor::Key::Escape) {
        cancel_drag();
        event.handled = true;
        return;
    }
    // run the pointer transitions first, which the plate state reads
    handle_hover_transitions(event);
    // map the window position into the view local coordinate space
    const albor::Point local = to_local(event.position);
    // a press inside the panel starts a zoom drag
    if (event.kind == albor::EventKind::PointerDown) {
        // a press outside the view belongs to whatever is behind it
        if (!hit_test(event.position))
            return;
        // the drag owns the pointer until it ends or is canceled
        m_dragging = true;
        // the view consumed the press
        event.handled = true;
        // report the drag anchor to the controller
        if (m_on_zoom_drag_started)
            m_on_zoom_drag_started(local.x, local.y);
        // the press is settled
        return;
    }
    // a move either extends the drag or updates the hover readout
    if (event.kind == albor::EventKind::PointerMove) {
        // a drag consumes the move
        if (m_dragging) {
            // the view consumed the move
            event.handled = true;
            // report the moving corner to the controller
            if (m_on_zoom_drag_moved)
                m_on_zoom_drag_moved(local.x, local.y);
            // the move is settled
            return;
        }
        // report the hover position to the controller
        if (m_on_hover_moved)
            m_on_hover_moved(local.x, local.y);
        // the move is settled
        return;
    }
    // a release ends the drag, or refreshes the hover readout when no drag is active
    if (event.kind == albor::EventKind::PointerUp) {
        // a drag consumes the release
        if (m_dragging) {
            // the drag no longer owns the pointer
            m_dragging = false;
            // the view consumed the release
            event.handled = true;
            // apply the completed zoom window
            if (m_on_zoom_drag_ended)
                m_on_zoom_drag_ended();
            // the release is settled
            return;
        }
        // report the final hover position to the controller
        if (m_on_hover_moved)
            m_on_hover_moved(local.x, local.y);
        // the release is settled
        return;
    }
    // a leave cancels a drag or clears the hover readout
    if (event.kind == albor::EventKind::PointerLeave) {
        // a drag is canceled when the pointer leaves the view
        if (m_dragging) {
            // the drag no longer owns the pointer
            m_dragging = false;
            // the view consumed the leave
            event.handled = true;
            // drop the selection without changing the charts
            if (m_on_zoom_drag_canceled)
                m_on_zoom_drag_canceled();
            // the leave is settled
            return;
        }
        // clear the readout the pointer left
        if (m_on_hover_ended)
            m_on_hover_ended();
        // the leave is settled
        return;
    }
    // an enter refreshes the hover readout
    if (event.kind == albor::EventKind::PointerEnter) {
        // report the hover position to the controller
        if (m_on_hover_moved)
            m_on_hover_moved(local.x, local.y);
    }
}

void studio::ChartsView::measure(const albor::SizeConstraints& constraints, albor::Size& out_size) {
    // measure the children of the widget, which the panel has none of
    albor::Size children{};
    // let the base widget measure its children
    albor::UiNode::measure(constraints, children);
    // the chart panel asks for a useful default when the host leaves it unbounded
    albor::Size content{.width = 640.0f, .height = 480.0f};
    // a bounded width is what the panel fills
    if (std::isfinite(constraints.maximum.width))
        content.width = constraints.maximum.width;
    // a bounded height is what the panel fills
    if (std::isfinite(constraints.maximum.height))
        content.height = constraints.maximum.height;
    // clamp the fill into the offered constraints
    m_measured_size = constraints.constrain(content);
    // the preferred size follows the measured one
    m_preferred_size = m_measured_size;
    // hand the parent the extent
    out_size = m_measured_size;
}

void studio::ChartsView::arrange(const albor::Rect& bounds) {
    // remember whether the panel actually changed extent
    const bool changed = std::abs(bounds.width - m_last_width) > 0.01f || std::abs(bounds.height - m_last_height) > 0.01f;
    // let the base component arrange the bounds
    albor::Component::arrange(bounds);
    // remember the extent the next arrange compares against
    m_last_width = bounds.width;
    // remember the height the next arrange compares against
    m_last_height = bounds.height;
    // a changed extent needs a fresh scene, but only after the drag settles
    if (changed)
        schedule_resize_settle();
}

albor::SemanticNode studio::ChartsView::semantic_node() const {
    // the chart panel is a drawing surface the user interacts with
    albor::SemanticNode node{albor::SemanticRole::Canvas};
    // carry the author id so the automation can select the panel
    node.set_explicit_id(identity().explicit_id);
    // prefer the summary that names the chart kinds, falling back to the author id
    node.set_name(!m_summary.empty() ? m_summary : (identity().explicit_id.empty() ? "charts" : identity().explicit_id));
    // place the announcement where the panel was arranged
    node.set_bounds(bounds());
    // hand the node to the adapter
    return node;
}

void studio::ChartsView::schedule_resize_settle() {
    // no timer registry leaves the caller to rebuild on its own
    if (m_timers == nullptr)
        return;
    // cancel the settle a previous resize armed
    if (m_timers->scheduled(m_resize_timer))
        (void)m_timers->cancel(m_resize_timer);
    // arm one rebuild after the resize has been quiet for a moment
    m_resize_timer = m_timers->schedule_once(
        [this] {
            // the settle consumed the timer
            m_resize_timer = 0;
            // ask the controller for a fresh scene at the settled size
            if (m_on_resize_settled)
                m_on_resize_settled(bounds().width, bounds().height);
        },
        std::chrono::milliseconds(50));
}

albor::Point studio::ChartsView::to_local(const albor::Point& window_position) const {
    // the picture origin is the top left of the arranged panel
    return albor::Point{.x = window_position.x - bounds().x, .y = window_position.y - bounds().y};
}

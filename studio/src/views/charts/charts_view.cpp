#include "views/charts/charts_view.h"

#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

#include "albor/core/constraints.h"
#include "albor/render/draw_command.h"
#include "albor/widgets/menu.h"
#include "albor/widgets/menu_item.h"
#include "albor/window/popup_host.h"

studio::ChartsView::ChartsView(albor::BindingEngine* engine, albor::TimerScheduler* timers) :
    albor::Widget(engine, "charts_view"), m_timers(timers) {
    // a secondary press asks the panel for its chart context menu
    set_on_context_menu([this](const albor::Point point) { open_context_menu(point); });
}

studio::ChartsView::~ChartsView() {
    // the menu surface dies with the panel that anchors it
    destroy_context_menu();
    // drop the pending resize timer so it cannot reach a destroyed view
    if (m_timers != nullptr && m_timers->scheduled(m_resize_timer))
        (void)m_timers->cancel(m_resize_timer);
    // tell the presenter that borrowed this view that it is gone
    if (m_on_destroyed)
        m_on_destroyed();
}

void studio::ChartsView::set_scene(std::shared_ptr<const ChartScene> scene) {
    // a base republish ends a drag recorded against the old scene
    cancel_drag();
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
        // a stale scene is stretched into the arranged extent until the resize settles
        const float scale_x = m_scene != nullptr && m_scene->width > 0.0f && bounds().width > 0.0f ? bounds().width / m_scene->width : 1.0f;
        // the vertical scale matches the picture transform
        const float scale_y = m_scene != nullptr && m_scene->height > 0.0f && bounds().height > 0.0f ? bounds().height / m_scene->height : 1.0f;
        // map the local damage into window coordinates
        invalidate(albor::Rect::from_origin_size(albor::Point{.x = bounds().x + local.x * scale_x, .y = bounds().y + local.y * scale_y}, albor::Size{.width = local.width * scale_x, .height = local.height * scale_y}));
    }
}

void studio::ChartsView::set_accessible_summary(std::string summary) { m_summary = std::move(summary); }

void studio::ChartsView::set_on_zoom_drag_started(std::function<void(float, float)> handler) { m_on_zoom_drag_started = std::move(handler); }

void studio::ChartsView::set_on_zoom_drag_moved(std::function<void(float, float)> handler) { m_on_zoom_drag_moved = std::move(handler); }

void studio::ChartsView::set_on_zoom_drag_ended(std::function<void()> handler) { m_on_zoom_drag_ended = std::move(handler); }

void studio::ChartsView::set_on_zoom_drag_canceled(std::function<void()> handler) { m_on_zoom_drag_canceled = std::move(handler); }

void studio::ChartsView::set_on_hover_moved(std::function<void(float, float)> handler) { m_on_hover_moved = std::move(handler); }

void studio::ChartsView::set_on_hover_ended(std::function<void()> handler) { m_on_hover_ended = std::move(handler); }

void studio::ChartsView::set_on_tab_selected(std::function<void(int)> handler) { m_on_tab_selected = std::move(handler); }

void studio::ChartsView::set_on_tab_closed(std::function<void(int)> handler) { m_on_tab_closed = std::move(handler); }

void studio::ChartsView::set_on_resize_settled(std::function<void(float, float)> handler) { m_on_resize_settled = std::move(handler); }

void studio::ChartsView::set_on_destroyed(std::function<void()> handler) { m_on_destroyed = std::move(handler); }

void studio::ChartsView::set_text_engine(albor::TextEngine* text) { m_text = text; }

void studio::ChartsView::set_on_chart_command(std::function<void(ChartCommand, std::size_t)> handler) { m_on_chart_command = std::move(handler); }

void studio::ChartsView::run_chart_command(const ChartCommand command, const std::size_t chart_index) const {
    // a command reaches the presenter only while the view is wired to it
    if (m_on_chart_command)
        m_on_chart_command(command, chart_index);
}

float studio::ChartsView::viewport_width() const { return bounds().width; }

float studio::ChartsView::viewport_height() const { return bounds().height; }

void studio::ChartsView::paint_content(albor::RenderContext& context) {
    // a view with no area draws nothing
    if (bounds().empty())
        return;
    // the retained base scene is the whole chart panel
    draw_scene_picture(context, m_scene != nullptr ? m_scene->base : nullptr);
    // the strip flattens into the scene, so a partial frame records its commands individually
    draw_strip(context);
    // the transient zoom selection draws above the base
    if (m_overlay != nullptr)
        draw_scene_picture(context, m_overlay->picture);
}

albor::Transform studio::ChartsView::scene_transform() const {
    // the scene picture is recorded in panel local coordinates
    albor::Transform transform = albor::Transform::translation(bounds().x, bounds().y);
    // a stale scene is stretched until the resize settle rebuilds it
    if (m_scene != nullptr && m_scene->width > 0.0f && m_scene->height > 0.0f && (std::abs(m_scene->width - bounds().width) > 0.01f || std::abs(m_scene->height - bounds().height) > 0.01f))
        transform = albor::Transform::scaling(bounds().width / m_scene->width, bounds().height / m_scene->height).then(transform);
    // hand out the panel transform
    return transform;
}

void studio::ChartsView::draw_scene_picture(albor::RenderContext& context, const std::shared_ptr<const albor::PictureResource>& picture) {
    // a picture with no command draws nothing
    if (picture == nullptr)
        return;
    // replay the retained command list under the panel transform
    context.draw_picture(picture, scene_transform());
}

void studio::ChartsView::draw_strip(albor::RenderContext& context) {
    // a scene without a strip picture has no tab to draw
    if (m_scene == nullptr || m_scene->strip == nullptr)
        return;
    // the strip commands are recorded in panel local coordinates
    context.save();
    context.set_transform(scene_transform());
    // flatten every strip command so the scene records it on its own damage bound
    for (const albor::DrawCommand& command : m_scene->strip->commands())
        command.replay(context);
    context.restore();
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
    // map the window position into the logical scene coordinates the picture was recorded in
    const albor::Point local = to_scene(to_local(event.position));
    // the chart content starts under the dataset strip
    const float content_y = m_scene != nullptr ? m_scene->content_y : 0.0f;
    // the chart point is relative to the content, not the strip
    const albor::Point chart_point{.x = local.x, .y = local.y - content_y};
    // a press in the dataset strip selects or closes a tab
    if (event.kind == albor::EventKind::PointerDown && m_scene != nullptr && local.y >= 0.0f && local.y < m_scene->tab_bar_height) {
        // find the tab under the press
        for (const studio::ChartTab& tab : m_scene->tabs) {
            // a press on the close affordance closes the tab
            if (tab.closable && tab.close_bounds.width > 0.0f && local.x >= tab.close_bounds.x && local.x <= tab.close_bounds.x + tab.close_bounds.width && local.y >= tab.close_bounds.y && local.y <= tab.close_bounds.y + tab.close_bounds.height) {
                // the view consumed the close press
                event.handled = true;
                // report the tab to the presenter
                if (m_on_tab_closed)
                    m_on_tab_closed(static_cast<int>(&tab - m_scene->tabs.data()));
                // the press is settled
                return;
            }
            // a press on the tab body selects it
            if (local.x >= tab.bounds.x && local.x <= tab.bounds.x + tab.bounds.width) {
                // the view consumed the selection press
                event.handled = true;
                // report the tab to the presenter
                if (m_on_tab_selected)
                    m_on_tab_selected(static_cast<int>(&tab - m_scene->tabs.data()));
                // the press is settled
                return;
            }
        }
        // the strip itself consumes a press so it cannot start a chart drag
        event.handled = true;
        // the press is settled
        return;
    }
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
            m_on_zoom_drag_started(chart_point.x, chart_point.y);
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
                m_on_zoom_drag_moved(chart_point.x, chart_point.y);
            // the move is settled
            return;
        }
        // a move over the strip clears the chart readout
        if (local.y < content_y) {
            // clear the readout the strip has no chart to explain
            if (m_on_hover_ended)
                m_on_hover_ended();
            // the move is settled
            return;
        }
        // report the hover position to the controller
        if (m_on_hover_moved)
            m_on_hover_moved(chart_point.x, chart_point.y);
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
            m_on_hover_moved(chart_point.x, chart_point.y);
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
        // a pointer on the strip has no chart to explain
        if (local.y >= content_y && m_on_hover_moved)
            m_on_hover_moved(chart_point.x, chart_point.y);
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

std::vector<studio::ChartsView::ContextMenuEntry> studio::ChartsView::context_menu_entries(const std::size_t chart_index) const {
    // a chart index past the recorded slots has no menu
    if (m_scene == nullptr || chart_index >= m_scene->slots.size())
        return {};
    // the smith plane is fixed at the unit circle, so its chart only offers the plot command
    const bool cartesian = m_scene->slots[chart_index].kind != ChartKind::SMITH;
    // the menu under construction
    std::vector<ContextMenuEntry> entries;
    // the zoom commands apply to a cartesian chart only
    if (cartesian) {
        // fit both axes of the targeted chart
        entries.push_back(ContextMenuEntry{.label = "Zoom to Fit", .command = ChartCommand::ZoomToFit});
        // reset only the ordinate of the targeted chart
        entries.push_back(ContextMenuEntry{.label = "Autorange", .command = ChartCommand::Autorange});
        // reset the abscissa extent of every chart
        entries.push_back(ContextMenuEntry{.label = "Zoom Abscissa Extent", .command = ChartCommand::ZoomAbscissaExtent});
        // separate the zoom group from the plot commands
        entries.push_back(ContextMenuEntry{.label = {}, .command = ChartCommand::ZoomToFit, .separator = true});
    }
    // the plot command applies to both chart kinds
    entries.push_back(ContextMenuEntry{.label = "Delete All Plots", .command = ChartCommand::DeleteAllPlots});
    // the chart stack commands apply to a cartesian chart only
    if (cartesian) {
        // separate the plot command from the chart stack commands
        entries.push_back(ContextMenuEntry{.label = {}, .command = ChartCommand::ZoomToFit, .separator = true});
        // insert an empty chart after the targeted one
        entries.push_back(ContextMenuEntry{.label = "Add Chart", .command = ChartCommand::AddChart});
        // remove the targeted chart
        entries.push_back(ContextMenuEntry{.label = "Delete Chart", .command = ChartCommand::DeleteChart});
    }
    // hand the rows to the popup builder
    return entries;
}

std::unique_ptr<albor::Menu> studio::ChartsView::build_context_menu(albor::BindingEngine& engine, albor::TextEngine& text, const std::size_t chart_index) {
    // the menu is the whole popup surface
    auto menu = std::make_unique<albor::Menu>(&engine, &text);
    // every row routes its command to the presenter through this view
    for (const ContextMenuEntry& entry : context_menu_entries(chart_index)) {
        // a separator groups the rows around it
        if (entry.separator) {
            // the rule joins the menu between the rows it separates
            menu->add_separator();
            // next row
            continue;
        }
        // one row presents the label and runs the command
        auto item = std::make_unique<albor::MenuItem>(&engine, &text, entry.label);
        // the press dismisses the menu before the scene it names changes
        item->set_on_invoke([this, command = entry.command, chart_index] {
            // hide the surface the press landed on
            close_context_menu();
            // forward the chosen command to the presenter
            run_chart_command(command, chart_index);
        });
        // the row joins the menu, which lays it out and routes its input
        menu->add_child(std::move(item));
    }
    // the popup reads the panel palette so its rows match the shell
    if (const albor::Theme* installed = theme()) {
        // the menu face takes the panel role the shell paints behind it
        menu->set_background(installed->palette().panel_background[0]);
        // every row takes the theme colors and metrics
        albor::apply_theme(*menu, *installed);
    }
    // hand the built menu to the popup stage
    return menu;
}

void studio::ChartsView::open_context_menu(const albor::Point local_point) {
    // a panel with no engine, chart or text engine has no menu to offer
    if (engine() == nullptr || m_scene == nullptr || m_scene->slots.empty() || m_text == nullptr)
        return;
    // the press in the logical scene coordinates the recorded slots use
    const albor::Point scene_point = to_scene(local_point);
    // the dataset strip keeps its own interactions, so a press there opens no menu
    if (scene_point.y < m_scene->content_y)
        return;
    // the content the chart slots share
    const float content_height = std::max(0.0f, m_scene->height - m_scene->content_y);
    // the chart the press lands on, by the vertical slice it fills
    const std::size_t index = content_height <= 0.0f ? 0 : std::min(static_cast<std::size_t>((scene_point.y - m_scene->content_y) / content_height * static_cast<float>(m_scene->slots.size())), m_scene->slots.size() - 1);
    // the measuring pass measures the rows the popup will build
    auto measuring = build_context_menu(*engine(), *m_text, index);
    // the popup takes the extent the rows measured
    albor::Size size{};
    measuring->measure(albor::SizeConstraints::unconstrained(), size);
    // a popup is opened by the process wide host the application installs
    albor::PopupHost* host = albor::window_popup_host();
    // no installed host leaves the menu without a surface
    if (host == nullptr)
        return;
    // a surface from the previous open is released before the replacement takes its place
    destroy_context_menu();
    // the menu opens at the press, with the panel position the anchor expects
    const albor::Rect anchor = albor::Rect::from_origin_size(albor::Point{.x = bounds().x + local_point.x, .y = bounds().y + local_point.y}, albor::Size{});
    // the popup clears with the panel role so its first frame never flashes
    const albor::Color background = theme() != nullptr ? theme()->palette().panel_background[0] : albor::Color::rgba(0.13f, 0.15f, 0.19f, 1.0f);
    // the builder runs over the popup stage engines, which is where the menu tree lives
    m_menu_popup = host->open_popup(*this, anchor, size, [this, index](albor::BindingEngine& popup_engine, albor::TextEngine& popup_text) { return build_context_menu(popup_engine, popup_text, index); }, background, {});
}

void studio::ChartsView::close_context_menu() {
    // a view without a surface has nothing to hide
    if (!m_menu_popup.valid())
        return;
    // a live host hides the surface while keeping its tree
    if (albor::PopupHost* host = albor::window_popup_host())
        host->close_popup(m_menu_popup);
}

void studio::ChartsView::destroy_context_menu() {
    // a view without a surface has nothing to release
    if (!m_menu_popup.valid())
        return;
    // a live host destroys the surface and the tree it built
    if (albor::PopupHost* host = albor::window_popup_host())
        host->destroy_popup(m_menu_popup);
    // the handle no longer names a live surface
    m_menu_popup = {};
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
    // expose one select and close action per tab so automation can drive the strip
    if (m_scene != nullptr) {
        // register an action for every recorded tab
        for (std::size_t index = 0; index < m_scene->tabs.size(); ++index) {
            // the actions address the tab by its recorded position
            const int tab_index = static_cast<int>(index);
            // select the tab the action names
            (void)node.add_action("select-tab-" + std::to_string(index), [this, tab_index] {
                // report the selection to the presenter
                if (m_on_tab_selected)
                    m_on_tab_selected(tab_index);
            });
            // a closable tab also exposes its close affordance
            if (m_scene->tabs[index].closable)
                (void)node.add_action("close-tab-" + std::to_string(index), [this, tab_index] {
                    // report the close to the presenter
                    if (m_on_tab_closed)
                        m_on_tab_closed(tab_index);
                });
        }
        // register the context menu commands for every recorded chart
        for (const ChartSlot& slot : m_scene->slots) {
            // the actions address the chart by its recorded index
            const std::size_t chart_index = slot.chart_index;
            // the command prefix keeps every chart's actions apart
            const std::string prefix = "chart-" + std::to_string(chart_index) + "-";
            // the smith plane keeps the plot command only
            if (slot.kind != ChartKind::SMITH) {
                // fit both axes of the chart
                (void)node.add_action(prefix + "zoom-to-fit", [this, chart_index] { run_chart_command(ChartCommand::ZoomToFit, chart_index); });
                // reset only the ordinate of the chart
                (void)node.add_action(prefix + "autorange", [this, chart_index] { run_chart_command(ChartCommand::Autorange, chart_index); });
                // reset the abscissa extent of every chart
                (void)node.add_action(prefix + "zoom-abscissa-extent", [this, chart_index] { run_chart_command(ChartCommand::ZoomAbscissaExtent, chart_index); });
                // insert an empty chart after the chart
                (void)node.add_action(prefix + "add-chart", [this, chart_index] { run_chart_command(ChartCommand::AddChart, chart_index); });
                // remove the chart, keeping at least one chart
                (void)node.add_action(prefix + "delete-chart", [this, chart_index] { run_chart_command(ChartCommand::DeleteChart, chart_index); });
            }
            // drop every plotted series of the chart
            (void)node.add_action(prefix + "delete-all-plots", [this, chart_index] { run_chart_command(ChartCommand::DeleteAllPlots, chart_index); });
        }
    }
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

albor::Point studio::ChartsView::to_scene(const albor::Point& local) const {
    // a view without a scene or extent has no scale to apply
    if (m_scene == nullptr || m_scene->width <= 0.0f || m_scene->height <= 0.0f || bounds().width <= 0.0f || bounds().height <= 0.0f)
        return local;
    // a stale scene is stretched into the arranged extent until the resize settles
    return albor::Point{.x = local.x * (m_scene->width / bounds().width), .y = local.y * (m_scene->height / bounds().height)};
}

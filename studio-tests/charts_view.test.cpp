#include <memory>

#include <gtest/gtest.h>

#include "albor/core/constraints.h"
#include "albor/runtime/binding_engine.h"
#include "albor/ui/ui_event.h"
#include "views/charts/charts_view.h"

TEST(StudioChartsViewChecks, a_bounded_measure_fills_the_host_page) {
    // arrange: a view over a binding engine and no timer registry
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    // act: measure against an exact host extent
    albor::Size measured{};
    view.measure(albor::SizeConstraints::tight(albor::Size{.width = 400.0f, .height = 300.0f}), measured);
    // assert: the view asks for the whole page
    EXPECT_FLOAT_EQ(measured.width, 400.0f);
    EXPECT_FLOAT_EQ(measured.height, 300.0f);
    // act: arrange the view and read its viewport
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 10.0f, .y = 20.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // assert: the viewport reports the arranged size
    EXPECT_FLOAT_EQ(view.viewport_width(), 400.0f);
    EXPECT_FLOAT_EQ(view.viewport_height(), 300.0f);
}

TEST(StudioChartsViewChecks, the_semantic_node_names_the_chart_surface) {
    // arrange: an arranged view
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 10.0f, .y = 20.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    // act: build the semantic node
    const albor::SemanticNode node = view.semantic_node();
    // assert: the panel is a canvas with its author id and arranged bounds
    EXPECT_EQ(node.role(), albor::SemanticRole::Canvas);
    EXPECT_EQ(node.explicit_id(), "charts_view");
    EXPECT_EQ(node.name(), "charts_view");
    EXPECT_FLOAT_EQ(node.bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(node.bounds().y, 20.0f);
}

TEST(StudioChartsViewChecks, a_press_move_and_release_drive_the_zoom_callbacks) {
    // arrange: an arranged view with recording callbacks
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 10.0f, .y = 20.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    float started_x = -1.0f;
    float started_y = -1.0f;
    float moved_x = -1.0f;
    float moved_y = -1.0f;
    int ended = 0;
    view.set_on_zoom_drag_started([&](const float x, const float y) {
        started_x = x;
        started_y = y;
    });
    view.set_on_zoom_drag_moved([&](const float x, const float y) {
        moved_x = x;
        moved_y = y;
    });
    view.set_on_zoom_drag_ended([&] { ended++; });
    // act: press inside the view, move, release
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 10.0f, .y = 20.0f};
    view.dispatch_event(press);
    albor::Event move;
    move.kind = albor::EventKind::PointerMove;
    move.position = albor::Point{.x = 110.0f, .y = 120.0f};
    view.dispatch_event(move);
    albor::Event release;
    release.kind = albor::EventKind::PointerUp;
    release.position = albor::Point{.x = 110.0f, .y = 120.0f};
    view.dispatch_event(release);
    // assert: the callbacks received view local coordinates and the drag completed once
    EXPECT_TRUE(press.handled);
    EXPECT_TRUE(move.handled);
    EXPECT_TRUE(release.handled);
    EXPECT_FLOAT_EQ(started_x, 0.0f);
    EXPECT_FLOAT_EQ(started_y, 0.0f);
    EXPECT_FLOAT_EQ(moved_x, 100.0f);
    EXPECT_FLOAT_EQ(moved_y, 100.0f);
    EXPECT_EQ(ended, 1);
}

TEST(StudioChartsViewChecks, a_press_in_the_tab_strip_selects_or_closes_a_dataset) {
    // arrange: an arranged view showing a two-dataset tab strip
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->tab_bar_height = studio::CHART_TAB_HEIGHT;
    scene->content_y = studio::CHART_TAB_HEIGHT;
    scene->tabs.push_back(studio::ChartTab{.title = "first", .active = true, .closable = false, .bounds = albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 100.0f, .height = studio::CHART_TAB_HEIGHT}), .close_bounds = {}});
    scene->tabs.push_back(studio::ChartTab{.title = "second", .active = false, .closable = true, .bounds = albor::Rect::from_origin_size(albor::Point{.x = 100.0f, .y = 0.0f}, albor::Size{.width = 100.0f, .height = studio::CHART_TAB_HEIGHT}), .close_bounds = albor::Rect::from_origin_size(albor::Point{.x = 180.0f, .y = 9.0f}, albor::Size{.width = 14.0f, .height = 14.0f})});
    view.set_scene(scene);
    int selected = -1;
    int closed = -1;
    view.set_on_tab_selected([&](const int index) { selected = index; });
    view.set_on_tab_closed([&](const int index) { closed = index; });
    // act: press the first tab body
    albor::Event body;
    body.kind = albor::EventKind::PointerDown;
    body.position = albor::Point{.x = 10.0f, .y = 10.0f};
    view.dispatch_event(body);
    // assert: the first tab was selected and the press was claimed
    EXPECT_TRUE(body.handled);
    EXPECT_EQ(selected, 0);
    // act: press the close cross of the second tab
    albor::Event close;
    close.kind = albor::EventKind::PointerDown;
    close.position = albor::Point{.x = 185.0f, .y = 16.0f};
    view.dispatch_event(close);
    // assert: the second tab close was reported and the press was claimed
    EXPECT_TRUE(close.handled);
    EXPECT_EQ(closed, 1);
}

TEST(StudioChartsViewChecks, cancel_drag_reports_the_cancellation_once) {
    // arrange: an arranged view with a press owned by a drag
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 10.0f, .y = 20.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    int canceled = 0;
    view.set_on_zoom_drag_canceled([&] { canceled++; });
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 10.0f, .y = 20.0f};
    view.dispatch_event(press);
    // act: cancel the drag without a pointer event
    view.cancel_drag();
    // assert: the cancellation ran once and a second call is inert
    EXPECT_EQ(canceled, 1);
    view.cancel_drag();
    EXPECT_EQ(canceled, 1);
}

TEST(StudioChartsViewChecks, a_leave_cancels_a_drag_and_clears_a_hover) {
    // arrange: an arranged view with recording callbacks
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 10.0f, .y = 20.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    int canceled = 0;
    int hover_ended = 0;
    view.set_on_zoom_drag_canceled([&] { canceled++; });
    view.set_on_hover_ended([&] { hover_ended++; });
    // act: start a drag and leave the view
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 10.0f, .y = 20.0f};
    view.dispatch_event(press);
    albor::Event leave;
    leave.kind = albor::EventKind::PointerLeave;
    leave.position = albor::Point{.x = 500.0f, .y = 500.0f};
    view.dispatch_event(leave);
    // get ready for the hover leave without a drag
    albor::Event release;
    release.kind = albor::EventKind::PointerUp;
    release.position = albor::Point{.x = 20.0f, .y = 30.0f};
    view.dispatch_event(release);
    albor::Event second_leave;
    second_leave.kind = albor::EventKind::PointerLeave;
    second_leave.position = albor::Point{.x = 500.0f, .y = 500.0f};
    view.dispatch_event(second_leave);
    // assert: the drag canceled and the second leave cleared the hover
    EXPECT_EQ(canceled, 1);
    EXPECT_EQ(hover_ended, 1);
}

TEST(StudioChartsViewChecks, a_strip_press_outside_a_tab_is_consumed_without_a_zoom) {
    // arrange: an arranged view with one tab and a strip
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->tab_bar_height = studio::CHART_TAB_HEIGHT;
    scene->content_y = studio::CHART_TAB_HEIGHT;
    scene->tabs.push_back(studio::ChartTab{.title = "only", .active = true, .closable = false, .bounds = albor::Rect::from_origin_size(albor::Point{.x = 8.0f, .y = 0.0f}, albor::Size{.width = 96.0f, .height = studio::CHART_TAB_HEIGHT}), .close_bounds = {}});
    view.set_scene(scene);
    int selected = -1;
    int zoom_started = 0;
    view.set_on_tab_selected([&](const int index) { selected = index; });
    view.set_on_zoom_drag_started([&](const float, const float) { zoom_started++; });
    // act: press the empty part of the strip
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 300.0f, .y = 10.0f};
    view.dispatch_event(press);
    // assert: the strip consumed the press without selecting or zooming
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(selected, -1);
    EXPECT_EQ(zoom_started, 0);
}

TEST(StudioChartsViewChecks, a_chart_press_below_the_strip_uses_content_coordinates) {
    // arrange: an arranged view whose scene reserves a tab strip
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->tab_bar_height = studio::CHART_TAB_HEIGHT;
    scene->content_y = studio::CHART_TAB_HEIGHT;
    view.set_scene(scene);
    float started_x = -1.0f;
    float started_y = -1.0f;
    view.set_on_zoom_drag_started([&](const float x, const float y) {
        started_x = x;
        started_y = y;
    });
    // act: press 50 pixels below the panel top
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 80.0f, .y = 50.0f};
    view.dispatch_event(press);
    // assert: the controller receives the chart content coordinates
    EXPECT_TRUE(press.handled);
    EXPECT_FLOAT_EQ(started_x, 80.0f);
    EXPECT_FLOAT_EQ(started_y, 18.0f);
}

TEST(StudioChartsViewChecks, a_tab_close_press_does_not_select_the_tab_body) {
    // arrange: a view with a closable tab and recording callbacks
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->tab_bar_height = studio::CHART_TAB_HEIGHT;
    scene->content_y = studio::CHART_TAB_HEIGHT;
    scene->tabs.push_back(studio::ChartTab{.title = "only", .active = false, .closable = true, .bounds = albor::Rect::from_origin_size(albor::Point{.x = 8.0f, .y = 0.0f}, albor::Size{.width = 120.0f, .height = studio::CHART_TAB_HEIGHT}), .close_bounds = albor::Rect::from_origin_size(albor::Point{.x = 110.0f, .y = 8.0f}, albor::Size{.width = 16.0f, .height = 16.0f})});
    view.set_scene(scene);
    int selected = -1;
    int closed = -1;
    view.set_on_tab_selected([&](const int index) { selected = index; });
    view.set_on_tab_closed([&](const int index) { closed = index; });
    // act: press the close cross
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 118.0f, .y = 16.0f};
    view.dispatch_event(press);
    // assert: only the close callback ran
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(closed, 0);
    EXPECT_EQ(selected, -1);
}

TEST(StudioChartsViewChecks, a_scene_replacement_drops_the_active_drag) {
    // arrange: a drag started against the first scene
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto first = std::make_shared<studio::ChartScene>();
    first->width = 400.0f;
    first->height = 300.0f;
    view.set_scene(first);
    int canceled = 0;
    int ended = 0;
    view.set_on_zoom_drag_canceled([&] { canceled++; });
    view.set_on_zoom_drag_ended([&] { ended++; });
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 100.0f, .y = 100.0f};
    view.dispatch_event(press);
    // act: replace the scene before the release
    auto second = std::make_shared<studio::ChartScene>();
    second->width = 400.0f;
    second->height = 300.0f;
    view.set_scene(second);
    albor::Event release;
    release.kind = albor::EventKind::PointerUp;
    release.position = albor::Point{.x = 200.0f, .y = 200.0f};
    view.dispatch_event(release);
    // assert: the replacement canceled the drag and the release completed nothing
    EXPECT_EQ(canceled, 1);
    EXPECT_EQ(ended, 0);
}

TEST(StudioChartsViewChecks, a_stale_scene_maps_the_pointer_into_scene_coordinates) {
    // arrange: a scene recorded at 400 by 300 stretched into a 200 by 150 view
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 200.0f, .height = 150.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->tab_bar_height = studio::CHART_TAB_HEIGHT;
    scene->content_y = studio::CHART_TAB_HEIGHT;
    scene->tabs.push_back(studio::ChartTab{.title = "second", .active = false, .closable = false, .bounds = albor::Rect::from_origin_size(albor::Point{.x = 120.0f, .y = 0.0f}, albor::Size{.width = 100.0f, .height = studio::CHART_TAB_HEIGHT}), .close_bounds = {}});
    view.set_scene(scene);
    int selected = -1;
    view.set_on_tab_selected([&](const int index) { selected = index; });
    // act: press the tab where the stretched picture shows it
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 85.0f, .y = 10.0f};
    view.dispatch_event(press);
    // assert: the press maps back into the recorded scene before the hit test
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(selected, 0);
}

TEST(StudioChartsViewChecks, the_semantic_node_exposes_tab_select_and_close_actions) {
    // arrange: a view with one closable tab
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->tab_bar_height = studio::CHART_TAB_HEIGHT;
    scene->content_y = studio::CHART_TAB_HEIGHT;
    scene->tabs.push_back(studio::ChartTab{.title = "only", .active = true, .closable = true, .bounds = albor::Rect::from_origin_size(albor::Point{.x = 8.0f, .y = 0.0f}, albor::Size{.width = 120.0f, .height = studio::CHART_TAB_HEIGHT}), .close_bounds = albor::Rect::from_origin_size(albor::Point{.x = 110.0f, .y = 8.0f}, albor::Size{.width = 16.0f, .height = 16.0f})});
    view.set_scene(scene);
    int selected = -1;
    int closed = -1;
    view.set_on_tab_selected([&](const int index) { selected = index; });
    view.set_on_tab_closed([&](const int index) { closed = index; });
    // act: invoke the tab actions the semantic node publishes
    const albor::SemanticNode node = view.semantic_node();
    // assert: both actions exist and reach their callbacks
    EXPECT_TRUE(node.has_action("select-tab-0"));
    EXPECT_TRUE(node.has_action("close-tab-0"));
    EXPECT_TRUE(node.invoke("select-tab-0"));
    EXPECT_EQ(selected, 0);
    EXPECT_TRUE(node.invoke("close-tab-0"));
    EXPECT_EQ(closed, 0);
}

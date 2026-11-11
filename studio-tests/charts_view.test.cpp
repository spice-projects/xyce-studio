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

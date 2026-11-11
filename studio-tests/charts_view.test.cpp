#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "albor/core/constraints.h"
#include "albor/render/shaped_run.h"
#include "albor/runtime/binding_engine.h"
#include "albor/text/text_engine.h"
#include "albor/ui/ui_event.h"
#include "albor/widgets/menu.h"
#include "albor/widgets/menu_item.h"
#include "albor/window/popup_host.h"
#include "views/charts/charts_view.h"

namespace
{
    // the text engine double shapes every menu label into a fixed run
    class FakeTextEngine final : public albor::TextEngine
    {
    public:
        albor::TextLayout measure(const std::string_view text, const float max_width) const override {
            // the unbounded measure delegates to the explicit size overload
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        albor::TextLayout measure(const std::string_view text, const float max_width, const float font_size) const override {
            // ignore the ceiling, because menu labels are single line
            (void)max_width;
            // build the line the run hands to the renderer
            albor::ShapedRun::Line line;
            // place the baseline below the run origin
            line.origin = albor::Point{.x = 0.0f, .y = font_size * 0.8f};
            // give the run one glyph per character so it is not empty
            for (std::size_t index = 0; index < text.size(); ++index)
                line.glyphs.push_back(albor::ShapedGlyph{.id = static_cast<std::uint32_t>(index), .position = albor::Point{.x = static_cast<float>(index) * 6.0f, .y = 0.0f}});
            // wrap the line in an immutable shaped run
            std::shared_ptr<const albor::ShapedRun> run = albor::ShapedRun::make({std::move(line)}, font_size);
            // return a deterministic run whose extent grows with the text
            return albor::TextLayout{.run = std::move(run), .size = albor::Size{.width = static_cast<float>(text.size()) * 6.0f, .height = font_size}, .ascent = font_size * 0.8f, .descent = font_size * 0.2f};
        }

        [[nodiscard]] bool ready() const override { return true; }
    };

    // the popup host double records the surface the view opens
    class FakePopupHost final : public albor::PopupHost
    {
    public:
        // the handle every accepted surface is named by
        albor::WindowHandle handle = albor::WindowHandle::from_id(1000);

        // the anchor rectangle the last opening named
        albor::Rect anchor_rect{};

        // the surface extent the last opening named
        albor::Size size{};

        // how many openings, hides and destructions arrived
        std::size_t opens = 0;

        std::size_t closes = 0;

        std::size_t destroys = 0;

        // the tree builder the host received, which the test invokes to cover the surface
        albor::PopupRootBuilder builder;

        // the dismissal work the view installed
        std::function<void()> on_closed;

        [[nodiscard]] albor::WindowHandle open_popup(const albor::UiNode&, const albor::Rect anchor_bounds, const albor::Size surface_size, albor::PopupRootBuilder root_builder, const albor::Color, std::function<void()> closed) override {
            // the request is recorded before the answer
            opens++;
            // remember where and how large the surface opens
            anchor_rect = anchor_bounds;
            size = surface_size;
            // keep the builder the test invokes
            builder = std::move(root_builder);
            on_closed = std::move(closed);
            return handle;
        }

        void close_popup(albor::WindowHandle) override {
            // count the hide the view asked for
            closes++;
        }

        void show_popup(albor::WindowHandle) override {}

        void destroy_popup(albor::WindowHandle) override {
            // count the release the view asked for
            destroys++;
        }
    };
} // namespace

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

TEST(StudioChartsViewChecks, the_context_menu_offers_the_line_chart_commands) {
    // arrange: a view showing one cartesian chart over a fake popup host
    albor::BindingEngine engine;
    FakeTextEngine text;
    FakePopupHost host;
    albor::set_window_popup_host(&host);
    studio::ChartsView view(&engine, nullptr);
    view.set_text_engine(&text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->slots.push_back(studio::ChartSlot{.chart_index = 0, .kind = ChartKind::XY, .frame = albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}), .plot = albor::Rect::from_origin_size(albor::Point{.x = 40.0f, .y = 20.0f}, albor::Size{.width = 340.0f, .height = 260.0f})});
    view.set_scene(scene);
    // act: a secondary press opens the chart menu
    (void)view.request_context_menu(albor::Point{.x = 100.0f, .y = 120.0f});
    // assert: one surface opened where the press landed
    ASSERT_EQ(host.opens, 1U);
    EXPECT_FLOAT_EQ(host.anchor_rect.x, 100.0f);
    EXPECT_FLOAT_EQ(host.anchor_rect.y, 120.0f);
    EXPECT_GT(host.size.width, 0.0f);
    EXPECT_GT(host.size.height, 0.0f);
    // act: build the tree the popup stage would show
    albor::BindingEngine popup_engine;
    FakeTextEngine popup_text;
    std::unique_ptr<albor::UiNode> root = host.builder(popup_engine, popup_text);
    auto* menu = dynamic_cast<albor::Menu*>(root.get());
    ASSERT_NE(menu, nullptr);
    // assert: the rows present the cartesian commands in menu order
    std::vector<std::string> labels;
    for (const auto& child : menu->children()) {
        // a separator has no label to present
        const auto* item = dynamic_cast<const albor::MenuItem*>(child.get());
        labels.push_back(item != nullptr ? item->label() : std::string("-"));
    }
    EXPECT_EQ(labels, (std::vector<std::string>{"Zoom to Fit", "Autorange", "Zoom Abscissa Extent", "-", "Delete All Plots", "-", "Add Chart", "Delete Chart"}));
    // the process wide host is restored before the next test
    albor::set_window_popup_host(nullptr);
}

TEST(StudioChartsViewChecks, a_smith_chart_context_menu_offers_only_the_plot_command) {
    // arrange: a view showing one smith chart over a fake popup host
    albor::BindingEngine engine;
    FakeTextEngine text;
    FakePopupHost host;
    albor::set_window_popup_host(&host);
    studio::ChartsView view(&engine, nullptr);
    view.set_text_engine(&text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->slots.push_back(studio::ChartSlot{.chart_index = 0, .kind = ChartKind::SMITH, .frame = albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}), .plot = albor::Rect::from_origin_size(albor::Point{.x = 40.0f, .y = 20.0f}, albor::Size{.width = 260.0f, .height = 260.0f})});
    view.set_scene(scene);
    // act: a secondary press opens the chart menu
    (void)view.request_context_menu(albor::Point{.x = 100.0f, .y = 120.0f});
    ASSERT_EQ(host.opens, 1U);
    // act: build the tree the popup stage would show
    albor::BindingEngine popup_engine;
    FakeTextEngine popup_text;
    std::unique_ptr<albor::UiNode> root = host.builder(popup_engine, popup_text);
    auto* menu = dynamic_cast<albor::Menu*>(root.get());
    ASSERT_NE(menu, nullptr);
    // assert: the fixed plane offers the plot command only
    std::vector<std::string> labels;
    for (const auto& child : menu->children()) {
        // a separator has no label to present
        const auto* item = dynamic_cast<const albor::MenuItem*>(child.get());
        labels.push_back(item != nullptr ? item->label() : std::string("-"));
    }
    EXPECT_EQ(labels, (std::vector<std::string>{"Delete All Plots"}));
    // the process wide host is restored before the next test
    albor::set_window_popup_host(nullptr);
}

TEST(StudioChartsViewChecks, a_context_menu_press_runs_the_command_once_and_closes_the_surface) {
    // arrange: a view whose menu rows report to a command recorder
    albor::BindingEngine engine;
    FakeTextEngine text;
    FakePopupHost host;
    albor::set_window_popup_host(&host);
    studio::ChartsView view(&engine, nullptr);
    view.set_text_engine(&text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->slots.push_back(studio::ChartSlot{.chart_index = 0, .kind = ChartKind::XY, .frame = albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}), .plot = albor::Rect::from_origin_size(albor::Point{.x = 40.0f, .y = 20.0f}, albor::Size{.width = 340.0f, .height = 260.0f})});
    view.set_scene(scene);
    std::vector<std::pair<studio::ChartCommand, std::size_t>> commands;
    view.set_on_chart_command([&](const studio::ChartCommand command, const std::size_t index) { commands.emplace_back(command, index); });
    // act: open the menu and build the tree the popup stage would show
    (void)view.request_context_menu(albor::Point{.x = 100.0f, .y = 120.0f});
    ASSERT_EQ(host.opens, 1U);
    albor::BindingEngine popup_engine;
    FakeTextEngine popup_text;
    std::unique_ptr<albor::UiNode> root = host.builder(popup_engine, popup_text);
    auto* menu = dynamic_cast<albor::Menu*>(root.get());
    ASSERT_NE(menu, nullptr);
    // arrange the menu to its measured extent so the rows carry bounds
    albor::Size size{};
    menu->measure(albor::SizeConstraints::unconstrained(), size);
    menu->arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, size));
    // find the row that removes the chart
    albor::MenuItem* target = nullptr;
    for (const auto& child : menu->children()) {
        auto* item = dynamic_cast<albor::MenuItem*>(child.get());
        if (item != nullptr && item->label() == "Delete Chart")
            target = item;
    }
    ASSERT_NE(target, nullptr);
    // act: press the row
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.button = albor::PointerButton::Left;
    press.position = albor::Point{.x = target->bounds().x + 5.0f, .y = target->bounds().y + 5.0f};
    target->dispatch_event(press);
    // assert: the command reached the presenter once and the surface hid
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_EQ(commands[0].first, studio::ChartCommand::DeleteChart);
    EXPECT_EQ(commands[0].second, 0U);
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(host.closes, 1U);
    // the process wide host is restored before the next test
    albor::set_window_popup_host(nullptr);
}

TEST(StudioChartsViewChecks, the_context_menu_ignores_the_dataset_strip) {
    // arrange: a view whose strip sits above the chart content
    albor::BindingEngine engine;
    FakeTextEngine text;
    FakePopupHost host;
    albor::set_window_popup_host(&host);
    studio::ChartsView view(&engine, nullptr);
    view.set_text_engine(&text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->tab_bar_height = studio::CHART_TAB_HEIGHT;
    scene->content_y = studio::CHART_TAB_HEIGHT;
    scene->slots.push_back(studio::ChartSlot{.chart_index = 0, .kind = ChartKind::XY, .frame = albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = studio::CHART_TAB_HEIGHT}, albor::Size{.width = 400.0f, .height = 300.0f - studio::CHART_TAB_HEIGHT}), .plot = albor::Rect::from_origin_size(albor::Point{.x = 40.0f, .y = studio::CHART_TAB_HEIGHT + 20.0f}, albor::Size{.width = 340.0f, .height = 260.0f})});
    view.set_scene(scene);
    // act: a secondary press lands on the strip
    (void)view.request_context_menu(albor::Point{.x = 100.0f, .y = 10.0f});
    // assert: the strip keeps its own interactions and opens no menu
    EXPECT_EQ(host.opens, 0U);
    // the process wide host is restored before the next test
    albor::set_window_popup_host(nullptr);
}

TEST(StudioChartsViewChecks, the_semantic_node_exposes_the_chart_menu_actions) {
    // arrange: a view that records the commands its actions run
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->slots.push_back(studio::ChartSlot{.chart_index = 0, .kind = ChartKind::XY, .frame = albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}), .plot = albor::Rect::from_origin_size(albor::Point{.x = 40.0f, .y = 20.0f}, albor::Size{.width = 340.0f, .height = 260.0f})});
    view.set_scene(scene);
    std::vector<std::pair<studio::ChartCommand, std::size_t>> commands;
    view.set_on_chart_command([&](const studio::ChartCommand command, const std::size_t index) { commands.emplace_back(command, index); });
    // act: read the actions the semantic node publishes
    const albor::SemanticNode node = view.semantic_node();
    // assert: the cartesian chart exposes every menu command
    EXPECT_TRUE(node.has_action("chart-0-zoom-to-fit"));
    EXPECT_TRUE(node.has_action("chart-0-autorange"));
    EXPECT_TRUE(node.has_action("chart-0-zoom-abscissa-extent"));
    EXPECT_TRUE(node.has_action("chart-0-delete-all-plots"));
    EXPECT_TRUE(node.has_action("chart-0-add-chart"));
    // act: run the add chart action
    EXPECT_TRUE(node.invoke("chart-0-add-chart"));
    // assert: the command reached the presenter with the chart index
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_EQ(commands[0].first, studio::ChartCommand::AddChart);
    EXPECT_EQ(commands[0].second, 0U);
    // arrange: replace the scene with a smith chart
    auto smith = std::make_shared<studio::ChartScene>();
    smith->width = 400.0f;
    smith->height = 300.0f;
    smith->slots.push_back(studio::ChartSlot{.chart_index = 0, .kind = ChartKind::SMITH, .frame = albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}), .plot = albor::Rect::from_origin_size(albor::Point{.x = 40.0f, .y = 20.0f}, albor::Size{.width = 260.0f, .height = 260.0f})});
    view.set_scene(smith);
    const albor::SemanticNode smith_node = view.semantic_node();
    // assert: the fixed plane exposes the plot command only
    EXPECT_FALSE(smith_node.has_action("chart-0-zoom-to-fit"));
    EXPECT_FALSE(smith_node.has_action("chart-0-add-chart"));
    EXPECT_TRUE(smith_node.has_action("chart-0-delete-all-plots"));
}

TEST(StudioChartsViewChecks, the_next_context_menu_releases_the_previous_surface) {
    // arrange: a view showing one chart over a fake popup host
    albor::BindingEngine engine;
    FakeTextEngine text;
    FakePopupHost host;
    albor::set_window_popup_host(&host);
    studio::ChartsView view(&engine, nullptr);
    view.set_text_engine(&text);
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}));
    auto scene = std::make_shared<studio::ChartScene>();
    scene->width = 400.0f;
    scene->height = 300.0f;
    scene->slots.push_back(studio::ChartSlot{.chart_index = 0, .kind = ChartKind::XY, .frame = albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 300.0f}), .plot = albor::Rect::from_origin_size(albor::Point{.x = 40.0f, .y = 20.0f}, albor::Size{.width = 340.0f, .height = 260.0f})});
    view.set_scene(scene);
    // act: open the menu twice in a row
    (void)view.request_context_menu(albor::Point{.x = 100.0f, .y = 120.0f});
    (void)view.request_context_menu(albor::Point{.x = 140.0f, .y = 160.0f});
    // assert: the replacement released the surface it replaced
    EXPECT_EQ(host.opens, 2U);
    EXPECT_EQ(host.destroys, 1U);
    EXPECT_FLOAT_EQ(host.anchor_rect.x, 140.0f);
    // the process wide host is restored before the next test
    albor::set_window_popup_host(nullptr);
}

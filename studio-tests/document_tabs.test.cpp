#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "albor/core/paint.h"
#include "albor/render/draw_command.h"
#include "albor/render/picture_resource.h"
#include "albor/render/shaped_run.h"
#include "albor/runtime/binding_engine.h"
#include "albor/text/text_engine.h"
#include "charts/chart_palette.h"
#include "views/main_window/document_tabs.h"

namespace
{
    // the text engine double shapes one glyph per character
    class FakeTextEngine final : public albor::TextEngine
    {
    public:
        albor::TextLayout measure(const std::string_view text, const float max_width) const override {
            // the unbounded measure delegates to the explicit size overload
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        albor::TextLayout measure(const std::string_view text, const float max_width, const float font_size) const override {
            // ignore the ceiling, because tab titles are single line
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
} // namespace

TEST(StudioDocumentTabsChecks, the_netlist_is_the_first_tab_and_never_closes) {
    // arrange: a netlist document and one closable dataset
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "data", .closable = true}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    // assert: both tabs lay out and only the dataset carries a close box
    EXPECT_FALSE(tabs.tab_bounds(0).empty());
    EXPECT_FALSE(tabs.tab_bounds(1).empty());
    EXPECT_TRUE(tabs.close_bounds(0).empty());
    EXPECT_FALSE(tabs.close_bounds(1).empty());
    // assert: the tabs sit left to right
    EXPECT_LT(tabs.tab_bounds(0).x, tabs.tab_bounds(1).x);
}

TEST(StudioDocumentTabsChecks, a_tab_press_selects_and_a_close_press_closes) {
    // arrange: a strip with one dataset ready to close
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "data", .closable = true}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    int selected = -1;
    int closed = -1;
    tabs.set_on_selected([&](const int index) { selected = index; });
    tabs.set_on_closed([&](const int index) { closed = index; });
    // act: press the netlist tab body
    const albor::Rect netlist = tabs.tab_bounds(0);
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = netlist.x + netlist.width * 0.5f, .y = 16.0f};
    tabs.dispatch_event(press);
    // assert: the netlist tab is selected
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(selected, 0);
    // act: press the dataset close box
    const albor::Rect close = tabs.close_bounds(1);
    albor::Event close_press;
    close_press.kind = albor::EventKind::PointerDown;
    close_press.position = albor::Point{.x = close.x + close.width * 0.5f, .y = close.y + close.height * 0.5f};
    tabs.dispatch_event(close_press);
    // assert: closing the dataset does not also select it
    EXPECT_TRUE(close_press.handled);
    EXPECT_EQ(closed, 1);
    EXPECT_EQ(selected, 0);
}

TEST(StudioDocumentTabsChecks, a_press_outside_every_tab_is_consumed) {
    // arrange: a short strip with lots of empty space
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    int selected = -1;
    tabs.set_on_selected([&](const int index) { selected = index; });
    // act: press the empty strip past the last tab
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 380.0f, .y = 16.0f};
    tabs.dispatch_event(press);
    // assert: the strip consumed the press without selecting anything
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(selected, -1);
}

TEST(StudioDocumentTabsChecks, the_strip_paints_a_plate_hairline_and_tabs) {
    // arrange: a strip with an active netlist and one dataset
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "data", .closable = true}});
    tabs.set_active(0);
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    // act: paint the strip
    albor::PictureRecorder recorder;
    tabs.paint_content(recorder);
    const std::vector<albor::DrawCommand> commands = recorder.commands();
    // assert: the plate covers the strip and the hairline sits on its bottom edge
    ASSERT_GE(commands.size(), 5U);
    EXPECT_FLOAT_EQ(commands[0].rect_geometry().rect.width, 400.0f);
    EXPECT_FLOAT_EQ(commands[0].rect_geometry().rect.height, 32.0f);
    EXPECT_FLOAT_EQ(commands[1].rect_geometry().rect.y, 31.0f);
    EXPECT_FLOAT_EQ(commands[1].rect_geometry().rect.height, 1.0f);
}

TEST(StudioDocumentTabsChecks, the_semantic_node_exposes_select_and_close_actions) {
    // arrange: a netlist tab and one closable dataset
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "data", .closable = true}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    // act: read the semantic node
    const albor::SemanticNode node = tabs.semantic_node();
    // assert: every tab selects and only the dataset closes
    EXPECT_TRUE(node.has_action("select-tab-0"));
    EXPECT_TRUE(node.has_action("select-tab-1"));
    EXPECT_FALSE(node.has_action("close-tab-0"));
    EXPECT_TRUE(node.has_action("close-tab-1"));
    // assert: the name carries the active document
    EXPECT_EQ(node.name(), "Documents, active: Netlist");
}

TEST(StudioDocumentTabsChecks, the_strip_picture_is_reused_until_a_key_input_changes) {
    // arrange: a two document strip
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "data", .closable = true}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    albor::PictureRecorder first;
    tabs.paint_content(first);
    // assert: the first paint records once
    ASSERT_EQ(tabs.strip_record_count(), 1U);
    // act: paint again with no state change
    albor::PictureRecorder second;
    tabs.paint_content(second);
    // assert: the picture is reused
    EXPECT_EQ(tabs.strip_record_count(), 1U);
    // act: the selection moves
    tabs.set_active(1);
    albor::PictureRecorder third;
    tabs.paint_content(third);
    // assert: the active plate change re-records
    EXPECT_EQ(tabs.strip_record_count(), 2U);
    // act: a title changes
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "other", .closable = true}});
    albor::PictureRecorder fourth;
    tabs.paint_content(fourth);
    // assert: the new title re-records
    EXPECT_EQ(tabs.strip_record_count(), 3U);
    // act: the same palette is written again
    tabs.set_palette(CHART_PALETTE_LIGHT);
    albor::PictureRecorder fifth;
    tabs.paint_content(fifth);
    // assert: an unchanged palette reuses the picture
    EXPECT_EQ(tabs.strip_record_count(), 3U);
}

TEST(StudioDocumentTabsChecks, a_palette_change_that_keeps_the_red_channels_rerecords) {
    // arrange: a strip recorded with the light palette
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    albor::PictureRecorder first;
    tabs.paint_content(first);
    ASSERT_EQ(tabs.strip_record_count(), 1U);
    // act: flip only the green channel of every role
    ChartPalette shifted = CHART_PALETTE_LIGHT;
    shifted.text.g = 0.5f;
    shifted.muted.g = 0.5f;
    shifted.background.g = 0.5f;
    shifted.panel.g = 0.5f;
    shifted.border.g = 0.5f;
    shifted.grid.g = 0.5f;
    tabs.set_palette(shifted);
    albor::PictureRecorder second;
    tabs.paint_content(second);
    // assert: the key covers every channel, so the picture is rebuilt
    EXPECT_EQ(tabs.strip_record_count(), 2U);
}

TEST(StudioDocumentTabsChecks, a_shrunk_document_list_drops_the_retired_hit_rects) {
    // arrange: two tabs laid out at a known extent
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "data", .closable = true}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    const albor::Rect retired = tabs.close_bounds(1);
    int closed = 0;
    tabs.set_on_closed([&](int) { closed++; });
    // act: the dataset leaves before any paint, then its old close point is pressed
    tabs.set_documents({{.title = "Netlist", .closable = false}});
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = retired.x + retired.width * 0.5f, .y = retired.y + retired.height * 0.5f};
    tabs.dispatch_event(press);
    // assert: the retired close box no longer answers
    EXPECT_EQ(closed, 0);
}

TEST(StudioDocumentTabsChecks, set_documents_clamps_the_active_index) {
    // arrange: three documents with the last one active
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "a", .closable = true}, {.title = "b", .closable = true}});
    tabs.set_active(2);
    // act: the set shrinks to two
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "a", .closable = true}});
    // assert: the selection clamps to the last remaining tab
    EXPECT_EQ(tabs.active_index(), 1U);
}

TEST(StudioDocumentTabsChecks, a_press_on_the_netlist_close_slot_selects_the_netlist) {
    // arrange: a single netlist tab
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    int selected = -1;
    int closed = 0;
    tabs.set_on_selected([&](const int index) { selected = index; });
    tabs.set_on_closed([&](int) { closed++; });
    // act: press the east edge of the netlist tab, where a close box would sit
    const albor::Rect tab = tabs.tab_bounds(0);
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = tab.x + tab.width - 8.0f, .y = 16.0f};
    tabs.dispatch_event(press);
    // assert: the press selects the netlist and never closes it
    EXPECT_EQ(selected, 0);
    EXPECT_EQ(closed, 0);
}

TEST(StudioDocumentTabsChecks, an_empty_document_list_records_no_rect_and_consumes_a_press) {
    // arrange: a strip with no documents
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    int selected = -1;
    tabs.set_on_selected([&](const int index) { selected = index; });
    // act: press the empty strip
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.position = albor::Point{.x = 10.0f, .y = 16.0f};
    tabs.dispatch_event(press);
    // assert: no tab exists and the press is consumed
    EXPECT_TRUE(tabs.tab_bounds(0).empty());
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(selected, -1);
}

TEST(StudioDocumentTabsChecks, a_zero_width_arrangement_records_no_rect) {
    // arrange: a strip with a document but no width
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 0.0f, .height = 32.0f}));
    // assert: a strip without width has no hit rectangle
    EXPECT_TRUE(tabs.tab_bounds(0).empty());
}

TEST(StudioDocumentTabsChecks, the_strip_measure_keeps_the_tab_height_and_fills_the_width) {
    // arrange: a bounded and an unbounded offer
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    albor::Size bounded{};
    albor::Size unbounded{};
    // act: measure under both offers
    tabs.measure(albor::SizeConstraints::at_most(albor::Size{.width = 500.0f, .height = 800.0f}), bounded);
    tabs.measure(albor::SizeConstraints::unconstrained(), unbounded);
    // assert: the strip fills the width and always keeps its tab height
    EXPECT_FLOAT_EQ(bounded.width, 500.0f);
    EXPECT_FLOAT_EQ(bounded.height, 32.0f);
    EXPECT_FLOAT_EQ(unbounded.width, 640.0f);
    EXPECT_FLOAT_EQ(unbounded.height, 32.0f);
}

TEST(StudioDocumentTabsChecks, a_squeezed_strip_keeps_every_tab_reachable) {
    // arrange: four documents in a narrow strip
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "aaaa", .closable = true}, {.title = "bbbb", .closable = true}, {.title = "cccc", .closable = true}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 120.0f, .height = 32.0f}));
    // assert: every document keeps a rectangle, and a narrow tab hides the close box
    for (std::size_t index = 0; index < 4; ++index) {
        EXPECT_FALSE(tabs.tab_bounds(index).empty());
        EXPECT_GT(tabs.tab_bounds(index).width, 0.0f);
        EXPECT_TRUE(tabs.close_bounds(index).empty());
    }
}

TEST(StudioDocumentTabsChecks, the_arrow_keys_move_the_selection) {
    // arrange: a three document strip with a recording callback
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "a", .closable = true}, {.title = "b", .closable = true}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    std::vector<int> selected;
    tabs.set_on_selected([&](const int index) { selected.push_back(index); });
    // act: walk right, jump to the end and back home
    albor::Event right;
    right.kind = albor::EventKind::KeyDown;
    right.key = albor::Key::ArrowRight;
    tabs.dispatch_event(right);
    albor::Event end;
    end.kind = albor::EventKind::KeyDown;
    end.key = albor::Key::End;
    tabs.dispatch_event(end);
    albor::Event home;
    home.kind = albor::EventKind::KeyDown;
    home.key = albor::Key::Home;
    tabs.dispatch_event(home);
    // assert: every move is reported once
    ASSERT_EQ(selected.size(), 3U);
    EXPECT_EQ(selected[0], 1);
    EXPECT_EQ(selected[1], 2);
    EXPECT_EQ(selected[2], 0);
    EXPECT_EQ(tabs.active_index(), 0U);
    EXPECT_TRUE(right.handled);
    // assert: the focus ring frames the active tab
    EXPECT_FLOAT_EQ(tabs.focus_ring_rect().x, tabs.tab_bounds(0).x);
}

TEST(StudioDocumentTabsChecks, the_arrow_key_at_the_last_tab_is_left_to_the_parent) {
    // arrange: the last document is active
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::DocumentTabs tabs(&engine, &text);
    tabs.set_documents({{.title = "Netlist", .closable = false}, {.title = "a", .closable = true}});
    tabs.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 400.0f, .height = 32.0f}));
    tabs.set_active(1);
    int selected = -1;
    tabs.set_on_selected([&](const int index) { selected = index; });
    // act: press right with nowhere to go
    albor::Event right;
    right.kind = albor::EventKind::KeyDown;
    right.key = albor::Key::ArrowRight;
    tabs.dispatch_event(right);
    // assert: the key is not consumed and no move is reported
    EXPECT_FALSE(right.handled);
    EXPECT_EQ(selected, -1);
}

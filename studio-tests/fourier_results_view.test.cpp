#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "albor/render/shaped_run.h"
#include "albor/runtime/binding_engine.h"
#include "albor/text/text_engine.h"
#include "charts/fourier_results.h"
#include "views/charts/fourier_results_view.h"

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
            // ignore the ceiling, because table labels are single line
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

TEST(StudioFourierResultsViewChecks, the_table_announces_its_blocks_and_harmonic_rows) {
    // arrange: a table over one signal block with two harmonics
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    view.set_title("FOUR: 1000 Hz");
    view.set_tables({{.signal = "V(1)", .step = "", .thd = "43.57 %", .harmonics = "2", .gridsize = "200", .interpolation = "Cubic Spline", .rows = {{.harmonic = 0, .frequency = "0.000000e+00", .magnitude = "3.125000e-01", .phase = "0.000000e+00", .norm_magnitude = "0.000000e+00", .norm_phase = "0.000000e+00"}, {.harmonic = 1, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00", .norm_magnitude = "1.000000e+00", .norm_phase = "0.000000e+00"}}}});
    // assert: the panel names the dataset it presents
    EXPECT_EQ(view.semantic_node().name(), "Fourier results: FOUR: 1000 Hz");
    EXPECT_EQ(view.block_count(), 1U);
    EXPECT_EQ(view.semantic_child_count(), 1U);
    // assert: the block carries the signal and its THD
    const std::unique_ptr<albor::SemanticProvider> block = view.semantic_child(0);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->semantic_node().explicit_id(), "fourier_block_0");
    EXPECT_EQ(block->semantic_node().name(), "V(1)  -  THD: 43.57 %");
    EXPECT_EQ(block->semantic_child_count(), 3U);
    // assert: the settings strip names what the analysis did
    const std::unique_ptr<albor::SemanticProvider> settings = block->semantic_child(0);
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->semantic_node().explicit_id(), "fourier_block_0_settings");
    EXPECT_EQ(settings->semantic_node().name(), "No. Harmonics: 2  -  Gridsize: 200  -  Cubic Spline");
    // assert: every harmonic row is addressable and names its cells
    const std::unique_ptr<albor::SemanticProvider> row = block->semantic_child(2);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->semantic_node().explicit_id(), "fourier_block_0_row_1");
    EXPECT_EQ(row->semantic_node().name(), "Harmonic 1, 1.000000e+03 Hz, 5.000000e+00, 0.000000e+00 deg, normalized 1.000000e+00, 0.000000e+00 deg");
    EXPECT_EQ(row->semantic_child_count(), 0U);
}

TEST(StudioFourierResultsViewChecks, a_stepped_block_names_its_step) {
    // arrange: a table over one block of a stepped file
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    view.set_tables({{.signal = "V(2)", .step = "Step 2", .thd = "2 %", .rows = {}}});
    // assert: the heading names the step beside the signal and the THD
    const std::unique_ptr<albor::SemanticProvider> block = view.semantic_child(0);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->semantic_node().name(), "V(2)  -  Step 2  -  THD: 2 %");
    // assert: a block without settings still carries its settings strip, which reads empty
    EXPECT_EQ(block->semantic_child_count(), 1U);
    const std::unique_ptr<albor::SemanticProvider> settings = block->semantic_child(0);
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->semantic_node().name(), "");
}

TEST(StudioFourierResultsViewChecks, the_wheel_scrolls_the_body_and_stops_at_the_end) {
    // arrange: a block long enough to overflow a short table
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    std::vector<fourier::ResultRow> rows;
    // one row per harmonic
    for (int harmonic = 0; harmonic < 40; ++harmonic)
        rows.push_back({.harmonic = harmonic, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"});
    // the table lists the long block
    view.set_tables({{.signal = "V(1)", .step = "", .thd = "1 %", .rows = std::move(rows)}});
    // the table fills a short page
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 160.0f}));
    // assert: the content overflows the page
    ASSERT_GT(view.maximum_offset(), 0.0f);
    // act: turn the wheel two notches toward the end
    albor::Event wheel;
    wheel.kind = albor::EventKind::Wheel;
    wheel.position = albor::Point{.x = 40.0f, .y = 90.0f};
    wheel.scroll = -2.0f;
    wheel.precision = albor::ScrollPrecision::Line;
    view.dispatch_event(wheel);
    // assert: the body scrolled by two theme lines and consumed the turn
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(view.offset(), 80.0f);
    // act: scroll far past the last row
    EXPECT_TRUE(view.scroll_to(100000.0f));
    // assert: the offset stops at the end of the content
    EXPECT_FLOAT_EQ(view.offset(), view.maximum_offset());
    // assert: a scroll that changes nothing reports no move
    EXPECT_FALSE(view.scroll_to(view.maximum_offset()));
}

TEST(StudioFourierResultsViewChecks, a_table_that_fits_consumes_no_wheel) {
    // arrange: a table with one short block
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    view.set_tables({{.signal = "V(1)", .step = "", .thd = "1 %", .rows = {{.harmonic = 0, .frequency = "0.000000e+00", .magnitude = "1.000000e+00", .phase = "0.000000e+00"}}}});
    // the table fills a tall page
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 400.0f}));
    // assert: the content fits and the offset never moves
    EXPECT_FLOAT_EQ(view.maximum_offset(), 0.0f);
    // act: turn the wheel over the table
    albor::Event wheel;
    wheel.kind = albor::EventKind::Wheel;
    wheel.position = albor::Point{.x = 40.0f, .y = 90.0f};
    wheel.scroll = -1.0f;
    view.dispatch_event(wheel);
    // assert: a table that fits leaves the turn for its host
    EXPECT_FALSE(wheel.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(StudioFourierResultsViewChecks, replacing_the_blocks_returns_to_the_first_row) {
    // arrange: a long block scrolled to its end
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    std::vector<fourier::ResultRow> rows;
    // one row per harmonic
    for (int harmonic = 0; harmonic < 40; ++harmonic)
        rows.push_back({.harmonic = harmonic, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"});
    // the table lists the long block
    view.set_tables({{.signal = "V(1)", .step = "", .thd = "1 %", .rows = std::move(rows)}});
    // the table fills a short page
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 160.0f}));
    // act: scroll to the end and swap the dataset
    EXPECT_TRUE(view.scroll_to(view.maximum_offset()));
    view.set_tables({{.signal = "V(2)", .step = "", .thd = "2 %", .rows = {}}});
    // assert: the new dataset opens at its first row
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
    EXPECT_EQ(view.block_count(), 1U);
}

TEST(StudioFourierResultsViewChecks, every_block_and_row_carries_a_unique_id) {
    // arrange: a table over two blocks of one row each
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    view.set_tables({{.signal = "V(1)", .rows = {{.harmonic = 0, .frequency = "0.000000e+00", .magnitude = "1.000000e+00", .phase = "0.000000e+00"}}}, {.signal = "V(2)", .rows = {{.harmonic = 0, .frequency = "0.000000e+00", .magnitude = "2.000000e+00", .phase = "0.000000e+00"}}}});
    // act: walk the announced blocks and their children
    const std::unique_ptr<albor::SemanticProvider> first = view.semantic_child(0);
    const std::unique_ptr<albor::SemanticProvider> second = view.semantic_child(1);
    // assert: both blocks exist and the table ends after them
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(view.semantic_child(2), nullptr);
    // assert: the ids keep the two blocks and their rows apart
    EXPECT_EQ(first->semantic_node().explicit_id(), "fourier_block_0");
    EXPECT_EQ(second->semantic_node().explicit_id(), "fourier_block_1");
    const std::unique_ptr<albor::SemanticProvider> first_settings = first->semantic_child(0);
    const std::unique_ptr<albor::SemanticProvider> first_row = first->semantic_child(1);
    const std::unique_ptr<albor::SemanticProvider> second_row = second->semantic_child(1);
    ASSERT_NE(first_row, nullptr);
    ASSERT_NE(second_row, nullptr);
    EXPECT_EQ(first_settings->semantic_node().explicit_id(), "fourier_block_0_settings");
    EXPECT_EQ(first_row->semantic_node().explicit_id(), "fourier_block_0_row_0");
    EXPECT_EQ(second_row->semantic_node().explicit_id(), "fourier_block_1_row_0");
    EXPECT_EQ(first->semantic_child(2), nullptr);
}

TEST(StudioFourierResultsViewChecks, the_content_height_stacks_the_blocks_and_their_gap) {
    // arrange: one block of ten rows in a short page
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    std::vector<fourier::ResultRow> rows;
    // one row per harmonic
    for (int harmonic = 0; harmonic < 10; ++harmonic)
        rows.push_back({.harmonic = harmonic, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"});
    // the table lists the block
    view.set_tables({{.signal = "V(1)", .rows = std::move(rows)}});
    // act: arrange a 600 by 200 page
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 200.0f}));
    // assert: the band, the caption row, the rows and the bottom inset scroll above a 43 pixel header
    EXPECT_FLOAT_EQ(view.maximum_offset(), 48.0f + 26.0f + 24.0f * 10.0f + 14.0f - (200.0f - 43.0f));
}

TEST(StudioFourierResultsViewChecks, the_paging_keys_scroll_a_focused_table) {
    // arrange: a long block in a short page
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    std::vector<fourier::ResultRow> rows;
    // one row per harmonic
    for (int harmonic = 0; harmonic < 40; ++harmonic)
        rows.push_back({.harmonic = harmonic, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"});
    // the table lists the block
    view.set_tables({{.signal = "V(1)", .rows = std::move(rows)}});
    // the table fills a short page and holds the focus
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 160.0f}));
    view.set_focused(true);
    // act: page down
    albor::Event page_down;
    page_down.kind = albor::EventKind::KeyDown;
    page_down.key = albor::Key::PageDown;
    view.dispatch_event(page_down);
    // assert: the body moved most of a page and consumed the key
    EXPECT_TRUE(page_down.handled);
    EXPECT_NEAR(view.offset(), 0.9f * (160.0f - 43.0f), 0.01f);
    // act: jump to the end and back to the start
    albor::Event end;
    end.kind = albor::EventKind::KeyDown;
    end.key = albor::Key::End;
    view.dispatch_event(end);
    // assert: the end key reached the last row
    EXPECT_TRUE(end.handled);
    EXPECT_FLOAT_EQ(view.offset(), view.maximum_offset());
    albor::Event home;
    home.kind = albor::EventKind::KeyDown;
    home.key = albor::Key::Home;
    view.dispatch_event(home);
    // assert: the home key returned to the first row
    EXPECT_TRUE(home.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(StudioFourierResultsViewChecks, a_modified_key_is_left_to_the_application) {
    // arrange: a long block in a short page that holds the focus
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    std::vector<fourier::ResultRow> rows;
    // one row per harmonic
    for (int harmonic = 0; harmonic < 40; ++harmonic)
        rows.push_back({.harmonic = harmonic, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"});
    // the table lists the block
    view.set_tables({{.signal = "V(1)", .rows = std::move(rows)}});
    // the table fills a short page and holds the focus
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 160.0f}));
    view.set_focused(true);
    // act: press shift and end together
    albor::Event shift_end;
    shift_end.kind = albor::EventKind::KeyDown;
    shift_end.key = albor::Key::End;
    shift_end.modifiers.shift = true;
    view.dispatch_event(shift_end);
    // assert: a modified key belongs to the application, so the body stays put
    EXPECT_FALSE(shift_end.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(StudioFourierResultsViewChecks, a_press_on_a_row_never_grabs_the_scrollbar) {
    // arrange: a long block in a short page, whose thumb sits at the top right
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    std::vector<fourier::ResultRow> rows;
    // one row per harmonic
    for (int harmonic = 0; harmonic < 40; ++harmonic)
        rows.push_back({.harmonic = harmonic, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"});
    // the table lists the block
    view.set_tables({{.signal = "V(1)", .rows = std::move(rows)}});
    // the table fills a short page
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 160.0f}));
    // act: press a row at the height the thumb spans, then move down
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.button = albor::PointerButton::Left;
    press.position = albor::Point{.x = 40.0f, .y = 50.0f};
    view.dispatch_event(press);
    albor::Event move = press;
    move.kind = albor::EventKind::PointerMove;
    move.position = albor::Point{.x = 40.0f, .y = 90.0f};
    view.dispatch_event(move);
    // assert: the press claimed the table and the move left the body where it was
    EXPECT_TRUE(press.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(StudioFourierResultsViewChecks, dragging_the_thumb_reaches_the_last_row) {
    // arrange: a long block in a short page
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    std::vector<fourier::ResultRow> rows;
    // one row per harmonic
    for (int harmonic = 0; harmonic < 40; ++harmonic)
        rows.push_back({.harmonic = harmonic, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"});
    // the table lists the block
    view.set_tables({{.signal = "V(1)", .rows = std::move(rows)}});
    // the table fills a short page
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 160.0f}));
    // act: grab the thumb at the top right and drag it to the track bottom
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.button = albor::PointerButton::Left;
    press.position = albor::Point{.x = 593.0f, .y = 50.0f};
    view.dispatch_event(press);
    albor::Event drag = press;
    drag.kind = albor::EventKind::PointerMove;
    drag.position = albor::Point{.x = 593.0f, .y = 400.0f};
    view.dispatch_event(drag);
    // assert: the track maps onto the whole content
    EXPECT_TRUE(press.handled);
    EXPECT_FLOAT_EQ(view.offset(), view.maximum_offset());
    // act: release the drag and move again
    albor::Event release = drag;
    release.kind = albor::EventKind::PointerUp;
    view.dispatch_event(release);
    albor::Event after = drag;
    after.position = albor::Point{.x = 593.0f, .y = 120.0f};
    view.dispatch_event(after);
    // assert: a released thumb stops following the pointer
    EXPECT_FLOAT_EQ(view.offset(), view.maximum_offset());
}

TEST(StudioFourierResultsViewChecks, a_press_on_the_track_pages_toward_the_pointer) {
    // arrange: a long block in a short page
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    std::vector<fourier::ResultRow> rows;
    // one row per harmonic
    for (int harmonic = 0; harmonic < 40; ++harmonic)
        rows.push_back({.harmonic = harmonic, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"});
    // the table lists the block
    view.set_tables({{.signal = "V(1)", .rows = std::move(rows)}});
    // the table fills a short page
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 160.0f}));
    // act: press the track below the thumb
    albor::Event press;
    press.kind = albor::EventKind::PointerDown;
    press.button = albor::PointerButton::Left;
    press.position = albor::Point{.x = 593.0f, .y = 140.0f};
    view.dispatch_event(press);
    // assert: the body paged forward by most of a screen
    EXPECT_TRUE(press.handled);
    EXPECT_NEAR(view.offset(), 0.9f * (160.0f - 43.0f), 0.01f);
}

TEST(StudioFourierResultsViewChecks, a_repaint_reuses_the_picture_until_the_content_changes) {
    // arrange: a table with settings in a fixed page
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    view.set_title("FOUR: 1000 Hz");
    view.set_tables({{.signal = "V(1)", .thd = "1 %", .gridsize = "200", .rows = {{.harmonic = 0, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"}}}});
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 400.0f}));
    // act: paint the table once
    albor::PictureRecorder first;
    view.paint_content(first);
    // assert: the first paint records the picture
    EXPECT_EQ(view.record_count(), 1U);
    // act: paint again with no state change
    albor::PictureRecorder second;
    view.paint_content(second);
    // assert: the retained picture is replayed
    EXPECT_EQ(view.record_count(), 1U);
    // act: only the recorded grid size changes
    view.set_tables({{.signal = "V(1)", .thd = "1 %", .gridsize = "400", .rows = {{.harmonic = 0, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"}}}});
    albor::PictureRecorder third;
    view.paint_content(third);
    // assert: the settings strip re-records, so the new value shows
    EXPECT_EQ(view.record_count(), 2U);
}

TEST(StudioFourierResultsViewChecks, shrinking_the_panel_clamps_the_scrolled_table) {
    // arrange: a long block scrolled to its end in a short page
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    std::vector<fourier::ResultRow> rows;
    // one row per harmonic
    for (int harmonic = 0; harmonic < 40; ++harmonic)
        rows.push_back({.harmonic = harmonic, .frequency = "1.000000e+03", .magnitude = "5.000000e+00", .phase = "0.000000e+00"});
    // the table lists the block
    view.set_tables({{.signal = "V(1)", .rows = std::move(rows)}});
    // the table fills a short page
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 160.0f}));
    // the reader reached the last row
    EXPECT_TRUE(view.scroll_to(view.maximum_offset()));
    // act: the panel grows
    view.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 400.0f}));
    // assert: the offset follows the shorter content the taller page allows
    EXPECT_FLOAT_EQ(view.offset(), view.maximum_offset());
}

TEST(StudioFourierResultsViewChecks, the_table_fills_the_offered_page) {
    // arrange: a table with no arranged extent yet
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::FourierResultsView view(&engine, &text);
    // act: measure under an unbounded parent
    albor::Size unbounded_size;
    view.measure(albor::SizeConstraints::unconstrained(), unbounded_size);
    // assert: an unbounded parent gets the useful default panel
    EXPECT_FLOAT_EQ(unbounded_size.width, 640.0f);
    EXPECT_FLOAT_EQ(unbounded_size.height, 480.0f);
    // act: measure under a bounded parent
    albor::Size bounded_size;
    view.measure(albor::SizeConstraints::at_most(albor::Size{.width = 900.0f, .height = 700.0f}), bounded_size);
    // assert: a bounded page is what the table fills
    EXPECT_FLOAT_EQ(bounded_size.width, 900.0f);
    EXPECT_FLOAT_EQ(bounded_size.height, 700.0f);
}

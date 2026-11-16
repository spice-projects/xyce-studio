#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include <gtest/gtest.h>

#include "albor/core/paint.h"
#include "albor/render/shaped_run.h"
#include "albor/runtime/binding_engine.h"
#include "albor/text/text_engine.h"
#include "views/charts/add_plot_widgets.h"

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
            // ignore the ceiling, because chip labels are single line
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

TEST(StudioExpressionChipChecks, a_cell_narrower_than_the_ellipsis_elides_a_multibyte_label) {
    // arrange: a chip whose label starts with a two-byte character and no room for the ellipsis
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ExpressionChip chip(&engine, &text, "\u00B5V(out)", "V(out)", albor::Color::rgba(0.4f, 0.6f, 0.9f, 1.0f), true, "chip");
    // act: arrange the chip into a cell that leaves less room than the ellipsis needs
    chip.arrange(albor::Rect::from_origin_size(albor::Point{}, albor::Size{.width = 55.0f, .height = 32.0f}));
    // assert: the chip keeps its cell and the fit terminates
    EXPECT_FLOAT_EQ(chip.bounds().width, 55.0f);
}

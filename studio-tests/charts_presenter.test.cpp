#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "albor/render/shaped_run.h"
#include "albor/runtime/binding_engine.h"
#include "albor/text/text_engine.h"
#include "charts/fft_selection.h"
#include "presenters/charts_presenter.h"
#include "views/charts/chart_view.h"

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
            // ignore the ceiling, because chart labels are single line
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

TEST(StudioChartsPresenterChecks, an_empty_presenter_reports_no_dataset) {
    // arrange: a presenter over a chart view with no dataset
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::ChartsPresenter presenter(view, text);
    // assert: the empty presenter reports nothing to select or close
    EXPECT_FALSE(presenter.has_dataset());
    EXPECT_EQ(presenter.dataset_count(), 0U);
    EXPECT_EQ(presenter.active_dataset(), 0U);
    EXPECT_EQ(presenter.active_dataset_id(), -1);
    EXPECT_EQ(presenter.chart_count(), 0U);
}

TEST(StudioChartsPresenterChecks, dataset_accessors_are_empty_past_the_last_tab) {
    // arrange: a presenter with no dataset
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::ChartsPresenter presenter(view, text);
    // assert: a tab past the set has no title and no close affordance
    EXPECT_TRUE(presenter.dataset_title(0).empty());
    EXPECT_TRUE(presenter.dataset_title(7).empty());
    EXPECT_FALSE(presenter.dataset_closable(0));
    EXPECT_FALSE(presenter.dataset_closable(7));
    EXPECT_TRUE(presenter.title().empty());
}

TEST(StudioChartsPresenterChecks, closing_a_missing_dataset_does_nothing) {
    // arrange: a presenter with no dataset
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::ChartsPresenter presenter(view, text);
    // act: close a tab that does not exist
    presenter.close_dataset(0);
    // assert: the empty state is untouched
    EXPECT_FALSE(presenter.has_dataset());
    EXPECT_EQ(presenter.dataset_count(), 0U);
    EXPECT_EQ(presenter.active_dataset(), 0U);
}

TEST(StudioChartsPresenterChecks, activating_a_missing_dataset_does_nothing) {
    // arrange: a presenter with no dataset
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::ChartsPresenter presenter(view, text);
    // act: select a tab that does not exist
    presenter.activate_dataset(3);
    // assert: the empty state is untouched
    EXPECT_FALSE(presenter.has_dataset());
    EXPECT_EQ(presenter.active_dataset(), 0U);
    EXPECT_EQ(presenter.active_dataset_id(), -1);
}

TEST(StudioChartsPresenterChecks, closing_every_dataset_releases_the_tabs) {
    // arrange: a parsed csv dataset
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-close-all";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "run.csv";
    {
        std::ofstream file(dataset);
        file << "TIME,V(1)\n0,0\n1,1\n2,2\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::ChartsPresenter presenter(view, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    ASSERT_EQ(presenter.dataset_count(), 1U);
    int changed = 0;
    presenter.set_on_tabs_changed([&] { changed++; });
    // act: a new netlist document closes every output tab
    presenter.close_all_datasets();
    // assert: the dataset, its identity and its charts are gone
    EXPECT_EQ(presenter.dataset_count(), 0U);
    EXPECT_FALSE(presenter.has_dataset());
    EXPECT_EQ(presenter.active_dataset_id(), -1);
    EXPECT_EQ(presenter.chart_count(), 0U);
    EXPECT_GE(changed, 1);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, the_fft_selection_offers_the_chart_series_and_apply_appends_a_dataset) {
    // arrange: a transient csv with one real signal
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-fft";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "tran-fft.csv";
    {
        std::ofstream file(dataset);
        file << "TIME,V(1)\n";
        for (int index = 0; index < 16; ++index)
            file << index * 0.001 << "," << (index % 2 == 0 ? 1.0 : 0.0) << "\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::ChartsPresenter presenter(view, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    // act: read the transform the dialog would open with
    const std::optional<studio::FftSelectionState> selection = presenter.fft_selection(0);
    // assert: the chart's real series is offered over the dataset range
    ASSERT_TRUE(selection.has_value());
    ASSERT_EQ(selection->expressions.size(), 1U);
    EXPECT_EQ(selection->expressions[0].name, "V(1)");
    EXPECT_DOUBLE_EQ(selection->all_from, 0.0);
    EXPECT_DOUBLE_EQ(selection->all_to, 0.015);
    // act: apply a small rectangular transform
    studio::FftRequest request;
    request.expressions = {"V(1)"};
    request.from = 0.0;
    request.to = 0.015;
    request.points = 8;
    request.window = fft::WindowFunction::RECTANGULAR;
    presenter.apply_fft(request);
    // assert: the transform joins the tab set as its own dataset
    ASSERT_EQ(presenter.dataset_count(), 2U);
    EXPECT_NE(presenter.dataset_title(1).rfind("FFT: Rectangular", 0), std::string::npos);
    EXPECT_EQ(presenter.active_dataset(), 1U);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

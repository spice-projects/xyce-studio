#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "albor/render/shaped_run.h"
#include "albor/runtime/binding_engine.h"
#include "albor/text/text_engine.h"
#include "presenters/charts_presenter.h"
#include "views/charts/charts_view.h"

namespace
{
    // the text engine double shapes every chart label into a fixed run
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

TEST(StudioChartsPresenterChecks, a_csv_output_file_loads_into_one_chart_scene) {
    // arrange: a clean scratch root and a minimal csv output file
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path csv = root / "tran.csv";
    {
        // write the header and two rows the parser reads as one step
        std::ofstream file(csv);
        file << "TIME,V(1)\n0,0\n1,1\n2,2\n";
    }
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    // act: load the output file
    const bool loaded = presenter.show_file(csv);
    // assert: the dataset and its retained scene are published
    EXPECT_TRUE(loaded);
    EXPECT_TRUE(presenter.has_dataset());
    EXPECT_EQ(presenter.chart_count(), 1U);
    ASSERT_NE(presenter.scene(), nullptr);
    ASSERT_NE(presenter.scene()->base, nullptr);
    ASSERT_EQ(presenter.scene()->slots.size(), 1U);
    EXPECT_EQ(presenter.scene()->slots[0].kind, ChartKind::XY);
    // act: resize the panel
    presenter.set_viewport(400.0f, 300.0f);
    // assert: the scene follows the new viewport
    ASSERT_NE(presenter.scene(), nullptr);
    EXPECT_FLOAT_EQ(presenter.scene()->width, 400.0f);
    EXPECT_FLOAT_EQ(presenter.scene()->height, 300.0f);
}

TEST(StudioChartsPresenterChecks, a_y_parameter_touchstone_plots_its_diagonal_entries) {
    // arrange: a clean scratch root and a two-port y parameter file
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-y";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path touchstone = root / "lin-y.s2p";
    {
        // write a minimal y parameter network with two frequencies
        std::ofstream file(touchstone);
        file << "[Version] 2.0\n";
        file << "# Hz Y RI R 50\n";
        file << "[Number of Ports] 2\n";
        file << "[Two-Port Data Order] 12_21\n";
        file << "[Number of Frequencies] 2\n";
        file << "[Reference] 50 50\n";
        file << "[Network Data]\n";
        file << "1e8 0.20 0.00 0.80 0.00 0.80 0.00 -0.20 0.00\n";
        file << "2e8 0.10 0.10 0.70 -0.10 0.70 -0.10 -0.10 0.10\n";
    }
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    // act: load the y parameter file
    const bool loaded = presenter.show_file(touchstone);
    // assert: the diagonal y entries reach the smith chart
    EXPECT_TRUE(loaded);
    EXPECT_EQ(presenter.chart_count(), 1U);
    EXPECT_GT(presenter.series_count(0), 0U);
}

TEST(StudioChartsPresenterChecks, a_null_dataset_clears_the_previous_charts) {
    // arrange: a loaded csv dataset
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-null";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path csv = root / "tran.csv";
    {
        // write the header and two rows the parser reads as one step
        std::ofstream file(csv);
        file << "TIME,V(1)\n0,0\n1,1\n2,2\n";
    }
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    ASSERT_TRUE(presenter.show_file(csv));
    ASSERT_NE(presenter.scene(), nullptr);
    // act: drop the dataset
    presenter.load_dataset(nullptr);
    // assert: the panel is empty and no engine keeps the old expressions
    EXPECT_FALSE(presenter.has_dataset());
    EXPECT_EQ(presenter.chart_count(), 0U);
    EXPECT_EQ(presenter.scene(), nullptr);
}

TEST(StudioChartsPresenterChecks, a_dark_scheme_republishes_the_dark_palette) {
    // arrange: a loaded csv dataset in the light scheme
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-dark";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path csv = root / "tran.csv";
    {
        // write the header and two rows the parser reads as one step
        std::ofstream file(csv);
        file << "TIME,V(1)\n0,0\n1,1\n2,2\n";
    }
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    ASSERT_TRUE(presenter.show_file(csv));
    ASSERT_NE(presenter.scene(), nullptr);
    // act: switch the chart palette to the dark scheme
    presenter.set_dark_mode(true);
    // assert: the republished scene carries the dark text colour
    ASSERT_NE(presenter.scene(), nullptr);
    EXPECT_NEAR(presenter.scene()->palette.text.r, CHART_PALETTE_DARK.text.r, 0.001f);
    EXPECT_NEAR(presenter.scene()->palette.panel.r, CHART_PALETTE_DARK.panel.r, 0.001f);
}

TEST(StudioChartsPresenterChecks, destroying_the_view_before_the_presenter_is_safe) {
    // arrange: a view and the presenter that borrows it
    albor::BindingEngine engine;
    FakeTextEngine text;
    auto view = std::make_unique<studio::ChartsView>(&engine, nullptr);
    auto presenter = std::make_unique<studio::ChartsPresenter>(*view, text);
    // act: the window tree frees the view first, then the presenter
    view.reset();
    // assert: the presenter destructor no longer touches the destroyed view
    EXPECT_NO_THROW(presenter.reset());
}

TEST(StudioChartsPresenterChecks, a_detached_presenter_refuses_a_later_file) {
    // arrange: a clean scratch root and a minimal csv output file
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-detached";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path csv = root / "tran.csv";
    {
        // write the header and two rows the parser reads as one step
        std::ofstream file(csv);
        file << "TIME,V(1)\n0,0\n1,1\n2,2\n";
    }
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    ASSERT_TRUE(presenter.show_file(csv));
    // act: detach the presenter from the view the window teardown destroys
    presenter.detach();
    // assert: the presenter no longer accepts a dataset for the dropped view
    EXPECT_FALSE(presenter.show_file(csv));
    EXPECT_TRUE(presenter.has_dataset());
}

TEST(StudioChartsPresenterChecks, an_unsupported_file_is_refused) {
    // arrange: a clean scratch root and a text file the charts do not read
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-unsupported";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path text_file = root / "notes.txt";
    {
        // write a file the parser dispatch does not recognize
        std::ofstream file(text_file);
        file << "not an output file\n";
    }
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    // act: offer the unsupported file
    const bool loaded = presenter.show_file(text_file);
    // assert: nothing was adopted
    EXPECT_FALSE(loaded);
    EXPECT_FALSE(presenter.has_dataset());
    EXPECT_EQ(presenter.chart_count(), 0U);
    EXPECT_EQ(presenter.scene(), nullptr);
}

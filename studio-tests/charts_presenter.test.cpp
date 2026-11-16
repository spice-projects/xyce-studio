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
#include "charts/chart_engine.h"
#include "charts/fft_selection.h"
#include "io/xyce_output_file.h"
#include "presenters/charts_presenter.h"
#include "presenters/run_output.h"
#include "views/charts/chart_view.h"
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
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
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
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
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
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
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
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
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
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
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
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
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

TEST(StudioChartsPresenterChecks, a_fourier_output_opens_as_its_results_table) {
    // arrange: a Fourier analysis file carrying one signal and two harmonics
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-fourier";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "run.four0";
    {
        std::ofstream file(dataset);
        file << "Fourier analysis for V(1):\n";
        file << "  No. Harmonics: 2, THD: 4.356800e+01, Gridsize: 200, Interpolation Type: Cubic Spline\n";
        file << "    Harmonic       Frequency       Magnitude           Phase       Norm. Mag     Norm. Phase\n";
        file << "           0    0.000000e+00    3.125000e-01    0.000000e+00    0.000000e+00    0.000000e+00\n";
        file << "           1    1.000000e+03    5.000000e+00    0.000000e+00    1.000000e+00    0.000000e+00\n";
        file << "\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    // act: open the Fourier analysis output
    ASSERT_TRUE(presenter.show_file(dataset));
    // assert: the dataset is active and presented as a table, which builds no chart
    ASSERT_EQ(presenter.dataset_count(), 1U);
    EXPECT_EQ(presenter.active_dataset(), 0U);
    EXPECT_TRUE(presenter.fourier_active());
    EXPECT_EQ(presenter.chart_count(), 0U);
    EXPECT_EQ(presenter.dataset_title(0), "FOUR: 1000 Hz");
    // assert: the table lists the blocks the file carries
    EXPECT_EQ(results.block_count(), 1U);
    // act: close the dataset
    presenter.close_dataset(0);
    // assert: the table surface is no longer active and holds no block
    EXPECT_FALSE(presenter.fourier_active());
    EXPECT_EQ(results.block_count(), 0U);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, a_scheme_flip_keeps_the_table_scrolled) {
    // arrange: a Fourier analysis output whose table is scrolled down
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-fourier-scheme";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "run.four0";
    {
        std::ofstream file(dataset);
        file << "Fourier analysis for V(1):\n";
        file << "  No. Harmonics: 6, THD: 4.356800e+01, Gridsize: 200, Interpolation Type: Cubic Spline\n";
        file << "    Harmonic       Frequency       Magnitude           Phase       Norm. Mag     Norm. Phase\n";
        for (int harmonic = 0; harmonic < 6; ++harmonic)
            file << "           " << harmonic << "    " << harmonic << ".000000e+03    5.000000e+00    0.000000e+00    1.000000e+00    0.000000e+00\n";
        file << "\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    // the table fills a short page the test arranges
    results.arrange(albor::Rect::from_origin_size(albor::Point{.x = 0.0f, .y = 0.0f}, albor::Size{.width = 600.0f, .height = 160.0f}));
    ASSERT_GT(results.maximum_offset(), 0.0f);
    ASSERT_TRUE(results.scroll_to(30.0f));
    // act: the scheme flips to dark while the table is shown
    presenter.set_dark_mode(true);
    // assert: the repaint keeps the reader where they were
    EXPECT_FLOAT_EQ(results.offset(), 30.0f);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, switching_tabs_swaps_the_table_and_the_charts) {
    // arrange: a Fourier analysis output and a transient csv in one tab set
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-fourier-switch";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path four_dataset = root / "run.four0";
    {
        std::ofstream file(four_dataset);
        file << "Fourier analysis for V(1):\n";
        file << "  No. Harmonics: 2, THD: 1.000000e+00, Gridsize: 200, Interpolation Type: Cubic Spline\n";
        file << "    Harmonic       Frequency       Magnitude           Phase       Norm. Mag     Norm. Phase\n";
        file << "           0    0.000000e+00    3.125000e-01    0.000000e+00    0.000000e+00    0.000000e+00\n";
        file << "           1    1.000000e+03    5.000000e+00    0.000000e+00    1.000000e+00    0.000000e+00\n";
        file << "\n";
    }
    const std::filesystem::path chart_dataset = root / "tran.csv";
    {
        std::ofstream file(chart_dataset);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    ASSERT_TRUE(presenter.show_file(four_dataset));
    ASSERT_TRUE(presenter.show_file(chart_dataset));
    // assert: the chart dataset of the second tab owns the canvas
    EXPECT_FALSE(presenter.fourier_active());
    EXPECT_EQ(presenter.chart_count(), 1U);
    // act: select the Fourier tab again
    presenter.activate_dataset(0);
    // assert: the table shows the blocks of the Fourier dataset again
    EXPECT_TRUE(presenter.fourier_active());
    EXPECT_EQ(results.block_count(), 1U);
    // act: select the chart tab again
    presenter.activate_dataset(1);
    // assert: the charts describe the chart dataset
    EXPECT_FALSE(presenter.fourier_active());
    EXPECT_EQ(presenter.chart_count(), 1U);
    // release the scratch files
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, discarding_run_companions_keeps_the_active_user_tab) {
    // arrange: a run that produced a primary and a Fourier companion, plus two user datasets
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-run-roles";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    {
        std::ofstream primary(root / "run.cir.csv");
        primary << "TIME,V(1)\n0,0\n1,1\n";
    }
    {
        std::ofstream four(root / "run.cir.four0");
        four << "Fourier analysis for V(1):\n";
        four << "  No. Harmonics: 2, THD: 1.000000e+00, Gridsize: 200, Interpolation Type: Cubic Spline\n";
        four << "    Harmonic       Frequency       Magnitude           Phase       Norm. Mag     Norm. Phase\n";
        four << "           0    0.000000e+00    3.125000e-01    0.000000e+00    0.000000e+00    0.000000e+00\n";
        four << "           1    1.000000e+03    5.000000e+00    0.000000e+00    1.000000e+00    0.000000e+00\n";
        four << "\n";
    }
    // the two user files the shell opened by hand
    const std::filesystem::path user_a = root / "user-a.csv";
    const std::filesystem::path user_b = root / "user-b.csv";
    {
        std::ofstream file(user_a);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    {
        std::ofstream file(user_b);
        file << "TIME,V(1)\n0,0\n1,2\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    // the run configuration that produced both files
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".TRAN 0 10m 1m", ".PRINT TRAN FORMAT=CSV V(1)", ".FOUR 1k V(1)"});
    output.netlist_path = root / "run.cir";
    output.working_directory = root;
    output.title = "run";
    const studio::RunOutputResult shown = presenter.show_run_output(output, nullptr);
    ASSERT_EQ(shown.outcome, studio::RunOutputOutcome::Primary);
    ASSERT_EQ(presenter.dataset_count(), 2U);
    ASSERT_TRUE(presenter.show_file(user_a));
    ASSERT_TRUE(presenter.show_file(user_b));
    ASSERT_EQ(presenter.dataset_count(), 4U);
    // the first user file is the tab the reader is looking at
    presenter.activate_dataset(2);
    const std::string active_title = presenter.dataset_title(presenter.active_dataset());
    // act: the next run discards the previous run's companion
    presenter.discard_run_companions();
    // assert: the erase sat before the active tab, which keeps the selection it had
    ASSERT_EQ(presenter.dataset_count(), 3U);
    EXPECT_EQ(presenter.active_dataset(), 1U);
    EXPECT_EQ(presenter.dataset_title(presenter.active_dataset()), active_title);
    // release the scratch files
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, a_new_window_request_hands_the_dataset_to_the_shell) {
    // arrange: a parsed csv dataset
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-new-window";
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
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    // the shell records the dataset it is asked to open in a window
    std::shared_ptr<XyceOutputFile> handed;
    ChartKind handed_kind = ChartKind::SMITH;
    std::string handed_title;
    int requests = 0;
    presenter.set_on_new_window([&](std::shared_ptr<XyceOutputFile> file, const ChartKind kind, std::string title) {
        handed = std::move(file);
        handed_kind = kind;
        handed_title = std::move(title);
        requests++;
    });
    // act: run the command on the chart the menu targets
    presenter.run_chart_command(studio::ChartCommand::NewWindow, 0);
    // assert: the shell receives the dataset that was already parsed, with the kind and title of the tab
    ASSERT_NE(handed, nullptr);
    EXPECT_EQ(handed->filename(), dataset);
    EXPECT_EQ(handed_kind, ChartKind::XY);
    EXPECT_EQ(handed_title, presenter.dataset_title(0));
    EXPECT_EQ(requests, 1);
    // assert: the window that asked keeps its own tab
    EXPECT_EQ(presenter.dataset_count(), 1U);
    // act: run the command on a chart that does not exist
    presenter.run_chart_command(studio::ChartCommand::NewWindow, 7);
    // assert: a missing chart opens nothing
    EXPECT_EQ(requests, 1);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, an_adopted_dataset_opens_in_a_second_window) {
    // arrange: a csv dataset the first shell parsed
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-adopt";
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
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    std::shared_ptr<XyceOutputFile> handed;
    ChartKind handed_kind = ChartKind::SMITH;
    std::string handed_title;
    presenter.set_on_new_window([&](std::shared_ptr<XyceOutputFile> file, const ChartKind kind, std::string title) {
        handed = std::move(file);
        handed_kind = kind;
        handed_title = std::move(title);
    });
    presenter.run_chart_command(studio::ChartCommand::NewWindow, 0);
    ASSERT_NE(handed, nullptr);
    // the second shell over its own surfaces
    albor::BindingEngine second_engine;
    FakeTextEngine second_text;
    studio::ChartView second_view(&second_engine, &second_text);
    studio::FourierResultsView second_results(&second_engine, &second_text);
    studio::ChartsPresenter second(second_view, second_results, second_text);
    // act: the second shell adopts the dataset the first handed over
    ASSERT_TRUE(second.adopt_file(handed, handed_kind, handed_title));
    // assert: the adopted dataset is the tab the second window shows
    EXPECT_EQ(second.dataset_count(), 1U);
    EXPECT_TRUE(second.has_dataset());
    EXPECT_EQ(second.active_dataset(), 0U);
    EXPECT_TRUE(second.dataset_closable(0));
    EXPECT_EQ(second.dataset_title(0), presenter.dataset_title(0));
    // assert: both windows share the parsed file instead of reading it twice
    EXPECT_GE(handed.use_count(), 2L);
    // act: adopt a missing dataset
    EXPECT_FALSE(second.adopt_file(nullptr, ChartKind::XY, {}));
    // assert: a missing dataset adds no tab
    EXPECT_EQ(second.dataset_count(), 1U);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, an_adopted_touchstone_dataset_plots_on_the_smith_plane) {
    // arrange: a one frequency touchstone dataset
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-adopt-smith";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "lin.s2p";
    {
        std::ofstream file(dataset);
        file << "[Version] 2.0\n";
        file << "# Hz S RI R 50\n";
        file << "[Number of Ports] 2\n";
        file << "[Two-Port Data Order] 12_21\n";
        file << "[Number of Frequencies] 1\n";
        file << "[Reference] 50\n";
        file << "[Network Data]\n";
        file << "1.0e8 0.1 0.0 0.2 0.0 0.2 0.0 -0.1 0.0\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    std::shared_ptr<XyceOutputFile> handed;
    ChartKind handed_kind = ChartKind::XY;
    std::string handed_title;
    presenter.set_on_new_window([&](std::shared_ptr<XyceOutputFile> file, const ChartKind kind, std::string title) {
        handed = std::move(file);
        handed_kind = kind;
        handed_title = std::move(title);
    });
    presenter.run_chart_command(studio::ChartCommand::NewWindow, 0);
    ASSERT_NE(handed, nullptr);
    // the second shell over its own surfaces
    albor::BindingEngine second_engine;
    FakeTextEngine second_text;
    studio::ChartView second_view(&second_engine, &second_text);
    studio::FourierResultsView second_results(&second_engine, &second_text);
    studio::ChartsPresenter second(second_view, second_results, second_text);
    // act: the second shell adopts the touchstone dataset
    ASSERT_TRUE(second.adopt_file(handed, handed_kind, handed_title));
    // assert: the adopted dataset keeps the fixed smith plane of its format
    const std::optional<studio::PlotSelectionState> selection = second.plot_selection(0);
    ASSERT_TRUE(selection.has_value());
    EXPECT_TRUE(selection->smith);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, a_new_window_request_without_a_sink_is_ignored) {
    // arrange: a parsed csv dataset and a shell that installed no window sink
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-new-window-sink";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "run.csv";
    {
        std::ofstream file(dataset);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    // act: run the command without a shell to hand the dataset to
    presenter.run_chart_command(studio::ChartCommand::NewWindow, 0);
    // assert: the tab set and the scene are untouched
    EXPECT_EQ(presenter.dataset_count(), 1U);
    EXPECT_EQ(presenter.chart_count(), 1U);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, an_adopted_run_tab_keeps_the_title_it_was_handed) {
    // arrange: a parsed csv dataset and the title its source tab carries
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-adopt-title";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "run.csv";
    {
        std::ofstream file(dataset);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    std::shared_ptr<XyceOutputFile> handed;
    presenter.set_on_new_window([&](std::shared_ptr<XyceOutputFile> file, ChartKind, std::string) { handed = std::move(file); });
    presenter.run_chart_command(studio::ChartCommand::NewWindow, 0);
    ASSERT_NE(handed, nullptr);
    // the second shell over its own surfaces
    albor::BindingEngine second_engine;
    FakeTextEngine second_text;
    studio::ChartView second_view(&second_engine, &second_text);
    studio::FourierResultsView second_results(&second_engine, &second_text);
    studio::ChartsPresenter second(second_view, second_results, second_text);
    // act: the second shell adopts the dataset under the title the run gave its tab
    ASSERT_TRUE(second.adopt_file(handed, ChartKind::XY, "mysim.cir"));
    // assert: the spawned tab reads the run title, not the analysis title the parser carries
    EXPECT_EQ(second.dataset_title(0), "mysim.cir");
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, an_adopted_touchstone_dataset_on_the_line_plane_keeps_the_xy_kind) {
    // arrange: a touchstone dataset the shell offers on the line plane
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-adopt-line";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "lin.s2p";
    {
        std::ofstream file(dataset);
        file << "[Version] 2.0\n";
        file << "# Hz S RI R 50\n";
        file << "[Number of Ports] 2\n";
        file << "[Two-Port Data Order] 12_21\n";
        file << "[Number of Frequencies] 1\n";
        file << "[Reference] 50\n";
        file << "[Network Data]\n";
        file << "1.0e8 0.1 0.0 0.2 0.0 0.2 0.0 -0.1 0.0\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    std::shared_ptr<XyceOutputFile> handed;
    presenter.set_on_new_window([&](std::shared_ptr<XyceOutputFile> file, ChartKind, std::string) { handed = std::move(file); });
    presenter.run_chart_command(studio::ChartCommand::NewWindow, 0);
    ASSERT_NE(handed, nullptr);
    // the second shell over its own surfaces
    albor::BindingEngine second_engine;
    FakeTextEngine second_text;
    studio::ChartView second_view(&second_engine, &second_text);
    studio::FourierResultsView second_results(&second_engine, &second_text);
    studio::ChartsPresenter second(second_view, second_results, second_text);
    // act: the second shell adopts the file with the line kind its tab was showing
    ASSERT_TRUE(second.adopt_file(handed, ChartKind::XY, {}));
    // assert: the kind the caller hands over wins over the extension the file carries
    const std::optional<studio::PlotSelectionState> selection = second.plot_selection(0);
    ASSERT_TRUE(selection.has_value());
    EXPECT_FALSE(selection->smith);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioChartsPresenterChecks, adopting_a_dataset_after_a_detach_reports_failure) {
    // arrange: a parsed csv dataset and a presenter that already lost its surfaces
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "presenter-adopt-detached";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "run.csv";
    {
        std::ofstream file(dataset);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    albor::BindingEngine engine;
    FakeTextEngine text;
    studio::ChartView view(&engine, &text);
    studio::FourierResultsView results(&engine, &text);
    studio::ChartsPresenter presenter(view, results, text);
    ASSERT_TRUE(presenter.show_file(dataset));
    std::shared_ptr<XyceOutputFile> handed;
    presenter.set_on_new_window([&](std::shared_ptr<XyceOutputFile> file, ChartKind, std::string) { handed = std::move(file); });
    presenter.run_chart_command(studio::ChartCommand::NewWindow, 0);
    ASSERT_NE(handed, nullptr);
    // act: the shell detaches the presenter before its views die
    presenter.detach();
    // assert: a detached shell adopts nothing
    EXPECT_FALSE(presenter.adopt_file(handed, ChartKind::XY, {}));
    EXPECT_EQ(presenter.dataset_count(), 1U);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

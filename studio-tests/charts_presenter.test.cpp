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
#include "io/temp_output_directory.h"
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

TEST(StudioChartsPresenterChecks, two_datasets_open_as_tabs_and_keep_their_scenes) {
    // arrange: a clean scratch root and two minimal csv output files
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-tabs";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path first_csv = root / "tran-first.csv";
    const std::filesystem::path second_csv = root / "tran-second.csv";
    {
        // write the first table
        std::ofstream file(first_csv);
        file << "TIME,V(1)\n0,0\n1,1\n2,2\n";
    }
    {
        // write the second table
        std::ofstream file(second_csv);
        file << "TIME,V(1)\n0,2\n1,1\n2,0\n";
    }
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    // act: load both files
    ASSERT_TRUE(presenter.show_file(first_csv));
    ASSERT_TRUE(presenter.show_file(second_csv));
    // assert: both tabs exist and the second is active
    EXPECT_EQ(presenter.dataset_count(), 2U);
    EXPECT_EQ(presenter.active_dataset(), 1U);
    ASSERT_NE(presenter.scene(), nullptr);
    ASSERT_EQ(presenter.scene()->tabs.size(), 2U);
    EXPECT_FLOAT_EQ(presenter.scene()->tab_bar_height, studio::CHART_TAB_HEIGHT);
    EXPECT_FLOAT_EQ(presenter.scene()->content_y, studio::CHART_TAB_HEIGHT);
    EXPECT_FALSE(presenter.dataset_title(0).empty());
    EXPECT_FALSE(presenter.dataset_title(1).empty());
    // act: return to the first tab
    presenter.activate_dataset(0);
    // assert: the first tab is active and its scene survives the switch
    EXPECT_EQ(presenter.active_dataset(), 0U);
    ASSERT_NE(presenter.scene(), nullptr);
    EXPECT_EQ(presenter.scene()->tabs.size(), 2U);
    // act: close the first tab, then the remaining one
    presenter.close_dataset(0);
    // assert: the surviving tab becomes active
    EXPECT_EQ(presenter.dataset_count(), 1U);
    EXPECT_EQ(presenter.active_dataset(), 0U);
    ASSERT_NE(presenter.scene(), nullptr);
    ASSERT_EQ(presenter.scene()->tabs.size(), 1U);
    EXPECT_TRUE(presenter.scene()->tabs.front().closable);
    EXPECT_FLOAT_EQ(presenter.scene()->tab_bar_height, studio::CHART_TAB_HEIGHT);
    // act: close the last tab
    presenter.close_dataset(0);
    // assert: the panel is empty and the view has no scene
    EXPECT_EQ(presenter.dataset_count(), 0U);
    EXPECT_EQ(presenter.active_dataset(), 0U);
    EXPECT_EQ(presenter.scene(), nullptr);
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

TEST(StudioChartsPresenterChecks, the_analysis_print_file_wins_over_a_secondary_output) {
    // arrange: a run directory holding the configured primary and a secondary companion
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-run-primary";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path netlist = root / "netlist.cir";
    const std::filesystem::path primary = root / "netlist.cir.csv";
    const std::filesystem::path secondary = root / "netlist.cir.TD.csv";
    {
        // write the primary table
        std::ofstream file(primary);
        file << "TIME,V(1)\n0,0\n1,1\n2,2\n";
    }
    {
        // write the secondary table with other values
        std::ofstream file(secondary);
        file << "TIME,V(1)\n0,9\n1,8\n2,7\n";
    }
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 2u", ".PRINT TRAN FORMAT=CSV V(1)"});
    output.netlist_path = netlist;
    output.working_directory = root;
    output.title = "tran.cir";
    auto artifacts = TempOutputDirectory::create();
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    // act: adopt the run outputs
    const studio::RunOutputResult result = presenter.show_run_output(output, artifacts);
    // assert: the configured print file is the only primary dataset
    EXPECT_EQ(result.outcome, studio::RunOutputOutcome::Primary);
    EXPECT_EQ(presenter.dataset_count(), 1U);
    EXPECT_EQ(presenter.active_dataset(), 0U);
    EXPECT_EQ(presenter.title(), "tran.cir");
    ASSERT_NE(presenter.scene(), nullptr);
    EXPECT_FLOAT_EQ(presenter.scene()->tab_bar_height, 0.0f);
    EXPECT_GT(presenter.series_count(0), 0U);
}

TEST(StudioChartsPresenterChecks, a_lin_touchstone_run_opens_a_line_and_a_smith_tab) {
    // arrange: a run directory holding the produced touchstone file
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-run-lin";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path netlist = root / "netlist.cir";
    const std::filesystem::path touchstone = root / "netlist.cir.s2p";
    {
        // write a minimal two-port s parameter file
        std::ofstream file(touchstone);
        file << "# Hz S RI R 50\n";
        file << "1e8 0.20 0.00 0.80 0.00 0.80 0.00 -0.20 0.00\n";
        file << "2e8 0.10 0.10 0.70 -0.10 0.70 -0.10 -0.10 0.10\n";
    }
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".AC DEC 2 1e8 2e8", ".LIN FORMAT=TOUCHSTONE2 DATA=RI SPARCALC=1"});
    output.netlist_path = netlist;
    output.working_directory = root;
    output.title = "lin.cir";
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    // act: adopt the run output without an analysis print
    const studio::RunOutputResult result = presenter.show_run_output(output, nullptr);
    // assert: the rectangular and smith tabs own the same produced file
    EXPECT_EQ(result.outcome, studio::RunOutputOutcome::Primary);
    EXPECT_EQ(presenter.dataset_count(), 2U);
    EXPECT_EQ(presenter.active_dataset(), 0U);
    ASSERT_EQ(presenter.scene()->tabs.size(), 2U);
    EXPECT_EQ(presenter.scene()->tabs[1].title, "Smith Chart");
    // act: switch to the smith tab
    presenter.activate_dataset(1);
    // assert: the smith plane is the active scene
    ASSERT_NE(presenter.scene(), nullptr);
    ASSERT_FALSE(presenter.scene()->slots.empty());
    EXPECT_EQ(presenter.scene()->slots.front().kind, ChartKind::SMITH);
}

TEST(StudioChartsPresenterChecks, a_rerun_keeps_the_primary_tab_position_and_state) {
    // arrange: a user tab, a primary tab and another user tab
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-rerun";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path first_user = root / "user-first.csv";
    const std::filesystem::path second_user = root / "user-second.csv";
    const std::filesystem::path netlist = root / "netlist.cir";
    {
        // write the first user table
        std::ofstream file(first_user);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    {
        // write the second user table
        std::ofstream file(second_user);
        file << "TIME,V(1)\n0,3\n1,4\n";
    }
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 2u", ".PRINT TRAN FORMAT=CSV V(1)"});
    output.netlist_path = netlist;
    output.working_directory = root;
    output.title = "run.cir";
    auto artifacts = TempOutputDirectory::create();
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    ASSERT_TRUE(presenter.show_file(first_user));
    {
        // write the first run output
        std::ofstream file(root / "netlist.cir.csv");
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    ASSERT_EQ(presenter.show_run_output(output, artifacts).outcome, studio::RunOutputOutcome::Primary);
    ASSERT_TRUE(presenter.show_file(second_user));
    ASSERT_EQ(presenter.dataset_count(), 3U);
    ASSERT_EQ(presenter.active_dataset(), 2U);
    // act: select the primary and rerun the same netlist with new data
    presenter.activate_dataset(1);
    {
        // overwrite the produced table with the rerun data
        std::ofstream file(root / "netlist.cir.csv");
        file << "TIME,V(1)\n0,5\n1,6\n";
    }
    auto rerun_artifacts = TempOutputDirectory::create();
    const studio::RunOutputResult rerun = presenter.show_run_output(output, rerun_artifacts);
    // assert: the primary keeps its position and still carries a chart
    EXPECT_EQ(rerun.outcome, studio::RunOutputOutcome::Primary);
    EXPECT_EQ(presenter.dataset_count(), 3U);
    EXPECT_EQ(presenter.active_dataset(), 1U);
    EXPECT_EQ(presenter.title(), "run.cir");
    EXPECT_GT(presenter.series_count(0), 0U);
}

TEST(StudioChartsPresenterChecks, a_primary_run_output_has_no_close_affordance) {
    // arrange: a run directory holding a produced analysis file
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-run-close";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path netlist = root / "netlist.cir";
    {
        // write the produced table
        std::ofstream file(root / "netlist.cir.csv");
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 2u", ".PRINT TRAN FORMAT=CSV V(1)"});
    output.netlist_path = netlist;
    output.working_directory = root;
    output.title = "tran.cir";
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    const studio::RunOutputResult result = presenter.show_run_output(output, TempOutputDirectory::create());
    EXPECT_EQ(result.outcome, studio::RunOutputOutcome::Primary);
    EXPECT_EQ(presenter.dataset_count(), 1U);
    ASSERT_NE(presenter.scene(), nullptr);
    EXPECT_TRUE(presenter.scene()->tabs.empty());
    // act: the user asks to close the run-owned tab
    presenter.close_dataset(0);
    // assert: the tab survives because a run output cannot be closed
    EXPECT_EQ(presenter.dataset_count(), 1U);
    ASSERT_NE(presenter.scene(), nullptr);
}

TEST(StudioChartsPresenterChecks, a_run_lease_keeps_the_directory_until_the_tabs_close) {
    // arrange: a run directory holding a produced table
    auto artifacts = TempOutputDirectory::create();
    ASSERT_NE(artifacts, nullptr);
    const std::filesystem::path netlist = artifacts->netlist_path();
    const std::filesystem::path produced = netlist.string() + ".csv";
    {
        // write the produced table inside the run directory
        std::ofstream file(produced);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 2u", ".PRINT TRAN FORMAT=CSV V(1)"});
    output.netlist_path = netlist;
    output.working_directory = netlist.parent_path();
    output.title = "tran.cir";
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    ASSERT_EQ(presenter.show_run_output(output, artifacts).outcome, studio::RunOutputOutcome::Primary);
    // act: the presenter drops its own lease
    const std::filesystem::path directory = artifacts->path();
    artifacts.reset();
    // assert: the dataset keeps the directory alive
    EXPECT_TRUE(std::filesystem::exists(directory));
    // act: drop the dataset, which releases the last lease
    presenter.load_dataset(nullptr);
    // assert: the run directory is removed
    EXPECT_FALSE(std::filesystem::exists(directory));
}

TEST(StudioChartsPresenterChecks, a_run_without_output_drops_the_previous_run_tabs) {
    // arrange: a user tab and a primary tab from an earlier run
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-no-output";
    const std::filesystem::path run_dir = root / "run";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(run_dir, error);
    const std::filesystem::path user_csv = root / "user.csv";
    {
        // write the user table
        std::ofstream file(user_csv);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    const std::filesystem::path netlist = run_dir / "netlist.cir";
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 2u", ".PRINT TRAN FORMAT=CSV V(1)"});
    output.netlist_path = netlist;
    output.working_directory = run_dir;
    output.title = "tran.cir";
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    ASSERT_TRUE(presenter.show_file(user_csv));
    {
        // write the first run output
        std::ofstream file(run_dir / "netlist.cir.csv");
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    ASSERT_EQ(presenter.show_run_output(output, TempOutputDirectory::create()).outcome, studio::RunOutputOutcome::Primary);
    ASSERT_EQ(presenter.dataset_count(), 2U);
    // act: a later run produces no readable output
    std::filesystem::remove(run_dir / "netlist.cir.csv", error);
    const studio::RunOutputResult result = presenter.show_run_output(output, TempOutputDirectory::create());
    // assert: the stale run tab is gone while the user tab survives
    EXPECT_EQ(result.outcome, studio::RunOutputOutcome::None);
    EXPECT_EQ(presenter.dataset_count(), 1U);
    EXPECT_EQ(presenter.title(), "user");
    ASSERT_NE(presenter.scene(), nullptr);
    EXPECT_FLOAT_EQ(presenter.scene()->tab_bar_height, studio::CHART_TAB_HEIGHT);
}

TEST(StudioChartsPresenterChecks, a_companion_only_rerun_activates_the_first_companion) {
    // arrange: a primary run with two FFT companion files
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-fft-rerun";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path netlist = root / "netlist.cir";
    studio::RunOutput primary_output;
    primary_output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 2u", ".PRINT TRAN FORMAT=CSV V(1)"});
    primary_output.netlist_path = netlist;
    primary_output.working_directory = root;
    primary_output.title = "tran.cir";
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    {
        // write the analysis primary
        std::ofstream file(root / "netlist.cir.csv");
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    ASSERT_EQ(presenter.show_run_output(primary_output, TempOutputDirectory::create()).outcome, studio::RunOutputOutcome::Primary);
    // arrange: an FFT-only rerun with two produced files
    {
        // write the first FFT file
        std::ofstream file(root / "netlist.cir.fft0");
        file << "FFT analysis for I(L1):\n";
        file << "  Window: HANN, Start Time: 0.000000e+00, Stop Time: 2.000000e-02\n";
        file << "  First Harmonic: 9.765625e+02, Start Freq: 0.000000e+00, Stop Freq: 5.000000e+05\n";
        file << "  DC component    Norm. Mag= 1.000000e-02   Phase= 1.800000e+02\n";
        file << "       Index       Frequency       Norm. Mag           Phase\n";
        file << "   1    9.765625e+02    5.000000e-01    9.000000e+01\n";
    }
    {
        // write the second FFT file
        std::ofstream file(root / "netlist.cir.fft1");
        file << "FFT analysis for I(C1):\n";
        file << "  Window: HANN, Start Time: 0.000000e+00, Stop Time: 2.000000e-02\n";
        file << "  First Harmonic: 4.882812e+02, Start Freq: 0.000000e+00, Stop Freq: 2.500000e+05\n";
        file << "  DC component    Mag= 2.000000e-02   Phase= 0.000000e+00\n";
        file << "       Index       Frequency           Mag           Phase\n";
        file << "   1    4.882812e+02    6.000000e-01    0.000000e+00\n";
    }
    std::filesystem::remove(root / "netlist.cir.csv", error);
    studio::RunOutput fft_output;
    fft_output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 20m 0", ".FFT I(L1) NP=1024 WINDOW=HANN"});
    fft_output.netlist_path = netlist;
    fft_output.working_directory = root;
    fft_output.title = "tran.cir";
    // act: adopt the companion-only rerun
    const studio::RunOutputResult result = presenter.show_run_output(fft_output, TempOutputDirectory::create());
    // assert: the first FFT tab becomes active, not the second
    EXPECT_EQ(result.outcome, studio::RunOutputOutcome::CompanionOnly);
    EXPECT_EQ(presenter.dataset_count(), 2U);
    EXPECT_EQ(presenter.active_dataset(), 0U);
    EXPECT_NE(presenter.title().find("FFT"), std::string::npos);
}

TEST(StudioChartsPresenterChecks, discarding_run_companions_keeps_the_primary_tab) {
    // arrange: a primary run with an FFT companion
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-discard";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path netlist = root / "netlist.cir";
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 20m 0", ".PRINT TRAN FORMAT=CSV V(1)", ".FFT I(L1) NP=1024 WINDOW=HANN"});
    output.netlist_path = netlist;
    output.working_directory = root;
    output.title = "tran.cir";
    {
        // write the analysis primary
        std::ofstream file(root / "netlist.cir.csv");
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    {
        // write the FFT companion
        std::ofstream file(root / "netlist.cir.fft0");
        file << "FFT analysis for I(L1):\n";
        file << "  Window: HANN, Start Time: 0.000000e+00, Stop Time: 2.000000e-02\n";
        file << "  First Harmonic: 9.765625e+02, Start Freq: 0.000000e+00, Stop Freq: 5.000000e+05\n";
        file << "  DC component    Norm. Mag= 1.000000e-02   Phase= 1.800000e+02\n";
        file << "       Index       Frequency       Norm. Mag           Phase\n";
        file << "   1    9.765625e+02    5.000000e-01    9.000000e+01\n";
    }
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    ASSERT_EQ(presenter.show_run_output(output, TempOutputDirectory::create()).outcome, studio::RunOutputOutcome::Primary);
    ASSERT_EQ(presenter.dataset_count(), 2U);
    // act: a new run drops the previous companions
    presenter.discard_run_companions();
    // assert: the primary tab survives and becomes active
    EXPECT_EQ(presenter.dataset_count(), 1U);
    EXPECT_EQ(presenter.active_dataset(), 0U);
    EXPECT_EQ(presenter.title(), "tran.cir");
}

TEST(StudioChartsPresenterChecks, an_unreadable_analysis_file_falls_back_to_another_output) {
    // arrange: a run directory whose configured csv is unreadable and whose prn is valid
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-fallback";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path netlist = root / "netlist.cir";
    {
        // write a file the csv parser refuses
        std::ofstream file(root / "netlist.cir.csv");
        file << "not a csv table\n";
    }
    {
        // write a readable print table under another extension
        std::ofstream file(root / "netlist.cir.prn");
        file << "Index TIME V(1)\n";
        file << "0 0.0 0.0\n";
        file << "1 1.0 1.0\n";
        file << ".\n";
    }
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 2u", ".PRINT TRAN FORMAT=CSV V(1)"});
    output.netlist_path = netlist;
    output.working_directory = root;
    output.title = "tran.cir";
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    // act: adopt the run outputs
    const studio::RunOutputResult result = presenter.show_run_output(output, nullptr);
    // assert: the readable alternative loads but the analysis file is not marked as read
    EXPECT_EQ(result.outcome, studio::RunOutputOutcome::Primary);
    EXPECT_FALSE(result.analysis_loaded);
    EXPECT_EQ(presenter.dataset_count(), 1U);
    EXPECT_GT(presenter.series_count(0), 0U);
}

TEST(StudioChartsPresenterChecks, the_plot_groups_declare_the_primary_tab_charts) {
    // arrange: a run directory whose primary carries three expressions
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-plot-groups";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path netlist = root / "netlist.cir";
    {
        // write the produced table with the plotted quantities
        std::ofstream file(root / "netlist.cir.csv");
        file << "TIME,V(1),V(2),I(L1)\n0,0,0,0\n1,1,2,3\n2,2,4,5\n";
    }
    studio::RunOutput output;
    output.config = SimulationConfig::from_xyce_directives({".TRAN 1u 2u", ".PRINT TRAN FORMAT=CSV V(1) V(2) I(L1)"});
    output.netlist_path = netlist;
    output.working_directory = root;
    output.title = "tran.cir";
    output.suggested_plots = {{"V(1)"}, {"I(L1)"}};
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    // act: adopt the run output
    ASSERT_EQ(presenter.show_run_output(output, nullptr).outcome, studio::RunOutputOutcome::Primary);
    // assert: one chart per declared plot group
    EXPECT_EQ(presenter.chart_count(), 2U);
    EXPECT_EQ(presenter.series_count(0), 1U);
    EXPECT_EQ(presenter.series_count(1), 1U);
    // act: rerun with a different plot group set
    output.suggested_plots = {{"I(L1)"}};
    ASSERT_EQ(presenter.show_run_output(output, nullptr).outcome, studio::RunOutputOutcome::Primary);
    // assert: the changed declarations rebuild the primary charts
    EXPECT_EQ(presenter.chart_count(), 1U);
    EXPECT_EQ(presenter.series_count(0), 1U);
}

TEST(StudioChartsPresenterChecks, the_context_menu_commands_drive_the_active_dataset) {
    // arrange: a run directory whose table carries two plotted series
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "charts-presenter-menu";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path csv = root / "tran.csv";
    {
        // write the header and two rows the parser reads as one step
        std::ofstream file(csv);
        file << "TIME,V(1),I(L1)\n0,0,0\n1,1,2\n2,2,4\n";
    }
    albor::BindingEngine engine;
    studio::ChartsView view(&engine, nullptr);
    FakeTextEngine text;
    studio::ChartsPresenter presenter(view, text);
    ASSERT_TRUE(presenter.show_file(csv));
    ASSERT_EQ(presenter.chart_count(), 1U);
    ASSERT_EQ(presenter.series_count(0), 2U);
    // act: add a chart after the plotted one
    presenter.run_chart_command(studio::ChartCommand::AddChart, 0);
    // assert: the empty chart joins the stack and the scene
    EXPECT_EQ(presenter.chart_count(), 2U);
    ASSERT_NE(presenter.scene(), nullptr);
    EXPECT_EQ(presenter.scene()->slots.size(), 2U);
    // act: delete the chart the command targets
    presenter.run_chart_command(studio::ChartCommand::DeleteChart, 1);
    // assert: the original chart remains
    EXPECT_EQ(presenter.chart_count(), 1U);
    // act: clear every plot of the remaining chart
    presenter.run_chart_command(studio::ChartCommand::DeleteAllPlots, 0);
    // assert: the chart stays with no series
    EXPECT_EQ(presenter.chart_count(), 1U);
    EXPECT_EQ(presenter.series_count(0), 0U);
    // act: run the zoom commands on the empty chart
    presenter.run_chart_command(studio::ChartCommand::ZoomToFit, 0);
    presenter.run_chart_command(studio::ChartCommand::Autorange, 0);
    presenter.run_chart_command(studio::ChartCommand::ZoomAbscissaExtent, 0);
    // assert: the commands leave the chart in place
    EXPECT_EQ(presenter.chart_count(), 1U);
    // act: address a chart past the stack
    presenter.run_chart_command(studio::ChartCommand::AddChart, 9);
    // assert: an absent chart changes nothing
    EXPECT_EQ(presenter.chart_count(), 1U);
}

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "config/plugin_config.h"
#include "core/step_information.h"
#include "dsp/fft.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/xyce_output_file.h"
#include "io/xyce_raw_file.h"
#include "netlist/netlist_source.h"
#include "simulation/simulation_config.h"
#include "simulation/transient_simulation_parameters.h"
#include "ui/main_window_presenter.h"

namespace
{
    // recording view double that captures every presenter interaction
    class RecordingView : public MainWindowViewDef
    {
    public:
        // window chrome
        void set_title(const std::string& title) override { m_title = title; }

        void set_status_text(const std::string& text) override { m_status_text = text; }

        void apply_action_enablement(const ActionStateEnablement& enablement) override { m_last_enablement = enablement; }

        // simulation run state, drives the Run/Stop toolbar toggle
        void set_simulation_running(bool running) override { m_simulation_running_ui = running; }

        // content views (netlist editor vs charts, mutually exclusive)
        void show_netlist_view() override {
            m_netlist_view_shown = true;
            m_charts_view_shown = false;
        }

        void show_charts_view() override {
            m_charts_view_shown = true;
            m_netlist_view_shown = false;
        }

        void set_netlist_editor_content(const std::string& content) override { m_editor_content = content; }

        [[nodiscard]] std::string netlist_editor_content() const override { return m_editor_content; }

        void set_netlist_editor_read_only(bool read_only) override { m_editor_read_only = read_only; }

        [[nodiscard]] bool charts_shown() const override { return m_charts_view_shown && !m_netlist_view_shown; }

        // simulation output panel / log
        void show_simulation_output_panel() override { m_output_panel_hidden = false; }

        void hide_simulation_output_panel() override { m_output_panel_hidden = true; }

        void clear_simulation_output() override { m_output_lines.clear(); }

        void append_simulation_output_line(const std::string& line) override { m_output_lines.push_back(line); }

        [[nodiscard]] bool simulation_output_panel_hidden() const override { return m_output_panel_hidden; }

        [[nodiscard]] bool simulation_output_has_content() const override { return !m_output_lines.empty(); }

        // charts; the dataset id identifies the plot tab the charts belong to
        void update_charts(int dataset_id, ExpressionManager&, const StepInformation&, AbscissaScale, const std::vector<std::vector<std::string>>&, bool smith) override {
            m_update_charts_count++;
            m_updated_dataset_ids.push_back(dataset_id);
            m_updated_smith_datasets.push_back(smith);
        }

        void release_charts(int dataset_id) override { m_released_dataset_ids.push_back(dataset_id); }

        void release_all_charts() override { m_release_all_count++; }

        // plot tabs
        void set_plot_tabs(const std::vector<PlotTabItem>& tabs, int active_index) override {
            m_plot_tabs = tabs;
            m_active_plot_tab = active_index;
        }

        void set_active_plot_tab(int active_index) override { m_active_plot_tab = active_index; }

        // move the chart at the given index to the given index in the stack
        void move_chart(size_t from, size_t to) override {
            m_moved_from.push_back(from);
            m_moved_to.push_back(to);
        }

        // show the FFT setup dialog for the chart at the given index
        void show_fft_dialog(size_t chart_index) override { m_fft_dialog_index = chart_index; }

        // show the step tool dialog for the chart at the given index
        void show_step_tool_dialog(size_t chart_index) override { m_step_tool_dialog_index = chart_index; }

        // modal dialogs (the view's job, they need a parent window)
        [[nodiscard]] std::optional<SimulationConfig> show_simulation_parameters_dialog(const SimulationConfig& current) override {
            m_simulation_dialog_requests++;
            m_last_simulation_config_seed = current;
            return m_simulation_config_result;
        }

        [[nodiscard]] std::optional<PluginConfig> show_plugin_config_dialog(const PluginConfig&) override { return m_plugin_config_result; }

        [[nodiscard]] std::optional<std::filesystem::path> request_netlist_save_path() override { return m_save_path_result; }

        // simulation process lifecycle (presenter decides when, the view wires the process events)
        void start_simulation_process(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) override {
            m_started = true;
            m_started_program = program;
            m_started_netlist_path = netlist_path;
            m_started_working_directory = working_directory;
        }

        // cancel the running simulation process owned by the view
        void cancel_simulation_process() override { m_cancel_count++; }

        // window management
        void spawn_raw_file_window(std::shared_ptr<XyceOutputFile> raw_file) override { m_spawned_files.push_back(std::move(raw_file)); }

        // event handler wiring (unused by the recording view)
        void set_event_handler(MainWindowViewDefEvents&) override {}

        // recorded state
        std::string m_title;
        std::string m_status_text;
        ActionStateEnablement m_last_enablement;
        bool m_simulation_running_ui = false;
        bool m_netlist_view_shown = false;
        bool m_charts_view_shown = false;
        std::string m_editor_content;
        bool m_editor_read_only = true;
        bool m_output_panel_hidden = true;
        std::vector<std::string> m_output_lines;
        int m_update_charts_count = 0;
        std::vector<int> m_updated_dataset_ids;
        std::vector<bool> m_updated_smith_datasets;
        std::vector<int> m_released_dataset_ids;
        int m_release_all_count = 0;
        std::vector<PlotTabItem> m_plot_tabs;
        int m_active_plot_tab = -1;
        std::optional<size_t> m_fft_dialog_index;
        std::optional<size_t> m_step_tool_dialog_index;
        std::vector<size_t> m_moved_from;
        std::vector<size_t> m_moved_to;
        std::optional<SimulationConfig> m_simulation_config_result;
        int m_simulation_dialog_requests = 0;
        std::optional<SimulationConfig> m_last_simulation_config_seed;
        std::optional<PluginConfig> m_plugin_config_result;
        std::optional<std::filesystem::path> m_save_path_result;
        bool m_started = false;
        std::string m_started_program;
        std::filesystem::path m_started_netlist_path;
        std::filesystem::path m_started_working_directory;
        int m_cancel_count = 0;
        std::vector<std::shared_ptr<XyceOutputFile>> m_spawned_files;
    };

    // stub netlist source returning canned content
    class StubNetlistSource : public NetlistSource
    {
    public:
        StubNetlistSource(std::string content, std::filesystem::path working_directory) :
            m_content(std::move(content)), m_working_directory(std::move(working_directory)) {}

        [[nodiscard]] std::string title() const override { return "stub.net"; }

        [[nodiscard]] bool is_read_only() const override { return false; }

        [[nodiscard]] bool has_backing_file() const override { return m_has_backing_file; }

        [[nodiscard]] std::filesystem::path working_directory() const override { return m_working_directory; }

        [[nodiscard]] std::tuple<bool, std::string> load_netlist() override { return {m_reloaded, m_content}; }

        void save_netlist(const std::string& content = "") override {
            m_save_count++;
            m_saved_content = content;
        }

        std::string m_content;
        std::filesystem::path m_working_directory;
        bool m_reloaded = false;
        bool m_has_backing_file = true;
        int m_save_count = 0;
        std::string m_saved_content;
    };
} // namespace

// ========================================================================================
// construction and initial state
// ========================================================================================

TEST(SlintMainWindowPresenterChecks, constructor_applies_initial_action_states) {
    // arrange
    RecordingView view;
    // act
    const SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // assert — the empty window allows opening files but no editing actions
    EXPECT_TRUE(view.m_last_enablement.open);
    EXPECT_FALSE(view.m_last_enablement.save);
    EXPECT_FALSE(view.m_last_enablement.run_simulation);
    EXPECT_FALSE(view.m_last_enablement.show_netlist);
}

TEST(SlintMainWindowPresenterChecks, run_simulation_with_invalid_xyce_path_sets_error_status) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_run_simulation();
    // assert
    EXPECT_EQ(view.m_status_text, "Configured Xyce executable path is invalid");
    EXPECT_FALSE(view.m_started);
}

TEST(SlintMainWindowPresenterChecks, run_simulation_with_empty_netlist_never_launches) {
    // arrange — use the test binary as a stand-in valid executable
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act
    presenter.on_run_simulation();
    // assert — nothing can be simulated, so no process starts
    EXPECT_FALSE(view.m_started);
}

TEST(SlintMainWindowPresenterChecks, two_consecutive_runs_on_empty_netlist_do_not_launch) {
    // arrange — empty netlist with a valid executable path
    RecordingView view;
    auto source = std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path());
    source->m_reloaded = true;
    SlintMainWindowPresenter presenter(view, std::move(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — first run
    presenter.on_run_simulation();
    // assert — empty netlist exits early without dialog or launch
    EXPECT_EQ(view.m_simulation_dialog_requests, 0);
    EXPECT_FALSE(view.m_started);
    EXPECT_EQ(view.m_status_text, "No netlist content to simulate");
    // act — second run (cached pending state is still empty from the first early exit)
    view.m_status_text.clear();
    presenter.on_run_simulation();
    // assert — same early exit, no dialog, no launch
    EXPECT_EQ(view.m_simulation_dialog_requests, 0);
    EXPECT_FALSE(view.m_started);
    EXPECT_EQ(view.m_status_text, "No netlist content to simulate");
}

TEST(SlintMainWindowPresenterChecks, opening_new_netlist_clears_stale_pending_state) {
    // arrange — create a temp .cir file with no analysis directives
    const auto temp_dir = std::filesystem::temp_directory_path();
    const auto temp_cir = temp_dir / "test_no_analysis.cir";
    {
        std::ofstream file(temp_cir, std::ios::out | std::ios::trunc);
        file << "V1 1 0 5\nR1 1 0 1K\n.END\n";
    }
    RecordingView view;
    // arrange — first netlist has .TRAN so on_run_simulation populates the pending cache
    auto first_source = std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.END\n", temp_dir);
    SlintMainWindowPresenter presenter(view, std::move(first_source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // fill the pending parse cache and m_simulation_config with the first netlist data
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // cleanup the temp netlist from the first run
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    view.m_started = false;
    view.m_simulation_dialog_requests = 0;
    // act — open a second netlist file that has no analysis directives
    presenter.on_open_xyce_file(temp_cir);
    // the editor now reflects the new file content
    ASSERT_EQ(view.m_editor_content, "V1 1 0 5\nR1 1 0 1K\n.END\n");
    // run simulation on the second netlist
    presenter.on_run_simulation();
    // assert — the config dialog was shown because the new file has no analysis directives; before the fix the stale TRAN config from the first netlist would have launched the simulation directly without showing the dialog
    EXPECT_EQ(view.m_simulation_dialog_requests, 1);
    EXPECT_FALSE(view.m_started);
    // cleanup
    std::filesystem::remove(temp_cir, ec);
}

// ========================================================================================
// simulation control flow
// ========================================================================================

TEST(SlintMainWindowPresenterChecks, run_simulation_without_analysis_prompts_for_parameters) {
    // arrange — netlist without any analysis directive leaves the config empty
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act
    presenter.on_run_simulation();
    // assert — the configure dialog was requested instead of launching
    EXPECT_EQ(view.m_simulation_dialog_requests, 1);
    EXPECT_FALSE(view.m_started);
}

TEST(SlintMainWindowPresenterChecks, run_simulation_with_transient_launches_process) {
    // arrange
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path();
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.END\n", working_directory);
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act
    presenter.on_run_simulation();
    // assert — the process was started with the plugin executable and a temp netlist
    ASSERT_TRUE(view.m_started);
    EXPECT_EQ(view.m_started_program, testing::internal::GetArgvs()[0]);
    EXPECT_EQ(view.m_started_working_directory, working_directory);
    EXPECT_FALSE(view.m_started_netlist_path.empty());
    EXPECT_TRUE(std::filesystem::exists(view.m_started_netlist_path));
    // the output panel is no longer shown on launch — it shows on failure only
    EXPECT_TRUE(view.m_output_panel_hidden);
    EXPECT_TRUE(view.m_simulation_running_ui);
    EXPECT_FALSE(view.m_last_enablement.run_simulation);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, pending_dialog_result_launches_the_simulation) {
    // arrange — no analysis directive so the run flow parks on the dialog
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_FALSE(view.m_started);
    // act — accept a transient configuration through the dialog result
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    presenter.on_simulation_parameters_dialog_result(config);
    // assert
    ASSERT_TRUE(view.m_started);
    EXPECT_TRUE(view.m_simulation_running_ui);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, rerun_with_saved_config_does_not_show_empty_dialog) {
    // arrange — netlist without any analysis directive so the first run parks on the dialog; the source reports not-reloaded on the second run
    RecordingView view;
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.END\n", std::filesystem::temp_directory_path());
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — first run: no directives → dialog shown
    presenter.on_run_simulation();
    ASSERT_EQ(view.m_simulation_dialog_requests, 1);
    ASSERT_FALSE(view.m_started);
    // accept a transient configuration
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    presenter.on_simulation_parameters_dialog_result(config);
    ASSERT_TRUE(view.m_started);
    // record the first run's netlist path
    const auto first_netlist_path = view.m_started_netlist_path;
    // finish the first run successfully
    presenter.on_simulation_finished(0, false);
    // act — second run (reloaded=false, cached pending state still valid, m_simulation_config holds the saved transient config)
    presenter.on_run_simulation();
    // assert — the dialog was NOT shown again.  instead, the saved config triggered a direct launch with a fresh netlist path
    EXPECT_EQ(view.m_simulation_dialog_requests, 1);
    ASSERT_TRUE(view.m_started);
    EXPECT_NE(view.m_started_netlist_path, first_netlist_path);
    EXPECT_TRUE(view.m_simulation_running_ui);
    // cleanup both temp netlists
    std::error_code ec;
    std::filesystem::remove(first_netlist_path, ec);
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, raw_print_file_is_stripped_for_xyce_and_copied_on_finish) {
    // arrange — netlist with a transient analysis whose RAW print carries an output file
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path() / "xyce_studio_copy_test";
    std::filesystem::remove_all(working_directory);
    std::filesystem::create_directories(working_directory);
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=RAW FILE=copy_test_user_out.raw V(1)\n.END\n", working_directory);
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — launch the simulation
    presenter.on_run_simulation();
    // assert — the netlist handed to Xyce does not carry the FILE= option
    ASSERT_TRUE(view.m_started);
    {
        std::ifstream temp_netlist(view.m_started_netlist_path);
        const std::string content((std::istreambuf_iterator<char>(temp_netlist)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content.find("FILE=copy_test_user_out.raw"), std::string::npos);
        EXPECT_NE(content.find(".PRINT TRAN FORMAT=RAW V(1)"), std::string::npos);
    }
    // the editor keeps the user-facing directive with the FILE= option intact
    EXPECT_NE(view.m_editor_content.find("FILE=copy_test_user_out.raw"), std::string::npos);
    // simulate Xyce producing the RAW file next to the temporary netlist
    const auto produced_path = view.m_started_netlist_path.string() + ".raw";
    const std::string payload = "Title: Test\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nBinary:\n";
    {
        std::ofstream produced(produced_path, std::ios::binary);
        produced.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        produced.write("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32);
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the produced file was copied to the user-indicated location
    const auto copied_path = working_directory / "copy_test_user_out.raw";
    ASSERT_TRUE(std::filesystem::exists(copied_path));
    {
        std::ifstream copied(copied_path);
        const std::string copied_content((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
        EXPECT_EQ(copied_content, payload + std::string(32, '\0'));
    }
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(produced_path, ec);
    std::filesystem::remove_all(working_directory, ec);
}

TEST(SlintMainWindowPresenterChecks, dialog_file_with_spaces_is_quoted_in_netlist_and_copied_on_finish) {
    // arrange — directive-less netlist so the run parks on the dialog
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path() / "xyce_studio_copy_test_spaces";
    std::filesystem::remove_all(working_directory);
    std::filesystem::create_directories(working_directory);
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.END\n", working_directory);
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_FALSE(view.m_started);
    // accept a transient configuration whose print file name carries spaces, entered in the dialog without quotes
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, PrintParameters("TRAN", "RAW", "file with space.raw", {"V(1)"}, {}), {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    presenter.on_simulation_parameters_dialog_result(config);
    ASSERT_TRUE(view.m_started);
    // assert — the netlist handed to Xyce carries no FILE= option at all
    {
        std::ifstream temp_netlist(view.m_started_netlist_path);
        const std::string content((std::istreambuf_iterator<char>(temp_netlist)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content.find("FILE="), std::string::npos);
    }
    // the editor netlist quotes the filename so it survives tokenization
    EXPECT_NE(view.m_editor_content.find(R"(FILE="file with space.raw")"), std::string::npos);
    // simulate Xyce producing the RAW file next to the temporary netlist
    const auto produced_path = view.m_started_netlist_path.string() + ".raw";
    const std::string payload = "Title: Test\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nBinary:\n";
    {
        std::ofstream produced(produced_path, std::ios::binary);
        produced.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        produced.write("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32);
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the produced file was copied to the user-indicated spaced filename
    const auto copied_path = working_directory / "file with space.raw";
    ASSERT_TRUE(std::filesystem::exists(copied_path));
    {
        std::ifstream copied(copied_path);
        const std::string copied_content((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
        EXPECT_EQ(copied_content, payload + std::string(32, '\0'));
    }
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(produced_path, ec);
    std::filesystem::remove_all(working_directory, ec);
}

TEST(SlintMainWindowPresenterChecks, noise_print_with_operators_is_stripped_for_xyce_and_copied_on_finish) {
    // arrange — netlist with a noise analysis whose RAW print carries an output file and device noise operators, the emitted .PRINT NOISE statement appends the DNI()/DNO() operators after the base print serialization and they must survive the FILE= removal
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path() / "xyce_studio_noise_copy_test";
    std::filesystem::remove_all(working_directory);
    std::filesystem::create_directories(working_directory);
    StubNetlistSource* source = new StubNetlistSource("V1 1 2 5\nR1 1 2 1K\nR2 2 0 1K\n.NOISE V(2) V1 DEC 10 1 100MEG\n.PRINT NOISE FORMAT=RAW FILE=noise_out.raw INOISE DNI(R1)\n.END\n", working_directory);
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — launch the simulation
    presenter.on_run_simulation();
    // assert — the netlist handed to Xyce does not carry the FILE= option
    ASSERT_TRUE(view.m_started);
    {
        std::ifstream temp_netlist(view.m_started_netlist_path);
        const std::string content((std::istreambuf_iterator<char>(temp_netlist)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content.find("FILE=noise_out.raw"), std::string::npos);
        EXPECT_NE(content.find(".PRINT NOISE FORMAT=RAW INOISE DNI(R1)"), std::string::npos);
    }
    // the editor keeps the user-facing directive with the FILE= option intact
    EXPECT_NE(view.m_editor_content.find("FILE=noise_out.raw"), std::string::npos);
    // simulate Xyce producing the RAW file next to the temporary netlist
    const auto produced_path = view.m_started_netlist_path.string() + ".raw";
    const std::string payload = "Title: Test\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nBinary:\n";
    {
        std::ofstream produced(produced_path, std::ios::binary);
        produced.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        produced.write("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32);
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the produced file was copied to the user-indicated location
    const auto copied_path = working_directory / "noise_out.raw";
    ASSERT_TRUE(std::filesystem::exists(copied_path));
    {
        std::ifstream copied(copied_path);
        const std::string copied_content((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
        EXPECT_EQ(copied_content, payload + std::string(32, '\0'));
    }
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(produced_path, ec);
    std::filesystem::remove_all(working_directory, ec);
}

TEST(SlintMainWindowPresenterChecks, copy_overwrites_a_destination_opened_as_primary_dataset) {
    // arrange — the user opened a raw file that is also the simulation print destination; the copy must run after the dataset swap released the reference holding its mapping open (a sharing violation on Windows)
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path() / "xyce_studio_open_copy_test";
    std::filesystem::remove_all(working_directory);
    std::filesystem::create_directories(working_directory);
    // write a parseable raw file at the destination path and open it through the real parser so the dataset holds a live memory mapping over it
    const std::string opened_payload = "Title: Test\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nBinary:\n";
    {
        std::ofstream opened(working_directory / "user_out.raw", std::ios::binary);
        opened.write(opened_payload.data(), static_cast<std::streamsize>(opened_payload.size()));
        opened.write("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32);
    }
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=RAW FILE=user_out.raw V(1)\n.END\n", working_directory);
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    auto opened_file = xyce_raw_file_parser(working_directory / "user_out.raw");
    ASSERT_TRUE(opened_file.has_value());
    // act — open the file as the primary dataset, then launch the simulation
    presenter.load_analysis_measurements(std::move(opened_file.value()));
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // simulate Xyce producing a parseable RAW file next to the temporary netlist
    const auto produced_path = view.m_started_netlist_path.string() + ".raw";
    {
        std::ofstream produced(produced_path, std::ios::binary);
        produced.write(opened_payload.data(), static_cast<std::streamsize>(opened_payload.size()));
        produced.write("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32);
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the copy overwrote the destination even though it was open as the primary dataset when the simulation finished
    const auto copied_path = working_directory / "user_out.raw";
    ASSERT_TRUE(std::filesystem::exists(copied_path));
    {
        std::ifstream copied(copied_path);
        const std::string copied_content((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
        EXPECT_EQ(copied_content, opened_payload + std::string(32, '\0'));
    }
    // the active dataset was re-pointed at the produced file, preserving the opened dataset's identity (the primary dataset id is kept on a re-run)
    ASSERT_GE(view.m_updated_dataset_ids.size(), 2u);
    EXPECT_EQ(view.m_updated_dataset_ids.front(), view.m_updated_dataset_ids.back());
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove_all(working_directory, ec);
}

TEST(SlintMainWindowPresenterChecks, failed_parse_does_not_overwrite_the_user_destination) {
    // arrange — the produced raw file exists but is not parseable; the copy is confined to successful runs so the user's last valid copy is preserved
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path() / "xyce_studio_failed_parse_test";
    std::filesystem::remove_all(working_directory);
    std::filesystem::create_directories(working_directory);
    const std::string sentinel = "previous valid copy";
    {
        std::ofstream destination(working_directory / "user_out.raw", std::ios::binary);
        destination.write(sentinel.data(), static_cast<std::streamsize>(sentinel.size()));
    }
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=RAW FILE=user_out.raw V(1)\n.END\n", working_directory);
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // simulate Xyce leaving a partial (unparseable) raw file
    const auto produced_path = view.m_started_netlist_path.string() + ".raw";
    {
        std::ofstream produced(produced_path, std::ios::binary);
        produced.write("raw payload", 11);
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the unparseable produced file was NOT copied over the destination, which keeps the last valid copy
    const auto copied_path = working_directory / "user_out.raw";
    ASSERT_TRUE(std::filesystem::exists(copied_path));
    {
        std::ifstream copied(copied_path);
        const std::string copied_content((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
        EXPECT_EQ(copied_content, sentinel);
    }
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(produced_path, ec);
    std::filesystem::remove_all(working_directory, ec);
}

TEST(SlintMainWindowPresenterChecks, schematic_change_without_directives_preserves_saved_config) {
    // arrange — directive-less netlist, valid executable
    RecordingView view;
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.END\n", std::filesystem::temp_directory_path());
    source->m_reloaded = true;
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — first run: no directives → dialog shown
    presenter.on_run_simulation();
    ASSERT_EQ(view.m_simulation_dialog_requests, 1);
    ASSERT_FALSE(view.m_started);
    // accept a transient configuration
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    presenter.on_simulation_parameters_dialog_result(config);
    ASSERT_TRUE(view.m_started);
    const auto first_netlist_path = view.m_started_netlist_path;
    // finish the first run successfully
    presenter.on_simulation_finished(0, false);
    view.m_started = false;
    // arrange — KiCad schematic changed (reloaded=true) but still has no directives
    source->m_reloaded = true;
    // act — second run
    presenter.on_run_simulation();
    // assert — saved config was preserved despite reload, dialog NOT shown, direct launch
    EXPECT_EQ(view.m_simulation_dialog_requests, 1);
    ASSERT_TRUE(view.m_started);
    EXPECT_NE(view.m_started_netlist_path, first_netlist_path);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(first_netlist_path, ec);
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, schematic_reexport_with_identical_content_keeps_edited_transient_config) {
    // arrange — a schematic netlist carrying the transient directives (KiCad plugin mode)
    RecordingView view;
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 20m 0\n.END\n", std::filesystem::temp_directory_path());
    source->m_reloaded = true;
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — run the simulation once with the schematic directives
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // assert — the first run used the schematic values
    {
        std::ifstream first_run(view.m_started_netlist_path);
        const std::string first_content((std::istreambuf_iterator<char>(first_run)), std::istreambuf_iterator<char>());
        EXPECT_NE(first_content.find(".TRAN 1u 20m 0"), std::string::npos);
    }
    const auto first_netlist_path = view.m_started_netlist_path;
    presenter.on_simulation_finished(0, false);
    view.m_started = false;
    // act — edit the transient end time through the configure dialog and accept it
    presenter.on_configure_simulation();
    ASSERT_EQ(view.m_simulation_dialog_requests, 1);
    const SimulationConfig edited("TRAN", TransientSimulationParameters("1u", "25m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    presenter.on_simulation_parameters_dialog_result(edited);
    // assert — the editor reflects the edited parameters
    EXPECT_NE(view.m_editor_content.find(".TRAN 1u 25m"), std::string::npos);
    // arrange — KiCad autosaves the schematic and re-exports it; the exported content is identical because the schematic still holds the old directives
    source->m_reloaded = true;
    // act — run the simulation again
    presenter.on_run_simulation();
    // assert — the second run keeps the edited parameters, the re-export must not resurrect the schematic directives over the accepted dialog config
    ASSERT_TRUE(view.m_started);
    EXPECT_NE(view.m_started_netlist_path, first_netlist_path);
    {
        std::ifstream second_run(view.m_started_netlist_path);
        const std::string second_content((std::istreambuf_iterator<char>(second_run)), std::istreambuf_iterator<char>());
        EXPECT_NE(second_content.find(".TRAN 1u 25m"), std::string::npos);
        EXPECT_EQ(second_content.find(".TRAN 1u 20m"), std::string::npos);
    }
    EXPECT_NE(view.m_editor_content.find(".TRAN 1u 25m"), std::string::npos);
    EXPECT_EQ(view.m_editor_content.find(".TRAN 1u 20m"), std::string::npos);
    // cleanup both temp netlists
    std::error_code ec;
    std::filesystem::remove(first_netlist_path, ec);
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, schematic_reexport_with_identical_content_does_not_revert_dialog_to_schematic_values) {
    // arrange — a schematic netlist carrying the transient directives (KiCad plugin mode)
    RecordingView view;
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 20m\n.END\n", std::filesystem::temp_directory_path());
    source->m_reloaded = true;
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — run the simulation once with the schematic directives
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // act — edit the transient end time through the configure dialog and accept it
    presenter.on_configure_simulation();
    ASSERT_EQ(view.m_simulation_dialog_requests, 1);
    const SimulationConfig edited("TRAN", TransientSimulationParameters("1u", "25m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    presenter.on_simulation_parameters_dialog_result(edited);
    // arrange — KiCad autosaves the schematic and re-exports it; the exported content is identical because the schematic still holds the old directives
    source->m_reloaded = true;
    // act — reopen the configure dialog
    presenter.on_configure_simulation();
    // assert — the dialog is seeded with the edited values, not the schematic directives
    ASSERT_TRUE(view.m_last_simulation_config_seed.has_value());
    EXPECT_FALSE(std::holds_alternative<std::monostate>(view.m_last_simulation_config_seed->analysis));
    EXPECT_EQ(std::get<TransientSimulationParameters>(view.m_last_simulation_config_seed->analysis).final_time_value, "25m");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, schematic_change_with_directives_overwrites_saved_config) {
    // arrange — a schematic netlist carrying the transient directives (KiCad plugin mode)
    RecordingView view;
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 20m\n.END\n", std::filesystem::temp_directory_path());
    source->m_reloaded = true;
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — run the simulation once with the schematic directives
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    const auto first_netlist_path = view.m_started_netlist_path;
    // act — edit the transient end time through the configure dialog and accept it
    presenter.on_configure_simulation();
    ASSERT_EQ(view.m_simulation_dialog_requests, 1);
    const SimulationConfig edited("TRAN", TransientSimulationParameters("1u", "25m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    presenter.on_simulation_parameters_dialog_result(edited);
    presenter.on_simulation_finished(0, false);
    view.m_started = false;
    // arrange — the schematic itself changed (a real directive edit in KiCad); the re-export now carries a different transient end time
    source->m_content = "V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 50m\n.END\n";
    source->m_reloaded = true;
    // act — run the simulation again
    presenter.on_run_simulation();
    // assert — the changed schematic directives win over the saved dialog config, the next run and the reopened dialog both use the new schematic value
    ASSERT_TRUE(view.m_started);
    EXPECT_NE(view.m_started_netlist_path, first_netlist_path);
    {
        std::ifstream second_run(view.m_started_netlist_path);
        const std::string second_content((std::istreambuf_iterator<char>(second_run)), std::istreambuf_iterator<char>());
        EXPECT_NE(second_content.find(".TRAN 1u 50m"), std::string::npos);
        EXPECT_EQ(second_content.find(".TRAN 1u 25m"), std::string::npos);
    }
    presenter.on_configure_simulation();
    ASSERT_TRUE(view.m_last_simulation_config_seed.has_value());
    EXPECT_FALSE(std::holds_alternative<std::monostate>(view.m_last_simulation_config_seed->analysis));
    EXPECT_EQ(std::get<TransientSimulationParameters>(view.m_last_simulation_config_seed->analysis).final_time_value, "50m");
    // cleanup both temp netlists
    std::error_code ec;
    std::filesystem::remove(first_netlist_path, ec);
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, configure_result_updates_netlist_without_launching) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_configure_simulation();
    ASSERT_FALSE(view.m_started);
    // act — accept a transient configuration from the configure dialog
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    presenter.on_simulation_parameters_dialog_result(config);
    // assert — the editor was rebuilt with the new directives, nothing launched
    EXPECT_FALSE(view.m_started);
    EXPECT_NE(view.m_editor_content.find(".TRAN 1u 1m"), std::string::npos);
}

TEST(SlintMainWindowPresenterChecks, subsequent_configure_shows_dialog_with_saved_config) {
    // arrange — directive-less netlist, valid executable
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — first configure: no directives → dialog shown with empty seed
    presenter.on_configure_simulation();
    ASSERT_EQ(view.m_simulation_dialog_requests, 1);
    ASSERT_TRUE(view.m_last_simulation_config_seed.has_value());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(view.m_last_simulation_config_seed->analysis));
    // accept a transient configuration
    const SimulationConfig config("TRAN", TransientSimulationParameters("1u", "1m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), {}, {}, OptionParameters({}, {}, {}, {}, {}), {}, true);
    presenter.on_simulation_parameters_dialog_result(config);
    // act — second configure: dialog shown again, seeded with the saved transient config
    presenter.on_configure_simulation();
    EXPECT_EQ(view.m_simulation_dialog_requests, 2);
    ASSERT_TRUE(view.m_last_simulation_config_seed.has_value());
    EXPECT_FALSE(std::holds_alternative<std::monostate>(view.m_last_simulation_config_seed->analysis));
    EXPECT_EQ(view.m_last_simulation_config_seed->analysis_type, "TRAN");
}

TEST(SlintMainWindowPresenterChecks, cancel_simulation_forwards_to_view) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_cancel_simulation();
    // assert
    EXPECT_EQ(view.m_cancel_count, 1);
}

TEST(SlintMainWindowPresenterChecks, simulation_finished_canceled_sets_status) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_simulation_finished(0, true);
    // assert
    EXPECT_EQ(view.m_status_text, "Simulation canceled");
    EXPECT_FALSE(view.m_simulation_running_ui);
    EXPECT_FALSE(view.m_output_panel_hidden);
}

TEST(SlintMainWindowPresenterChecks, simulation_finished_failure_reports_exit_code) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_simulation_finished(3, false);
    // assert
    EXPECT_EQ(view.m_status_text, "Simulation failed (exit code 3)");
    EXPECT_FALSE(view.m_simulation_running_ui);
    EXPECT_FALSE(view.m_output_panel_hidden);
}

TEST(SlintMainWindowPresenterChecks, simulation_finished_success_loads_raw_file) {
    // arrange — launch a transient simulation first so the run paths are known
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=RAW V(1)\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write an ascii raw file at the expected output location
    const auto raw_path = view.m_started_netlist_path.string() + ".raw";
    {
        std::ofstream raw_file(raw_path, std::ios::out | std::ios::trunc);
        raw_file << "Title: Presenter Test Circuit\n";
        raw_file << "Plotname: Transient Analysis\n";
        raw_file << "Flags: real\n";
        raw_file << "No. Variables: 2\n";
        raw_file << "No. Points: 3\n";
        raw_file << "Variables:\n";
        raw_file << "\t0\ttime\ttime\n";
        raw_file << "\t1\tV(1)\tvoltage\n";
        raw_file << "Values:\n";
        raw_file << " 0  0.0  1.0\n";
        raw_file << " 1  0.001  2.0\n";
        raw_file << " 2  0.002  3.0\n";
    }
    // expose the output panel before act so we can assert it gets hidden on success
    view.m_output_panel_hidden = false;
    // act
    presenter.on_simulation_finished(0, false);
    // assert — charts are shown with the parsed data and the title comes from the raw file; the run rewrote the editor netlist, so the unsaved-changes marker stays
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_title, "* Presenter Test Circuit");
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    EXPECT_TRUE(view.m_output_panel_hidden);
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_GT(view.m_update_charts_count, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(raw_path, ec);
}

TEST(SlintMainWindowPresenterChecks, simulation_finished_success_loads_csd_file) {
    // arrange — launch a transient simulation with a PROBE print first so the run paths are known
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=PROBE V(1)\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write a real csd file at the expected output location
    const auto csd_path = view.m_started_netlist_path.string() + ".csd";
    {
        std::ofstream csd_file(csd_path, std::ios::out | std::ios::trunc);
        csd_file << "#H\n";
        csd_file << "SOURCE='Xyce' VERSION='7.10'\n";
        csd_file << "TITLE='* Presenter Probe Circuit'\n";
        csd_file << "SUBTITLE='Xyce data'\n";
        csd_file << "ANALYSIS='Transient Analysis' SERIALNO='12345'\n";
        csd_file << "ALLVALUES='NO' COMPLEXVALUES='NO' NODES='1'\n";
        csd_file << "SWEEPVAR='Time' SWEEPMODE='VAR_STEP'\n";
        csd_file << "#N\n";
        csd_file << "'V(1)'\n";
        csd_file << "#C 0.000000000e+00 1\n";
        csd_file << "1.000000000e+00:1\n";
        csd_file << "#C 1.000000000e-09 1\n";
        csd_file << "2.000000000e+00:1\n";
        csd_file << "#;\n";
    }
    // act
    presenter.on_simulation_finished(0, false);
    // assert — charts are shown with the parsed csd data and the title comes from the csd file; the run rewrote the editor netlist, so the unsaved-changes marker stays
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_title, "* Presenter Probe Circuit");
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_GT(view.m_update_charts_count, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(csd_path, ec);
}

TEST(SlintMainWindowPresenterChecks, probe_print_file_is_stripped_for_xyce_and_copied_on_finish) {
    // arrange — netlist with a transient analysis whose PROBE print carries an output file
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path() / "xyce_studio_probe_copy_test";
    std::filesystem::remove_all(working_directory);
    std::filesystem::create_directories(working_directory);
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=PROBE FILE=probe_user_out.csd V(1)\n.END\n", working_directory);
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — launch the simulation
    presenter.on_run_simulation();
    // assert — the netlist handed to Xyce does not carry the FILE= option
    ASSERT_TRUE(view.m_started);
    {
        std::ifstream temp_netlist(view.m_started_netlist_path);
        const std::string content((std::istreambuf_iterator<char>(temp_netlist)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content.find("FILE=probe_user_out.csd"), std::string::npos);
        EXPECT_NE(content.find(".PRINT TRAN FORMAT=PROBE V(1)"), std::string::npos);
    }
    // the editor keeps the user-facing directive with the FILE= option intact
    EXPECT_NE(view.m_editor_content.find("FILE=probe_user_out.csd"), std::string::npos);
    // simulate Xyce producing the csd file next to the temporary netlist
    const auto produced_path = view.m_started_netlist_path.string() + ".csd";
    {
        std::ofstream produced(produced_path, std::ios::out | std::ios::trunc);
        produced << "#H\n";
        produced << "SOURCE='Xyce' VERSION='7.10'\n";
        produced << "TITLE='* Probe Copy Test'\n";
        produced << "SUBTITLE='Xyce data'\n";
        produced << "ANALYSIS='Transient Analysis' SERIALNO='12345'\n";
        produced << "ALLVALUES='NO' COMPLEXVALUES='NO' NODES='1'\n";
        produced << "SWEEPVAR='Time' SWEEPMODE='VAR_STEP'\n";
        produced << "#N\n";
        produced << "'V(1)'\n";
        produced << "#C 0.000000000e+00 1\n";
        produced << "1.000000000e+00:1\n";
        produced << "#;\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the produced file was copied to the user-indicated location
    const auto copied_path = working_directory / "probe_user_out.csd";
    ASSERT_TRUE(std::filesystem::exists(copied_path));
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(produced_path, ec);
    std::filesystem::remove(copied_path, ec);
    std::filesystem::remove_all(working_directory, ec);
}

TEST(SlintMainWindowPresenterChecks, simulation_finished_success_hides_output_panel) {
    // arrange — launch a transient simulation
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=RAW V(1)\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write an ascii raw file at the expected output location
    const auto raw_path = view.m_started_netlist_path.string() + ".raw";
    {
        std::ofstream raw_file(raw_path, std::ios::out | std::ios::trunc);
        raw_file << "Title: Presenter Test\n";
        raw_file << "Plotname: Transient Analysis\n";
        raw_file << "Flags: real\n";
        raw_file << "No. Variables: 2\n";
        raw_file << "No. Points: 3\n";
        raw_file << "Variables:\n";
        raw_file << "\t0\ttime\ttime\n";
        raw_file << "\t1\tV(1)\tvoltage\n";
        raw_file << "Values:\n";
        raw_file << " 0  0.0  1.0\n";
        raw_file << " 1  0.001  2.0\n";
        raw_file << " 2  0.002  3.0\n";
    }
    // expose the output panel so we can later assert it was hidden
    view.m_output_panel_hidden = false;
    // act
    presenter.on_simulation_finished(0, false);
    // assert — success hides the output panel and shows charts
    EXPECT_TRUE(view.m_output_panel_hidden);
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(raw_path, ec);
}

TEST(SlintMainWindowPresenterChecks, simulation_finished_raw_file_not_found_shows_output_panel) {
    // arrange — launch a transient simulation but do NOT create the raw file
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // act — simulation finishes successfully but raw file is missing
    presenter.on_simulation_finished(0, false);
    // assert — output panel shown so user can diagnose
    EXPECT_FALSE(view.m_output_panel_hidden);
    EXPECT_EQ(view.m_status_text, "Simulation finished but output file could not be found");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, lin_run_appends_touchstone_tab_and_keeps_primary_active) {
    // arrange — launch a LIN simulation first so the run paths are known
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.AC DEC 10 1 100k\n.LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=lin-presenter-test.s2p\n.PRINT AC FORMAT=RAW V(*) I(*)\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write an ascii raw file at the expected output location
    const auto raw_path = view.m_started_netlist_path.string() + ".raw";
    {
        std::ofstream raw_file(raw_path, std::ios::out | std::ios::trunc);
        raw_file << "Title: Presenter Test Circuit\n";
        raw_file << "Plotname: AC Analysis\n";
        raw_file << "Flags: real\n";
        raw_file << "No. Variables: 2\n";
        raw_file << "No. Points: 3\n";
        raw_file << "Variables:\n";
        raw_file << "\t0\tfrequency\tfrequency\n";
        raw_file << "\t1\tV(1)\tvoltage\n";
        raw_file << "Values:\n";
        raw_file << " 0  0.0  1.0\n";
        raw_file << " 1  0.001  2.0\n";
        raw_file << " 2  0.002  3.0\n";
    }
    // arrange — write the touchstone output the LIN run produces (FILE= resolves against the working directory)
    const auto s2p_path = view.m_started_working_directory / "lin-presenter-test.s2p";
    {
        std::ofstream s2p_file(s2p_path, std::ios::out | std::ios::trunc);
        s2p_file << "# Hz S RI R 50\n";
        s2p_file << "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n";
        s2p_file << "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n";
    }
    // act
    presenter.on_simulation_finished(0, false);
    // assert — the raw file stays the primary dataset, the touchstone file opens as a non-closable tab and the smith chart tab follows it; the primary tab is active
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    ASSERT_TRUE(presenter.s_parameter_measurements().has_value());
    ASSERT_EQ(view.m_plot_tabs.size(), 3u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "AC Analysis");
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_EQ(view.m_plot_tabs[1].title, "LIN Analysis");
    EXPECT_FALSE(view.m_plot_tabs[1].closable);
    EXPECT_EQ(view.m_plot_tabs[2].title, "Smith Chart");
    EXPECT_FALSE(view.m_plot_tabs[2].closable);
    EXPECT_EQ(view.m_active_plot_tab, 0);
    // the activated dataset is the primary one
    EXPECT_EQ(view.m_updated_dataset_ids.back(), view.m_plot_tabs[0].id);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(raw_path, ec);
    std::filesystem::remove(s2p_path, ec);
}

TEST(SlintMainWindowPresenterChecks, lin_run_without_raw_file_loads_touchstone_as_primary) {
    // arrange — launch a LIN simulation whose netlist carries no .PRINT AC, so the run produces no raw output at all
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.AC DEC 10 1 100k\n.LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=lin-presenter-test.s2p\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write only the touchstone output the LIN run produces
    const auto s2p_path = view.m_started_working_directory / "lin-presenter-test.s2p";
    {
        std::ofstream s2p_file(s2p_path, std::ios::out | std::ios::trunc);
        s2p_file << "# Hz S RI R 50\n";
        s2p_file << "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n";
        s2p_file << "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n";
    }
    // act
    presenter.on_simulation_finished(0, false);
    // assert — the touchstone file alone becomes the primary (non-closable) dataset followed by the smith chart tab, and the run reports success
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    EXPECT_TRUE(view.m_output_panel_hidden);
    ASSERT_TRUE(presenter.s_parameter_measurements().has_value());
    ASSERT_EQ(view.m_plot_tabs.size(), 2u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "LIN Analysis");
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_EQ(view.m_plot_tabs[1].title, "Smith Chart");
    EXPECT_FALSE(view.m_plot_tabs[1].closable);
    EXPECT_EQ(view.m_active_plot_tab, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(s2p_path, ec);
}

TEST(SlintMainWindowPresenterChecks, lin_smith_tab_builds_smith_charts_with_diagonal_entries) {
    // arrange — launch a LIN simulation first so the run paths are known
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.AC DEC 10 1 100k\n.LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=lin-presenter-test.s2p\n.PRINT AC FORMAT=RAW V(*) I(*)\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write an ascii raw file and the touchstone output
    const auto raw_path = view.m_started_netlist_path.string() + ".raw";
    {
        std::ofstream raw_file(raw_path, std::ios::out | std::ios::trunc);
        raw_file << "Title: Presenter Test Circuit\n";
        raw_file << "Plotname: AC Analysis\n";
        raw_file << "Flags: real\n";
        raw_file << "No. Variables: 2\n";
        raw_file << "No. Points: 3\n";
        raw_file << "Variables:\n";
        raw_file << "\t0\tfrequency\tfrequency\n";
        raw_file << "\t1\tV(1)\tvoltage\n";
        raw_file << "Values:\n";
        raw_file << " 0  0.0  1.0\n";
        raw_file << " 1  0.001  2.0\n";
        raw_file << " 2  0.002  3.0\n";
    }
    const auto s2p_path = view.m_started_working_directory / "lin-presenter-test.s2p";
    {
        std::ofstream s2p_file(s2p_path, std::ios::out | std::ios::trunc);
        s2p_file << "# Hz S RI R 50\n";
        s2p_file << "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n";
        s2p_file << "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n";
    }
    // act — finish the run, then switch to the Smith Chart tab
    presenter.on_simulation_finished(0, false);
    ASSERT_EQ(view.m_plot_tabs.size(), 3u);
    const int smith_dataset_id = view.m_plot_tabs[2].id;
    presenter.on_select_plot_tab(2);
    // assert — the last update targeted the smith dataset with the smith flag set so the renderer creates smith-kind charts
    EXPECT_EQ(view.m_updated_dataset_ids.back(), smith_dataset_id);
    ASSERT_FALSE(view.m_updated_smith_datasets.empty());
    EXPECT_TRUE(view.m_updated_smith_datasets.back());
    // assert — the smith tab is active so the cartesian-only chart tools are hidden in the panel (zoom, chart management, new window)
    EXPECT_TRUE(view.m_last_enablement.charts_smith);
    // act — switch back to the LIN Analysis (xy) tab
    presenter.on_select_plot_tab(1);
    // assert — the cartesian tools come back
    EXPECT_FALSE(view.m_last_enablement.charts_smith);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(raw_path, ec);
    std::filesystem::remove(s2p_path, ec);
}

TEST(SlintMainWindowPresenterChecks, lin_rerun_replaces_the_touchstone_dataset) {
    // arrange — launch a LIN simulation first so the run paths are known
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.AC DEC 10 1 100k\n.LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=lin-presenter-test.s2p\n.PRINT AC FORMAT=RAW V(*) I(*)\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write an ascii raw file and the touchstone output
    const auto raw_path = view.m_started_netlist_path.string() + ".raw";
    {
        std::ofstream raw_file(raw_path, std::ios::out | std::ios::trunc);
        raw_file << "Title: Presenter Test Circuit\n";
        raw_file << "Plotname: AC Analysis\n";
        raw_file << "Flags: real\n";
        raw_file << "No. Variables: 2\n";
        raw_file << "No. Points: 3\n";
        raw_file << "Variables:\n";
        raw_file << "\t0\tfrequency\tfrequency\n";
        raw_file << "\t1\tV(1)\tvoltage\n";
        raw_file << "Values:\n";
        raw_file << " 0  0.0  1.0\n";
        raw_file << " 1  0.001  2.0\n";
        raw_file << " 2  0.002  3.0\n";
    }
    const auto s2p_path = view.m_started_working_directory / "lin-presenter-test.s2p";
    {
        std::ofstream s2p_file(s2p_path, std::ios::out | std::ios::trunc);
        s2p_file << "# Hz S RI R 50\n";
        s2p_file << "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n";
        s2p_file << "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n";
    }
    // act — finish the first run
    presenter.on_simulation_finished(0, false);
    const int lin_tab_id = view.m_plot_tabs[1].id;
    const int smith_tab_id = view.m_plot_tabs[2].id;
    // act — finish a second run of the same netlist (files still in place)
    presenter.on_simulation_finished(0, false);
    // assert — the touchstone and smith datasets of the first run were released and fresh ones appended, while the primary dataset kept its identity
    ASSERT_EQ(view.m_plot_tabs.size(), 3u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "AC Analysis");
    EXPECT_EQ(view.m_plot_tabs[1].title, "LIN Analysis");
    EXPECT_EQ(view.m_plot_tabs[2].title, "Smith Chart");
    EXPECT_NE(view.m_plot_tabs[1].id, lin_tab_id);
    EXPECT_NE(view.m_plot_tabs[2].id, smith_tab_id);
    ASSERT_EQ(view.m_released_dataset_ids.size(), 2u);
    EXPECT_EQ(view.m_released_dataset_ids[0], lin_tab_id);
    EXPECT_EQ(view.m_released_dataset_ids[1], smith_tab_id);
    EXPECT_EQ(view.m_active_plot_tab, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(raw_path, ec);
    std::filesystem::remove(s2p_path, ec);
}

TEST(SlintMainWindowPresenterChecks, simulation_rerun_keeps_primary_dataset_identity) {
    // arrange — launch a transient simulation first so the run paths are known
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=RAW V(1)\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write an ascii raw file at the expected output location
    const auto raw_path = view.m_started_netlist_path.string() + ".raw";
    {
        std::ofstream raw_file(raw_path, std::ios::out | std::ios::trunc);
        raw_file << "Title: Presenter Test Circuit\n";
        raw_file << "Plotname: Transient Analysis\n";
        raw_file << "Flags: real\n";
        raw_file << "No. Variables: 2\n";
        raw_file << "No. Points: 3\n";
        raw_file << "Variables:\n";
        raw_file << "\t0\ttime\ttime\n";
        raw_file << "\t1\tV(1)\tvoltage\n";
        raw_file << "Values:\n";
        raw_file << " 0  0.0  1.0\n";
        raw_file << " 1  0.001  2.0\n";
        raw_file << " 2  0.002  3.0\n";
    }
    // expose the output panel so we can assert it gets hidden on success
    view.m_output_panel_hidden = false;
    // act — finish the first run
    presenter.on_simulation_finished(0, false);
    const int primary_id = view.m_updated_dataset_ids.back();
    // assert — first finish hides the output panel on success
    EXPECT_TRUE(view.m_output_panel_hidden);
    // act — finish a second run of the same netlist
    presenter.on_simulation_finished(0, false);
    // assert — the primary dataset kept its identity so the renderer re-points its charts instead of rebuilding, and no chart state was released
    EXPECT_EQ(view.m_updated_dataset_ids.back(), primary_id);
    EXPECT_TRUE(view.m_released_dataset_ids.empty());
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_active_plot_tab, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(raw_path, ec);
}

// ========================================================================================
// simulation output forwarding
// ========================================================================================

TEST(SlintMainWindowPresenterChecks, simulation_stdout_is_appended_to_output) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_simulation_stdout("line one");
    // assert
    ASSERT_EQ(view.m_output_lines.size(), 1);
    EXPECT_EQ(view.m_output_lines[0], "line one");
}

TEST(SlintMainWindowPresenterChecks, simulation_stderr_updates_output_and_status) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_simulation_stderr("boom");
    // assert
    ASSERT_EQ(view.m_output_lines.size(), 1);
    EXPECT_EQ(view.m_output_lines[0], "boom");
    EXPECT_EQ(view.m_status_text, "Simulation error: boom");
}

// ========================================================================================
// view switching
// ========================================================================================

TEST(SlintMainWindowPresenterChecks, view_switching_forwards_to_view_and_refreshes_states) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_show_netlist();
    // assert
    EXPECT_TRUE(view.m_netlist_view_shown);
    // act
    presenter.on_show_charts();
    // assert — the charts view became visible over the netlist editor
    EXPECT_TRUE(view.charts_shown());
    // act
    presenter.on_show_netlist();
    // assert — the netlist editor hides the charts again
    EXPECT_FALSE(view.charts_shown());
    // act
    presenter.on_show_simulation_output();
    // assert
    EXPECT_FALSE(view.m_output_panel_hidden);
    // act
    presenter.on_close_simulation_output();
    // assert
    EXPECT_TRUE(view.m_output_panel_hidden);
}

// ========================================================================================
// netlist editing lifecycle
// ========================================================================================

namespace
{
    // write a file with the given content
    void write_file(const std::filesystem::path& path, const std::string& content) {
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << content;
    }
} // namespace

TEST(SlintMainWindowPresenterChecks, open_cir_file_loads_editor_content) {
    // arrange
    const auto netlist_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_cir";
    std::filesystem::create_directories(netlist_dir);
    const auto netlist_path = netlist_dir / "demo.cir";
    write_file(netlist_path, "V1 1 0 5\nR1 1 0 1K\n.END\n");
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", netlist_dir), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file(netlist_path);
    // assert — the editor is editable, holds the file content and the title tracks the file
    EXPECT_FALSE(view.m_editor_read_only);
    EXPECT_NE(view.m_editor_content.find(".END"), std::string::npos);
    EXPECT_EQ(view.m_title, "demo.cir");
    EXPECT_TRUE(view.m_netlist_view_shown);
    // cleanup
    std::error_code ec;
    std::filesystem::remove_all(netlist_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, open_raw_extension_ignores_missing_file) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file("/nonexistent/presenter_missing.raw");
    // assert — parse failure leaves the window untouched
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    EXPECT_FALSE(view.m_charts_view_shown);
}

TEST(SlintMainWindowPresenterChecks, open_csd_extension_loads_analysis_measurements) {
    // arrange — a real TRAN csd file produced by the probe outputter
    const auto csd_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_csd";
    std::filesystem::create_directories(csd_dir);
    const auto csd_path = csd_dir / "demo.csd";
    write_file(csd_path, "#H\nSOURCE='Xyce' VERSION='7.10'\nTITLE='* demo.cir'\nSUBTITLE='Xyce data'\nANALYSIS='Transient Analysis' SERIALNO='12345'\nCOMPLEXVALUES='NO' NODES='1'\nSWEEPVAR='Time' SWEEPMODE='VAR_STEP'\n#N\n'V(1)'\n#C 0.000000000e+00 1\n1.000000000e+00:1\n#C 1.000000000e-09 1\n1.100000000e+00:1\n#;\n");
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", csd_dir), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file(csd_path);
    // assert — the csd file parsed into the analysis measurements and charts are shown
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_EQ(view.m_title, "demo.cir");
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_GT(view.m_update_charts_count, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(csd_path, ec);
    std::filesystem::remove_all(csd_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, open_csd_extension_ignores_missing_file) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file("/nonexistent/presenter_missing.TD.csd");
    // assert — parse failure leaves the window untouched
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    EXPECT_FALSE(view.m_charts_view_shown);
}

TEST(SlintMainWindowPresenterChecks, open_csv_extension_loads_analysis_measurements) {
    // arrange — a real TRAN csv file produced by the FORMAT=CSV outputter
    const auto csv_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_csv";
    std::filesystem::create_directories(csv_dir);
    const auto csv_path = csv_dir / "demo.csv";
    write_file(csv_path, "TIME,V(1)\n0.0,1.0\n1e-9,1.1\n2e-9,1.2\n");
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", csv_dir), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file(csv_path);
    // assert — the csv file parsed into the analysis measurements and charts are shown
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_GT(view.m_update_charts_count, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(csv_path, ec);
    std::filesystem::remove_all(csv_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, open_csv_extension_ignores_missing_file) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file("/nonexistent/presenter_missing.csv");
    // assert — parse failure leaves the window untouched
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    EXPECT_FALSE(view.m_charts_view_shown);
}

TEST(SlintMainWindowPresenterChecks, open_dat_extension_loads_analysis_measurements) {
    // arrange — a real TRAN tecplot file produced by the FORMAT=TECPLOT outputter
    const auto dat_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_dat";
    std::filesystem::create_directories(dat_dir);
    const auto dat_path = dat_dir / "demo.dat";
    write_file(dat_path, "TITLE = \"demo.cir - Xyce Electrical Simulator\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"demo.cir \" \n0.0 1.0\n1e-9 1.1\n2e-9 1.2\nEnd of Xyce(TM) Simulation\n");
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", dat_dir), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file(dat_path);
    // assert — the dat file parsed into the analysis measurements and charts are shown
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_GT(view.m_update_charts_count, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(dat_path, ec);
    std::filesystem::remove_all(dat_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, open_dat_extension_ignores_missing_file) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file("/nonexistent/presenter_missing.dat");
    // assert — parse failure leaves the window untouched
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    EXPECT_FALSE(view.m_charts_view_shown);
}

TEST(SlintMainWindowPresenterChecks, open_raw_extension_loads_analysis_measurements) {
    // arrange — a parseable transient raw file
    const auto raw_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_raw";
    std::filesystem::create_directories(raw_dir);
    const auto raw_path = raw_dir / "demo.raw";
    {
        std::ofstream raw_file(raw_path, std::ios::binary);
        raw_file << "Title: Test\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nBinary:\n";
        raw_file.write("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32);
    }
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", raw_dir), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file(raw_path);
    // assert — the raw file parsed into the analysis measurements and charts are shown
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_GT(view.m_update_charts_count, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(raw_path, ec);
    std::filesystem::remove_all(raw_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, open_prn_extension_loads_analysis_measurements) {
    // arrange — a parseable transient prn file
    const auto prn_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_prn";
    std::filesystem::create_directories(prn_dir);
    const auto prn_path = prn_dir / "demo.prn";
    write_file(prn_path, "TIME V(1)\n0.0 1.0\n1e-9 1.1\n.\n");
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", prn_dir), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file(prn_path);
    // assert — the prn file parsed into the analysis measurements and charts are shown
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_GT(view.m_update_charts_count, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(prn_path, ec);
    std::filesystem::remove_all(prn_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, open_prn_extension_ignores_unparseable_file) {
    // arrange — an existing prn file without a parsable table
    const auto prn_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_prn_bad";
    std::filesystem::create_directories(prn_dir);
    const auto prn_path = prn_dir / "broken.prn";
    write_file(prn_path, "not a print table\n");
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", prn_dir), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file(prn_path);
    // assert — parse failure leaves the window untouched
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    EXPECT_FALSE(view.m_charts_view_shown);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(prn_path, ec);
    std::filesystem::remove_all(prn_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, open_unsupported_extension_is_ignored) {
    // arrange — a file the dispatch chain knows no parser for
    const auto other_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_other";
    std::filesystem::create_directories(other_dir);
    const auto other_path = other_dir / "notes.txt";
    write_file(other_path, "hello\n");
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", other_dir), PluginConfig(""), nullptr);
    // act
    presenter.on_open_xyce_file(other_path);
    // assert — nothing loads and the editor state is left untouched
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    EXPECT_FALSE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_title, "");
    EXPECT_TRUE(view.m_editor_read_only);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(other_path, ec);
    std::filesystem::remove_all(other_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, missing_table_output_finishes_without_dataset) {
    // arrange — a FORMAT=CSV run that produced no file at all
    RecordingView view;
    const std::string netlist_content = "V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // act — finish successfully although the expected .csv file was never written
    presenter.on_simulation_finished(0, false);
    // assert — the status reports the missing output and no dataset was installed
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    EXPECT_EQ(view.m_status_text, "Simulation finished but output file could not be found");
    EXPECT_FALSE(view.m_output_panel_hidden);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, missing_csd_output_finishes_without_dataset) {
    // arrange — a PROBE run that produced no .csd file
    RecordingView view;
    const std::string netlist_content = "V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=PROBE V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // act — finish successfully although the expected .csd file was never written
    presenter.on_simulation_finished(0, false);
    // assert — the status reports the missing output and no dataset was installed
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    EXPECT_EQ(view.m_status_text, "Simulation finished but output file could not be found");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, editor_modified_marks_dirty_and_enables_save) {
    // arrange — open a .cir file so a base title exists
    const auto netlist_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_dirty";
    std::filesystem::create_directories(netlist_dir);
    const auto netlist_path = netlist_dir / "demo.cir";
    write_file(netlist_path, "V1 1 0 5\nR1 1 0 1K\n.END\n");
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", netlist_dir), PluginConfig(""), nullptr);
    presenter.on_open_xyce_file(netlist_path);
    // act — simulate user edits in the editor
    view.m_editor_content = "V1 1 0 6\nR1 1 0 2K\n.END\n";
    presenter.on_netlist_editor_modified();
    // assert — dirty marker prefixes the title and save becomes available
    EXPECT_EQ(view.m_title, "* demo.cir");
    EXPECT_TRUE(view.m_last_enablement.save);
    // cleanup
    std::error_code ec;
    std::filesystem::remove_all(netlist_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, save_netlist_clears_dirty_state_and_writes_the_file) {
    // arrange — open a .cir file and dirty it
    const auto netlist_dir = std::filesystem::temp_directory_path() / "kicad_xyce_presenter_save";
    std::filesystem::create_directories(netlist_dir);
    const auto netlist_path = netlist_dir / "demo.cir";
    write_file(netlist_path, "V1 1 0 5\nR1 1 0 1K\n.END\n");
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", netlist_dir), PluginConfig(""), nullptr);
    presenter.on_open_xyce_file(netlist_path);
    view.m_editor_content = "V1 1 0 6\nR1 1 0 2K\n.END\n";
    presenter.on_netlist_editor_modified();
    // act
    presenter.on_save_netlist();
    // assert — the live editor text was written back to the file and the dirty marker cleared
    std::ifstream saved(netlist_path);
    const std::string saved_content((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    EXPECT_EQ(saved_content, "V1 1 0 6\nR1 1 0 2K\n.END\n");
    EXPECT_EQ(view.m_title, "demo.cir");
    EXPECT_FALSE(view.m_last_enablement.save);
    // cleanup
    std::error_code ec;
    std::filesystem::remove_all(netlist_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, save_untitled_netlist_requests_a_path_and_persists) {
    // arrange — an untitled source (no backing file) with typed editor content
    const auto save_dir = std::filesystem::temp_directory_path() / "xyce_presenter_save_as";
    std::filesystem::create_directories(save_dir);
    const auto target = save_dir / "typed-from-scratch.cir";
    RecordingView view;
    auto source = std::make_unique<StubNetlistSource>("", save_dir);
    source->m_has_backing_file = false;
    SlintMainWindowPresenter presenter(view, std::move(source), PluginConfig(""), nullptr);
    view.m_editor_content = "V1 1 0 5\nR1 1 0 1K\n.END\n";
    presenter.on_netlist_editor_modified();
    view.m_save_path_result = target;
    // act
    presenter.on_save_netlist();
    // assert — the netlist was persisted at the chosen location
    std::ifstream saved(target);
    const std::string saved_content((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    EXPECT_EQ(saved_content, view.m_editor_content);
    // assert — the chosen file became the window title and save is clean
    EXPECT_EQ(view.m_title, "typed-from-scratch.cir");
    EXPECT_FALSE(view.m_last_enablement.save);
    // cleanup
    std::error_code ec;
    std::filesystem::remove_all(save_dir, ec);
}

TEST(SlintMainWindowPresenterChecks, extract_schematic_netlist_loads_readonly_editor) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("V1 1 0 5\nR1 1 0 1K\n.END\n", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_extract_schematic_netlist();
    // assert
    EXPECT_TRUE(view.m_editor_read_only);
    EXPECT_NE(view.m_editor_content.find("R1"), std::string::npos);
    EXPECT_TRUE(view.m_netlist_view_shown);
}

// ========================================================================================
// plugin configuration flow
// ========================================================================================

TEST(SlintMainWindowPresenterChecks, configure_plugin_requests_dialog_and_stores_result) {
    // arrange
    RecordingView view;
    StubNetlistSource* source = new StubNetlistSource("V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.END\n", std::filesystem::temp_directory_path());
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(""), nullptr);
    // act — request the dialog, the recording view reports no result yet
    presenter.on_configure_plugin();
    // act — deliver an accepted configuration with a valid executable
    const PluginConfig accepted(testing::internal::GetArgvs()[0]);
    presenter.on_plugin_config_dialog_result(accepted);
    // assert — the stored config is now valid, so a run proceeds past validation
    presenter.on_run_simulation();
    EXPECT_TRUE(view.m_started);
    EXPECT_EQ(view.m_started_program, testing::internal::GetArgvs()[0]);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

// ========================================================================================
// raw file loading and chart actions
// ========================================================================================

namespace
{
    // build a single-step real raw file for chart interactions
    std::shared_ptr<XyceOutputFile> make_raw_file() {
        std::vector<double> abscissa_data = {0.0, 0.001, 0.002};
        std::vector<double> voltage_data = {1.0, 2.0, 3.0};
        std::vector<std::pair<size_t, size_t>> step_slices = {{0, 3}};
        std::vector<AnyExpression> expressions;
        expressions.emplace_back(Expression<double>("time", std::move(abscissa_data), step_slices, "s"));
        expressions.emplace_back(Expression<double>("V(1)", std::move(voltage_data), step_slices, "V"));
        ExpressionManager expression_manager(expressions, step_slices);
        StepInformation step_information({"time"}, {{}}, {{0.0, 0.002}});
        return std::make_shared<XyceOutputFile>("", "Loaded Circuit", false, std::move(step_information), PlotType::TRANSIENT, AbscissaScale::LINEAR, std::move(expression_manager), nullptr);
    }
} // namespace

TEST(SlintMainWindowPresenterChecks, load_analysis_measurements_switches_to_charts_view) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.load_analysis_measurements(make_raw_file());
    // assert
    EXPECT_EQ(view.m_update_charts_count, 1);
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_title, "Loaded Circuit");
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
}

TEST(SlintMainWindowPresenterChecks, chart_actions_are_guarded_without_raw_file) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_chart_calculate_fft(0);
    presenter.on_chart_step_tool(0);
    presenter.on_chart_new_window(0);
    // assert — nothing was opened or spawned
    EXPECT_FALSE(view.m_fft_dialog_index.has_value());
    EXPECT_FALSE(view.m_step_tool_dialog_index.has_value());
    EXPECT_TRUE(view.m_spawned_files.empty());
}

TEST(SlintMainWindowPresenterChecks, chart_actions_delegate_with_loaded_raw_file) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    presenter.load_analysis_measurements(make_raw_file());
    // act
    presenter.on_chart_calculate_fft(2);
    presenter.on_chart_step_tool(1);
    presenter.on_chart_new_window(7);
    // assert — dialogs target the requested chart and the raw file spawns a window
    EXPECT_EQ(view.m_fft_dialog_index, 2u);
    EXPECT_EQ(view.m_step_tool_dialog_index, 1u);
    ASSERT_EQ(view.m_spawned_files.size(), 1);
}

TEST(SlintMainWindowPresenterChecks, chart_move_routes_to_the_view_with_a_loaded_raw_file) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    presenter.load_analysis_measurements(make_raw_file());
    // act
    presenter.on_chart_moved(2, 0);
    // assert — the move reached the view with the requested indexes
    ASSERT_EQ(view.m_moved_from.size(), 1u);
    EXPECT_EQ(view.m_moved_from[0], 2u);
    ASSERT_EQ(view.m_moved_to.size(), 1u);
    EXPECT_EQ(view.m_moved_to[0], 0u);
}

TEST(SlintMainWindowPresenterChecks, chart_move_is_ignored_without_raw_file) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_chart_moved(1, 0);
    // assert — nothing reached the view
    EXPECT_TRUE(view.m_moved_from.empty());
    EXPECT_TRUE(view.m_moved_to.empty());
}

TEST(SlintMainWindowPresenterChecks, load_analysis_measurements_populates_plot_tabs) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.load_analysis_measurements(make_raw_file());
    // assert — the raw file became the primary, non-closable, active tab
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_EQ(view.m_active_plot_tab, 0);
    EXPECT_TRUE(view.m_charts_view_shown);
}

TEST(SlintMainWindowPresenterChecks, select_plot_tab_validates_index) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    presenter.load_analysis_measurements(make_raw_file());
    const int charts_before = view.m_update_charts_count;
    // act — out of bounds and already-active selections are ignored
    presenter.on_select_plot_tab(3);
    presenter.on_select_plot_tab(0);
    // assert — no chart update was triggered
    EXPECT_EQ(view.m_update_charts_count, charts_before);
    EXPECT_EQ(view.m_active_plot_tab, 0);
}

TEST(SlintMainWindowPresenterChecks, fft_dialog_result_adds_closable_plot_tab) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    presenter.load_analysis_measurements(make_raw_file());
    // act — compute an FFT over the full abscissa range of the loaded raw file
    auto expressions = presenter.analysis_measurements().value()->expression_manager().expressions();
    const fft::FftParameters parameters{
        .np = 4,
        .window = fft::WindowFunction::RECTANGULAR,
        .format = fft::FftFormat::NORM,
        .start = 0.0,
        .stop = 0.002,
        .output = fft::FftOutput::MAGNITUDE,
        .keep_dc = true,
    };
    presenter.on_fft_dialog_result({expressions[1]}, parameters);
    // assert — a second closable tab was appended and activated
    ASSERT_EQ(view.m_plot_tabs.size(), 2u);
    EXPECT_EQ(view.m_plot_tabs[1].title, "FFT: RECTANGULAR, 0–1500 Hz");
    EXPECT_TRUE(view.m_plot_tabs[1].closable);
    EXPECT_EQ(view.m_active_plot_tab, 1);
    EXPECT_TRUE(view.m_charts_view_shown);
    // act — close the FFT tab
    presenter.on_close_plot_tab(1);
    // assert — the primary tab remains and stays active
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_active_plot_tab, 0);
    // act — the non-closable primary tab cannot be closed
    presenter.on_close_plot_tab(0);
    // assert
    EXPECT_EQ(view.m_plot_tabs.size(), 1u);
}

TEST(SlintMainWindowPresenterChecks, tab_switch_preserves_dataset_chart_state) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    presenter.load_analysis_measurements(make_raw_file());
    const int primary_id = view.m_updated_dataset_ids.back();
    // arrange — create a second dataset through an interactive FFT
    auto expressions = presenter.analysis_measurements().value()->expression_manager().expressions();
    const fft::FftParameters parameters{
        .np = 4,
        .window = fft::WindowFunction::RECTANGULAR,
        .format = fft::FftFormat::NORM,
        .start = 0.0,
        .stop = 0.002,
        .output = fft::FftOutput::MAGNITUDE,
        .keep_dc = true,
    };
    presenter.on_fft_dialog_result({expressions[1]}, parameters);
    const int fft_id = view.m_updated_dataset_ids.back();
    ASSERT_NE(primary_id, fft_id);
    // act — switch back to the primary tab
    presenter.on_select_plot_tab(0);
    // assert — the primary dataset was re-activated and no chart state was released
    EXPECT_EQ(view.m_updated_dataset_ids.back(), primary_id);
    EXPECT_TRUE(view.m_released_dataset_ids.empty());
    // act — close the FFT tab
    presenter.on_close_plot_tab(1);
    // assert — only the FFT chart state was released
    ASSERT_EQ(view.m_released_dataset_ids.size(), 1u);
    EXPECT_EQ(view.m_released_dataset_ids.back(), fft_id);
}

TEST(SlintMainWindowPresenterChecks, prn_output_tab_label_matches_analysis_type) {
    // arrange — launch a dc sweep simulation with a std-format print directive
    RecordingView view;
    const std::string netlist_content = "V1 1 0 DC 0V\nR1 1 2 1k\nR2 2 0 2k\n.DC V1 0 5 0.5\n.PRINT DC FORMAT=STD V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write a .prn file at the expected output location
    const auto prn_path = view.m_started_netlist_path.string() + ".prn";
    {
        // write a minimal std-format .prn file with an index column
        std::ofstream prn_file(prn_path, std::ios::out | std::ios::trunc);
        prn_file << "INDEX SWEEP V(1)\n";
        prn_file << "0 0.0 1.0\n";
        prn_file << "1 0.5 1.1\n";
        prn_file << "2 1.0 1.2\n";
        prn_file << ".\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the plot tab shows the analysis type name (dc sweep), same as the .raw behaviour, and the tab is not closable
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "DC Sweep");
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(prn_path, ec);
}

TEST(SlintMainWindowPresenterChecks, prn_default_format_produces_prn_file) {
    // arrange — launch a dc sweep with a print directive that has no explicit format; the default is STD which produces a .prn file
    RecordingView view;
    const std::string netlist_content = "V1 1 0 DC 0V\nR1 1 2 1k\nR2 2 0 2k\n.DC V1 0 5 0.5\n.PRINT DC V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write a .prn file at the expected output location
    const auto prn_path = view.m_started_netlist_path.string() + ".prn";
    {
        // write a minimal std-format .prn file
        std::ofstream prn_file(prn_path, std::ios::out | std::ios::trunc);
        prn_file << "INDEX SWEEP V(1)\n";
        prn_file << "0 0.0 1.0\n";
        prn_file << "1 0.5 1.1\n";
        prn_file << "2 1.0 1.2\n";
        prn_file << ".\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the plot tab shows the analysis type name (dc sweep)
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "DC Sweep");
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(prn_path, ec);
}

TEST(SlintMainWindowPresenterChecks, prn_ac_default_format_looks_for_fd_prn) {
    // arrange — launch an ac sweep with default-format print; Xyce produces <netlist>.FD.prn for AC analysis
    RecordingView view;
    const std::string netlist_content = "V1 1 0 AC 1\nR1 1 0 1K\n.AC DEC 10 1 100MEG\n.PRINT AC V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write a .prn file at the .FD.prn location
    const auto prn_path = view.m_started_netlist_path.string() + ".FD.prn";
    {
        std::ofstream prn_file(prn_path, std::ios::out | std::ios::trunc);
        prn_file << "INDEX FREQ V(1)\n";
        prn_file << "0 100 1.0\n";
        prn_file << "1 1000 1.1\n";
        prn_file << ".\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the plot tab shows the analysis type name (ac analysis)
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "AC Analysis");
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(prn_path, ec);
}

TEST(SlintMainWindowPresenterChecks, default_format_print_file_is_stripped_for_xyce_and_copied_on_finish) {
    // arrange — netlist with an AC analysis whose default-format print carries an output file
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path() / "xyce_studio_default_format_copy_test";
    std::filesystem::remove_all(working_directory);
    std::filesystem::create_directories(working_directory);
    StubNetlistSource* source = new StubNetlistSource("V1 IN 0 AC 1\nR1 IN N1 100\nL1 N1 N2 10mH\nC1 N2 0 1uF\n.AC LIN 20 1 100k\n.PRINT AC FILE=ac-simple-01.raw V(*) I(*)\n.END\n", working_directory);
    SlintMainWindowPresenter presenter(view, std::unique_ptr<StubNetlistSource>(source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    // act — launch the simulation
    presenter.on_run_simulation();
    // assert — the netlist handed to Xyce does not carry the FILE= option
    ASSERT_TRUE(view.m_started);
    {
        std::ifstream temp_netlist(view.m_started_netlist_path);
        const std::string content((std::istreambuf_iterator<char>(temp_netlist)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content.find("FILE=ac-simple-01.raw"), std::string::npos);
        EXPECT_NE(content.find(".PRINT AC V(*) I(*)"), std::string::npos);
    }
    // the editor keeps the user-facing directive with the FILE= option intact
    EXPECT_NE(view.m_editor_content.find("FILE=ac-simple-01.raw"), std::string::npos);
    // simulate Xyce producing the default frequency-domain prn file next to the temporary netlist
    const auto produced_path = view.m_started_netlist_path.string() + ".FD.prn";
    const std::string payload = "INDEX FREQ V(N2) I(V1)\n0 100 1.0 0.5\n1 1000 0.9 0.4\n.\n";
    {
        std::ofstream produced(produced_path, std::ios::out | std::ios::trunc);
        produced << payload;
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the default prn file opened in the charts instead of the recorded FILE= destination
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    EXPECT_TRUE(view.m_charts_view_shown);
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "AC Analysis");
    // assert — the produced prn file was copied to the recorded FILE= destination
    const auto copied_path = working_directory / "ac-simple-01.raw";
    ASSERT_TRUE(std::filesystem::exists(copied_path));
    {
        std::ifstream copied(copied_path);
        const std::string copied_content((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
        EXPECT_EQ(copied_content, payload);
    }
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(produced_path, ec);
    std::filesystem::remove(copied_path, ec);
    std::filesystem::remove_all(working_directory, ec);
}

TEST(SlintMainWindowPresenterChecks, prn_tran_default_format_looks_for_prn) {
    // arrange — launch a transient analysis with default-format print
    RecordingView view;
    const std::string netlist_content = "V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write a .prn file at the expected location
    const auto prn_path = view.m_started_netlist_path.string() + ".prn";
    {
        std::ofstream prn_file(prn_path, std::ios::out | std::ios::trunc);
        prn_file << "INDEX TIME V(1)\n";
        prn_file << "0 0.0 1.0\n";
        prn_file << "1 1e-9 1.1\n";
        prn_file << ".\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the plot tab shows the analysis type name (transient)
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(prn_path, ec);
}

TEST(SlintMainWindowPresenterChecks, prn_noise_default_format_looks_for_prn) {
    // arrange — launch a noise analysis with default-format print
    RecordingView view;
    const std::string netlist_content = "V1 1 0 AC 1\nR1 1 0 1K\n.NOISE V(1) V1 DEC 10 1 100MEG\n.PRINT NOISE V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write a .prn file at the expected location
    const auto prn_path = view.m_started_netlist_path.string() + ".prn";
    {
        std::ofstream prn_file(prn_path, std::ios::out | std::ios::trunc);
        prn_file << "INDEX FREQ V(1)\n";
        prn_file << "0 100 1.0\n";
        prn_file << "1 1000 1.1\n";
        prn_file << ".\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the plot tab shows the analysis type name (noise analysis)
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Noise Analysis");
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(prn_path, ec);
}

TEST(SlintMainWindowPresenterChecks, prn_lin_default_format_looks_for_fd_prn) {
    // arrange — launch a lin analysis with default-format ac print; Xyce produces <netlist>.FD.prn for LIN/.PRINT AC (touchstone is separate)
    RecordingView view;
    const std::string netlist_content = "R1 1 2 50\nR2 2 0 50\n.LIN 1 100 10\n.PRINT AC SM(1,1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write a .prn file at the .FD.prn location
    const auto prn_path = view.m_started_netlist_path.string() + ".FD.prn";
    {
        std::ofstream prn_file(prn_path, std::ios::out | std::ios::trunc);
        prn_file << "INDEX FREQ SM(1,1)\n";
        prn_file << "0 100 0.5\n";
        prn_file << "1 1000 0.7\n";
        prn_file << ".\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the plot tab shows the analysis type name (ac analysis)
    ASSERT_GE(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "AC Analysis");
    EXPECT_TRUE(view.m_charts_view_shown);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(prn_path, ec);
}

TEST(SlintMainWindowPresenterChecks, csv_tran_default_format_looks_for_csv) {
    // arrange — launch a transient analysis with a FORMAT=CSV print
    RecordingView view;
    const std::string netlist_content = "V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — the print statement survives verbatim in the netlist handed to Xyce and Xyce writes <netlist>.csv next to the temporary netlist
    {
        std::ifstream started(view.m_started_netlist_path);
        const std::string started_content((std::istreambuf_iterator<char>(started)), std::istreambuf_iterator<char>());
        EXPECT_NE(started_content.find(".PRINT TRAN FORMAT=CSV V(1)"), std::string::npos);
    }
    const auto csv_path = view.m_started_netlist_path.string() + ".csv";
    {
        std::ofstream csv_file(csv_path, std::ios::out | std::ios::trunc);
        csv_file << "TIME,V(1)\n";
        csv_file << "0.0,1.0\n";
        csv_file << "1e-9,1.1\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the produced csv loaded as the transient dataset
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(csv_path, ec);
}

TEST(SlintMainWindowPresenterChecks, csv_ac_default_format_looks_for_fd_csv) {
    // arrange — launch an ac analysis with a FORMAT=CSV print; Xyce produces <netlist>.FD.csv for the frequency-domain output
    RecordingView view;
    const std::string netlist_content = "V1 1 0 AC 1\nR1 1 0 1K\n.AC DEC 10 1 100MEG\n.PRINT AC FORMAT=CSV V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write the frequency-domain csv file with a complex Re/Im pair
    const auto csv_path = view.m_started_netlist_path.string() + ".FD.csv";
    {
        std::ofstream csv_file(csv_path, std::ios::out | std::ios::trunc);
        csv_file << "FREQ,Re(V(1)),Im(V(1))\n";
        csv_file << "100,1.0,-0.5\n";
        csv_file << "1000,0.9,-0.4\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the produced csv loaded as the ac dataset
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "AC Analysis");
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_TRUE((*presenter.analysis_measurements())->is_complex());
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(csv_path, ec);
}

TEST(SlintMainWindowPresenterChecks, csv_print_file_is_stripped_for_xyce_and_copied_on_finish) {
    // arrange — launch a transient analysis with a FORMAT=CSV print carrying a FILE= destination, the option is removed from the netlist handed to Xyce so the run writes the default file for the format
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path() / "xyce_studio_csv_file_option_test";
    std::filesystem::remove_all(working_directory);
    std::filesystem::create_directories(working_directory);
    const std::string netlist_content = "V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=CSV FILE=my_results.csv V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, working_directory), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // assert — the netlist handed to Xyce does not carry the FILE= option
    {
        std::ifstream started(view.m_started_netlist_path);
        const std::string started_content((std::istreambuf_iterator<char>(started)), std::istreambuf_iterator<char>());
        EXPECT_EQ(started_content.find("FILE=my_results.csv"), std::string::npos);
        EXPECT_NE(started_content.find(".PRINT TRAN FORMAT=CSV V(1)"), std::string::npos);
    }
    // arrange — Xyce produced the default csv file next to the temporary netlist
    const auto csv_path = view.m_started_netlist_path.string() + ".csv";
    {
        std::ofstream csv_file(csv_path, std::ios::out | std::ios::trunc);
        csv_file << "TIME,V(1)\n";
        csv_file << "0.0,1.0\n";
        csv_file << "1e-9,1.1\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the default csv file loaded as the transient dataset
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // assert — the produced csv file was copied to the recorded FILE= destination
    ASSERT_TRUE(std::filesystem::exists(working_directory / "my_results.csv"));
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(csv_path, ec);
    std::filesystem::remove_all(working_directory, ec);
}

TEST(SlintMainWindowPresenterChecks, tecplot_tran_default_format_looks_for_dat) {
    // arrange — launch a transient analysis with a FORMAT=TECPLOT print
    RecordingView view;
    const std::string netlist_content = "V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=TECPLOT V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — the print statement survives verbatim in the netlist handed to Xyce and Xyce writes <netlist>.dat next to the temporary netlist
    {
        std::ifstream started(view.m_started_netlist_path);
        const std::string started_content((std::istreambuf_iterator<char>(started)), std::istreambuf_iterator<char>());
        EXPECT_NE(started_content.find(".PRINT TRAN FORMAT=TECPLOT V(1)"), std::string::npos);
    }
    const auto dat_path = view.m_started_netlist_path.string() + ".dat";
    {
        std::ofstream dat_file(dat_path, std::ios::out | std::ios::trunc);
        dat_file << "TITLE = \"demo.cir - Xyce Electrical Simulator\", \n";
        dat_file << "\tVARIABLES = \" TIME\" \n";
        dat_file << "\" V(1)\" \n";
        dat_file << "ZONE F=POINT T=\"demo.cir \" \n";
        dat_file << "0.0 1.0\n";
        dat_file << "1e-9 1.1\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the produced dat loaded as the transient dataset
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(dat_path, ec);
}

TEST(SlintMainWindowPresenterChecks, tecplot_ac_default_format_looks_for_fd_dat) {
    // arrange — launch an ac analysis with a FORMAT=TECPLOT print; Xyce produces <netlist>.FD.dat for the frequency-domain output
    RecordingView view;
    const std::string netlist_content = "V1 1 0 AC 1\nR1 1 0 1K\n.AC DEC 10 1 100MEG\n.PRINT AC FORMAT=TECPLOT V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write the frequency-domain dat file with a complex Re/Im pair
    const auto dat_path = view.m_started_netlist_path.string() + ".FD.dat";
    {
        std::ofstream dat_file(dat_path, std::ios::out | std::ios::trunc);
        dat_file << " TITLE = \" Xyce Frequency Domain data, demo.cir\", \n";
        dat_file << "\tVARIABLES = \" FREQ\" \n";
        dat_file << "\" Re(V(1))\" \n";
        dat_file << "\" Im(V(1))\" \n";
        dat_file << "ZONE F=POINT  T=\"Xyce data\" \n";
        dat_file << "1.000000000000e+02 1.0 -0.5\n";
        dat_file << "1.000000000000e+03 0.9 -0.4\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the produced dat loaded as the ac dataset
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "AC Analysis");
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    ASSERT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_TRUE((*presenter.analysis_measurements())->is_complex());
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(dat_path, ec);
}

TEST(SlintMainWindowPresenterChecks, tecplot_print_file_is_stripped_for_xyce_and_copied_on_finish) {
    // arrange — launch a transient analysis with a FORMAT=TECPLOT print carrying a FILE= destination, the option is removed from the netlist handed to Xyce so the run writes the default file for the format
    RecordingView view;
    const auto working_directory = std::filesystem::temp_directory_path() / "xyce_studio_tecplot_file_option_test";
    std::filesystem::remove_all(working_directory);
    std::filesystem::create_directories(working_directory);
    const std::string netlist_content = "V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRAN FORMAT=TECPLOT FILE=my_results.dat V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, working_directory), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // assert — the netlist handed to Xyce does not carry the FILE= option
    {
        std::ifstream started(view.m_started_netlist_path);
        const std::string started_content((std::istreambuf_iterator<char>(started)), std::istreambuf_iterator<char>());
        EXPECT_EQ(started_content.find("FILE=my_results.dat"), std::string::npos);
        EXPECT_NE(started_content.find(".PRINT TRAN FORMAT=TECPLOT V(1)"), std::string::npos);
    }
    // arrange — Xyce produced the default dat file next to the temporary netlist
    const auto dat_path = view.m_started_netlist_path.string() + ".dat";
    {
        std::ofstream dat_file(dat_path, std::ios::out | std::ios::trunc);
        dat_file << "TITLE = \"demo.cir - Xyce Electrical Simulator\", \n";
        dat_file << "\tVARIABLES = \" TIME\" \n";
        dat_file << "\" V(1)\" \n";
        dat_file << "ZONE F=POINT T=\"demo.cir \" \n";
        dat_file << "0.0 1.0\n";
        dat_file << "1e-9 1.1\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert — the default dat file loaded as the transient dataset
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // assert — the produced dat file was copied to the recorded FILE= destination
    ASSERT_TRUE(std::filesystem::exists(working_directory / "my_results.dat"));
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(dat_path, ec);
    std::filesystem::remove_all(working_directory, ec);
}

TEST(SlintMainWindowPresenterChecks, fft_dialog_result_guards_without_raw_file) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    // act
    presenter.on_fft_dialog_result({}, fft::FftParameters{});
    // assert — no status update, no spawned window
    EXPECT_EQ(view.m_status_text, "");
    EXPECT_TRUE(view.m_spawned_files.empty());
}

TEST(SlintMainWindowPresenterChecks, fft_dialog_result_with_empty_selection_sets_status) {
    // arrange
    RecordingView view;
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>("", std::filesystem::temp_directory_path()), PluginConfig(""), nullptr);
    presenter.load_analysis_measurements(make_raw_file());
    // act
    presenter.on_fft_dialog_result({}, fft::FftParameters{});
    // assert
    EXPECT_EQ(view.m_status_text, "No expressions selected for FFT");
    EXPECT_TRUE(view.m_spawned_files.empty());
}

TEST(SlintMainWindowPresenterChecks, prn_tranadjoint_default_format_looks_for_tradj_prn) {
    // arrange — launch a transient analysis with TRANADJOINT print; Xyce produces <netlist>.TRADJ.prn for TRANADJOINT
    RecordingView view;
    const std::string netlist_content = "V1 1 0 5\nR1 1 0 1K\n.TRAN 1u 1m\n.PRINT TRANADJOINT V(1)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write a .prn file at the .TRADJ.prn location
    const auto prn_path = view.m_started_netlist_path.string() + ".TRADJ.prn";
    {
        std::ofstream prn_file(prn_path, std::ios::out | std::ios::trunc);
        prn_file << "INDEX TIME V(1)\n";
        prn_file << "0 0.0 1.0\n";
        prn_file << "1 1e-9 1.1\n";
        prn_file << ".\n";
    }
    // act — finish the simulation successfully
    presenter.on_simulation_finished(0, false);
    // assert
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(prn_path, ec);
}

TEST(SlintMainWindowPresenterChecks, fft_run_without_print_loads_fft_tabs) {
    // arrange — a transient run with .FFT directives and no .PRINT: Xyce produces only the FFT calculation files, no analysis output
    RecordingView view;
    const std::string netlist_content = "V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nL1 N1 N2 10mH\nC1 N2 0 1uF\n.TRAN 1u 20m 0\n.FFT I(L1) NP=1024 WINDOW=HANN\n.FFT I(C1) NP=2048 WINDOW=HANN FORMAT=UNORM\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write the FFT output files Xyce produces, one per abscissa configuration (the hand-written content mirrors a real Xyce .FFT dump)
    const auto fft0_path = view.m_started_netlist_path.string() + ".fft0";
    {
        std::ofstream fft0(fft0_path, std::ios::out | std::ios::trunc);
        fft0 << "FFT analysis for I(L1):\n";
        fft0 << "  Window: HANN, Start Time: 0.000000e+00, Stop Time: 2.000000e-02\n";
        fft0 << "  First Harmonic: 9.765625e+02, Start Freq: 0.000000e+00, Stop Freq: 5.000000e+05\n";
        fft0 << "  DC component    Norm. Mag= 1.000000e-02   Phase= 1.800000e+02\n";
        fft0 << "       Index       Frequency       Norm. Mag           Phase\n";
        fft0 << "   1    9.765625e+02    5.000000e-01    9.000000e+01\n";
    }
    const auto fft1_path = view.m_started_netlist_path.string() + ".fft1";
    {
        std::ofstream fft1(fft1_path, std::ios::out | std::ios::trunc);
        fft1 << "FFT analysis for I(C1):\n";
        fft1 << "  Window: HANN, Start Time: 0.000000e+00, Stop Time: 2.000000e-02\n";
        fft1 << "  First Harmonic: 4.882812e+02, Start Freq: 0.000000e+00, Stop Freq: 2.500000e+05\n";
        fft1 << "  DC component    Mag= 2.000000e-02   Phase= 0.000000e+00\n";
        fft1 << "       Index       Frequency           Mag           Phase\n";
        fft1 << "   1    4.882812e+02    6.000000e-01    0.000000e+00\n";
    }
    // act
    presenter.on_simulation_finished(0, false);
    // assert — the FFT tabs are rendered even though no analysis output exists
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    ASSERT_EQ(presenter.fft_measurements().size(), 2u);
    ASSERT_EQ(view.m_plot_tabs.size(), 2u);
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_FALSE(view.m_plot_tabs[1].closable);
    EXPECT_EQ(view.m_active_plot_tab, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(fft0_path, ec);
    std::filesystem::remove(fft1_path, ec);
}

TEST(SlintMainWindowPresenterChecks, rerun_without_analysis_output_drops_stale_primary_tab) {
    // arrange — a transient run with a raw print and an FFT directive
    RecordingView view;
    auto netlist_source = std::make_unique<StubNetlistSource>("V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nL1 N1 N2 10mH\nC1 N2 0 1uF\n.TRAN 1u 20m 0\n.PRINT TRAN FORMAT=RAW V(*)\n.FFT I(L1) NP=1024 WINDOW=HANN\n.END\n", std::filesystem::temp_directory_path());
    StubNetlistSource* source = netlist_source.get();
    SlintMainWindowPresenter presenter(view, std::move(netlist_source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write the raw output and the FFT file the run produces
    const auto raw_path = view.m_started_netlist_path.string() + ".raw";
    {
        std::ofstream raw_file(raw_path, std::ios::out | std::ios::trunc);
        raw_file << "Title: Presenter Test Circuit\n";
        raw_file << "Plotname: Transient Analysis\n";
        raw_file << "Flags: real\n";
        raw_file << "No. Variables: 2\n";
        raw_file << "No. Points: 3\n";
        raw_file << "Variables:\n";
        raw_file << "\t0\ttime\ttime\n";
        raw_file << "\t1\tV(1)\tvoltage\n";
        raw_file << "Values:\n";
        raw_file << " 0  0.0  1.0\n";
        raw_file << " 1  0.001  2.0\n";
        raw_file << " 2  0.002  3.0\n";
    }
    const auto fft0_path = view.m_started_netlist_path.string() + ".fft0";
    {
        std::ofstream fft0(fft0_path, std::ios::out | std::ios::trunc);
        fft0 << "FFT analysis for I(L1):\n";
        fft0 << "  Window: HANN, Start Time: 0.000000e+00, Stop Time: 2.000000e-02\n";
        fft0 << "  First Harmonic: 9.765625e+02, Start Freq: 0.000000e+00, Stop Freq: 5.000000e+05\n";
        fft0 << "  DC component    Norm. Mag= 1.000000e-02   Phase= 1.800000e+02\n";
        fft0 << "       Index       Frequency       Norm. Mag           Phase\n";
        fft0 << "   1    9.765625e+02    5.000000e-01    9.000000e+01\n";
    }
    // act — finish the first run
    presenter.on_simulation_finished(0, false);
    // assert — the transient tab and the fft tab are rendered
    ASSERT_EQ(view.m_plot_tabs.size(), 2u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    const int primary_id = view.m_plot_tabs[0].id;
    const int fft_id = view.m_plot_tabs[1].id;
    // arrange — the netlist is edited and the raw print line removed, then the simulation runs again
    source->m_reloaded = true;
    source->m_content = "V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nL1 N1 N2 10mH\nC1 N2 0 1uF\n.TRAN 1u 20m 0\n.FFT I(L1) NP=1024 WINDOW=HANN\n.END\n";
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write the FFT file the second run produces at its own output location
    const auto fft0_path_second_run = view.m_started_netlist_path.string() + ".fft0";
    {
        std::ofstream fft0(fft0_path_second_run, std::ios::out | std::ios::trunc);
        fft0 << "FFT analysis for I(L1):\n";
        fft0 << "  Window: HANN, Start Time: 0.000000e+00, Stop Time: 2.000000e-02\n";
        fft0 << "  First Harmonic: 9.765625e+02, Start Freq: 0.000000e+00, Stop Freq: 5.000000e+05\n";
        fft0 << "  DC component    Norm. Mag= 1.000000e-02   Phase= 1.800000e+02\n";
        fft0 << "       Index       Frequency       Norm. Mag           Phase\n";
        fft0 << "   1    9.765625e+02    5.000000e-01    9.000000e+01\n";
    }
    // act — finish the second run
    presenter.on_simulation_finished(0, false);
    // assert — the stale transient tab was dropped with the whole previous result set, only the fresh fft tab remains
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_NE(view.m_plot_tabs[0].id, fft_id);
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    ASSERT_EQ(presenter.fft_measurements().size(), 1u);
    ASSERT_EQ(view.m_released_dataset_ids.size(), 2u);
    EXPECT_EQ(view.m_released_dataset_ids[0], primary_id);
    EXPECT_EQ(view.m_released_dataset_ids[1], fft_id);
    EXPECT_EQ(view.m_active_plot_tab, 0);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(fft0_path_second_run, ec);
    std::filesystem::remove(raw_path, ec);
    std::filesystem::remove(fft0_path, ec);
}

// ========================================================================================
// PCE companion output loading
// ========================================================================================

TEST(SlintMainWindowPresenterChecks, pce_run_without_analysis_print_loads_pce_tab) {
    // arrange — a transient run with .PCE parameters and its companion print but no .PRINT TRAN: Xyce produces only the PCE statistics file
    RecordingView view;
    const std::string netlist_content = "V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 IN 0 1u\n.TRAN 10u 1m 0\n.PCE param=R1 type=normal means=100 std_deviations=10\n.PRINT PCE V(IN)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write the PCE output file the run produces next to the netlist
    const auto pce_path = view.m_started_netlist_path.string() + ".PCE.prn";
    {
        std::ofstream pce_file(pce_path, std::ios::out | std::ios::trunc);
        pce_file << "Index TIME V(IN) {V(IN)}_mean {V(IN)}_stddev\n";
        pce_file << "0 0.0 0.0 0.0 0.0\n";
        pce_file << "1 1e-6 5.0 4.9 0.1\n";
        pce_file << ".\n";
    }
    // act
    presenter.on_simulation_finished(0, false);
    // assert — the pce tab renders even though no analysis output exists
    EXPECT_TRUE(view.m_charts_view_shown);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    EXPECT_FALSE(presenter.analysis_measurements().has_value());
    EXPECT_TRUE(presenter.pce_measurements().has_value());
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "PCE Analysis");
    EXPECT_FALSE(view.m_plot_tabs[0].closable);
    EXPECT_EQ(view.m_active_plot_tab, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(pce_path, ec);
}

TEST(SlintMainWindowPresenterChecks, tran_and_pce_prints_render_transient_and_pce_tabs) {
    // arrange — a transient run carrying both the analysis print and the .PCE companion print
    RecordingView view;
    const std::string netlist_content = "V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 IN 0 1u\n.TRAN 10u 1m 0\n.PRINT TRAN V(IN)\n.PCE param=R1 type=normal means=100 std_deviations=10\n.PRINT PCE V(IN)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write the transient output file the run produces
    const auto tran_path = view.m_started_netlist_path.string() + ".prn";
    {
        std::ofstream tran_file(tran_path, std::ios::out | std::ios::trunc);
        tran_file << "INDEX TIME V(IN)\n";
        tran_file << "0 0.0 0.0\n";
        tran_file << "1 1e-6 5.0\n";
        tran_file << ".\n";
    }
    // arrange — write the PCE output file the run produces
    const auto pce_path = view.m_started_netlist_path.string() + ".PCE.prn";
    {
        std::ofstream pce_file(pce_path, std::ios::out | std::ios::trunc);
        pce_file << "Index TIME V(IN) {V(IN)}_mean {V(IN)}_stddev\n";
        pce_file << "0 0.0 0.0 0.0 0.0\n";
        pce_file << "1 1e-6 5.0 4.9 0.1\n";
        pce_file << ".\n";
    }
    // act
    presenter.on_simulation_finished(0, false);
    // assert — the analysis tab and the pce tab render side by side
    EXPECT_TRUE(presenter.analysis_measurements().has_value());
    EXPECT_TRUE(presenter.pce_measurements().has_value());
    ASSERT_EQ(view.m_plot_tabs.size(), 2u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    EXPECT_EQ(view.m_plot_tabs[1].title, "PCE Analysis");
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    EXPECT_EQ(view.m_active_plot_tab, 0);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(tran_path, ec);
    std::filesystem::remove(pce_path, ec);
}

TEST(SlintMainWindowPresenterChecks, pce_run_without_produced_file_reports_missing_output) {
    // arrange — a transient run configured with .PCE and its companion print but no .PRINT TRAN; the produced pce file is never written
    RecordingView view;
    const std::string netlist_content = "V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 IN 0 1u\n.TRAN 10u 1m 0\n.PCE param=R1 type=normal means=100 std_deviations=10\n.PRINT PCE V(IN)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // act — finish the simulation successfully without the produced file
    presenter.on_simulation_finished(0, false);
    // assert — no data could be loaded, the output panel explains it
    EXPECT_EQ(view.m_status_text, "Simulation finished but output file could not be found");
    EXPECT_FALSE(view.m_output_panel_hidden);
    EXPECT_FALSE(presenter.pce_measurements().has_value());
    EXPECT_TRUE(view.m_plot_tabs.empty());
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
}

TEST(SlintMainWindowPresenterChecks, pce_run_with_unparsable_file_reports_missing_output) {
    // arrange — a transient run configured with .PCE and its companion print but no .PRINT TRAN
    RecordingView view;
    const std::string netlist_content = "V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 IN 0 1u\n.TRAN 10u 1m 0\n.PCE param=R1 type=normal means=100 std_deviations=10\n.PRINT PCE V(IN)\n.END\n";
    SlintMainWindowPresenter presenter(view, std::make_unique<StubNetlistSource>(netlist_content, std::filesystem::temp_directory_path()), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — create an empty pce file the parsers reject
    const auto pce_path = view.m_started_netlist_path.string() + ".PCE.prn";
    {
        std::ofstream pce_file(pce_path, std::ios::out | std::ios::trunc);
    }
    // act
    presenter.on_simulation_finished(0, false);
    // assert — the unparsable file yields no pce tab
    EXPECT_FALSE(presenter.pce_measurements().has_value());
    EXPECT_TRUE(view.m_plot_tabs.empty());
    EXPECT_EQ(view.m_status_text, "Simulation finished but output file could not be found");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(pce_path, ec);
}

TEST(SlintMainWindowPresenterChecks, rerun_without_pce_print_drops_stale_pce_tab) {
    // arrange — a transient run carrying both the analysis print and the .PCE companion print
    RecordingView view;
    auto netlist_source = std::make_unique<StubNetlistSource>("V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 IN 0 1u\n.TRAN 10u 1m 0\n.PRINT TRAN V(IN)\n.PCE param=R1 type=normal means=100 std_deviations=10\n.PRINT PCE V(IN)\n.END\n", std::filesystem::temp_directory_path());
    StubNetlistSource* source = netlist_source.get();
    SlintMainWindowPresenter presenter(view, std::move(netlist_source), PluginConfig(testing::internal::GetArgvs()[0]), nullptr);
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write the transient and pce output files the run produces
    const auto tran_path = view.m_started_netlist_path.string() + ".prn";
    {
        std::ofstream tran_file(tran_path, std::ios::out | std::ios::trunc);
        tran_file << "INDEX TIME V(IN)\n";
        tran_file << "0 0.0 0.0\n";
        tran_file << "1 1e-6 5.0\n";
        tran_file << ".\n";
    }
    const auto pce_path = view.m_started_netlist_path.string() + ".PCE.prn";
    {
        std::ofstream pce_file(pce_path, std::ios::out | std::ios::trunc);
        pce_file << "Index TIME V(IN) {V(IN)}_mean {V(IN)}_stddev\n";
        pce_file << "0 0.0 0.0 0.0 0.0\n";
        pce_file << "1 1e-6 5.0 4.9 0.1\n";
        pce_file << ".\n";
    }
    // act — finish the first run
    presenter.on_simulation_finished(0, false);
    // assert — the transient tab and the pce tab are rendered
    ASSERT_EQ(view.m_plot_tabs.size(), 2u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    EXPECT_EQ(view.m_plot_tabs[1].title, "PCE Analysis");
    const int pce_id = view.m_plot_tabs[1].id;
    // arrange — the netlist is edited and the pce directives removed, then the simulation runs again
    source->m_reloaded = true;
    source->m_content = "V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 IN 0 1u\n.TRAN 10u 1m 0\n.PRINT TRAN V(IN)\n.END\n";
    presenter.on_run_simulation();
    ASSERT_TRUE(view.m_started);
    // arrange — write the transient output file the second run produces at its own output location
    const auto tran_path_second_run = view.m_started_netlist_path.string() + ".prn";
    {
        std::ofstream tran_file(tran_path_second_run, std::ios::out | std::ios::trunc);
        tran_file << "INDEX TIME V(IN)\n";
        tran_file << "0 0.0 0.0\n";
        tran_file << "1 1e-6 5.0\n";
        tran_file << ".\n";
    }
    // act — finish the second run
    presenter.on_simulation_finished(0, false);
    // assert — the stale pce tab was released, only the fresh transient tab remains
    ASSERT_EQ(view.m_plot_tabs.size(), 1u);
    EXPECT_EQ(view.m_plot_tabs[0].title, "Transient");
    EXPECT_FALSE(presenter.pce_measurements().has_value());
    ASSERT_EQ(view.m_released_dataset_ids.size(), 1u);
    EXPECT_EQ(view.m_released_dataset_ids[0], pce_id);
    EXPECT_EQ(view.m_status_text, "Simulation finished successfully");
    // cleanup
    std::error_code ec;
    std::filesystem::remove(view.m_started_netlist_path, ec);
    std::filesystem::remove(tran_path_second_run, ec);
    std::filesystem::remove(tran_path, ec);
    std::filesystem::remove(pce_path, ec);
}

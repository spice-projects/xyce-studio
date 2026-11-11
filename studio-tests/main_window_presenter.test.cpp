#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "config/plugin_config.h"
#include "io/temp_output_directory.h"
#include "presenters/main_window_presenter.h"
#include "simulation/transient_simulation_parameters.h"
#include "views/main_window/main_window_view_def.h"

namespace
{
    // the view double records what the presenter asks the surface to do
    class FakeView final : public studio::MainWindowViewDef
    {
    public:
        void set_event_handler(studio::MainWindowViewEvents*) override {}

        void set_netlist_text(std::string_view content) override {
            editor_text = std::string(content);
            seeded_texts.emplace_back(editor_text);
        }

        [[nodiscard]] std::string netlist_text() const override { return editor_text; }

        void set_netlist_read_only(bool read_only) override {
            read_only_count++;
            read_only_last = read_only;
        }

        void set_status_text(std::string_view text) override { statuses.emplace_back(text); }

        void set_title(std::string_view title) override { titles.emplace_back(title); }

        void set_open_enabled(bool enabled) override { open_enabled = enabled; }

        void set_save_enabled(bool enabled) override { save_enabled = enabled; }

        void set_run_enabled(bool enabled) override { run_enabled = enabled; }

        void set_configure_enabled(bool enabled) override { configure_enabled = enabled; }

        void set_simulation_running(bool running) override { running_label = running; }

        void set_simulation_progress(const studio::SimulationProgress& progress) override {
            progress_count++;
            progress_last = progress;
        }

        void show_output_panel() override { output_panel_visible = true; }

        void hide_output_panel() override { output_panel_visible = false; }

        void clear_output() override {
            output_lines.clear();
            clear_count++;
        }

        void append_output_line(std::string_view line, LogSeverity severity) override { output_lines.emplace_back(std::string(line), severity); }

        [[nodiscard]] std::size_t output_line_count() const override { return output_lines.size(); }

        void start_simulation_process(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) override {
            start_count++;
            start_program = program;
            start_netlist_path = netlist_path;
            start_working_directory = working_directory;
        }

        void cancel_simulation_process() override { cancel_count++; }

        [[nodiscard]] std::optional<std::filesystem::path> request_netlist_save_path() override {
            save_as_count++;
            // the scripted choice the test made, which an absent value reads as a cancelled panel
            return save_as_path;
        }

        // the text the editor currently edits
        std::string editor_text;

        // every seeded content the presenter pushed
        std::vector<std::string> seeded_texts;

        // every title the presenter rendered
        std::vector<std::string> titles;

        // every status the presenter reported
        std::vector<std::string> statuses;

        // the read only state the presenter last applied and how often it was set
        int read_only_count = 0;
        bool read_only_last = false;

        // the enablement the presenter last applied
        bool open_enabled = false;
        bool save_enabled = false;
        bool run_enabled = false;
        bool configure_enabled = false;
        bool running_label = false;

        // the run state the presenter last published
        int progress_count = 0;
        studio::SimulationProgress progress_last;

        // the output panel visibility and the log the presenter appended
        bool output_panel_visible = false;
        std::vector<std::pair<std::string, LogSeverity>> output_lines;
        int clear_count = 0;

        // the simulation process launches and cancels the presenter requested
        int start_count = 0;
        int cancel_count = 0;
        std::string start_program;
        std::filesystem::path start_netlist_path;
        std::filesystem::path start_working_directory;

        // the save-as flow the fake answers
        std::optional<std::filesystem::path> save_as_path;
        int save_as_count = 0;
    };

    // point the filesystem at a clean root for one test and remove it afterwards
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::filesystem::path& root) :
            m_root(root) {
            // start from a clean root, so a previous failed run cannot seed this one
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);
            std::filesystem::create_directories(m_root, ec);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;

        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        ~ScratchDirectory() {
            // the root holds every file the test created
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);
        }

        // hand out the root the test writes into
        [[nodiscard]] const std::filesystem::path& path() const { return m_root; }

    private:
        // the root every artifact of the test lives under
        std::filesystem::path m_root;
    };
} // namespace

TEST(StudioMainWindowPresenterChecks, startup_is_untitled_clean_and_save_disabled) {
    // arrange
    const PluginConfig config;
    FakeView view;
    // act
    studio::MainWindowPresenter presenter(view, config);
    // assert: the toolbar starts in the untitled state and no title was rendered
    EXPECT_TRUE(view.open_enabled);
    EXPECT_FALSE(view.save_enabled);
    EXPECT_FALSE(view.run_enabled);
    EXPECT_TRUE(view.titles.empty());
    EXPECT_EQ(view.read_only_count, 0);
}

TEST(StudioMainWindowPresenterChecks, open_loads_the_netlist_and_starts_clean) {
    // arrange: a netlist file with known content
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-open");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << "* a simple tran netlist\nV1 in 0 1\n.END\n";
    }
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    // act
    presenter.on_open_file(netlist);
    // assert: the file content is in the editor, the title carries the name and the state is clean
    EXPECT_EQ(view.editor_text, "* a simple tran netlist\nV1 in 0 1\n.END\n");
    EXPECT_EQ(view.titles.back(), "tran.cir");
    EXPECT_EQ(view.read_only_count, 1);
    EXPECT_FALSE(view.read_only_last);
    EXPECT_FALSE(view.save_enabled);
    EXPECT_TRUE(view.run_enabled);
}

TEST(StudioMainWindowPresenterChecks, open_ignores_non_netlist_extensions) {
    // arrange
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-ignore");
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    // act
    presenter.on_open_file(scratch.path() / "run.raw");
    // assert: nothing was loaded and no title was rendered
    EXPECT_TRUE(view.seeded_texts.empty());
    EXPECT_TRUE(view.titles.empty());
    EXPECT_EQ(view.read_only_count, 0);
}

TEST(StudioMainWindowPresenterChecks, open_missing_file_shows_empty_editor) {
    // arrange: a path no file backs
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-missing");
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    // act
    presenter.on_open_file(scratch.path() / "absent.cir");
    // assert: the load failed, so the editor holds nothing and save stays unreachable
    EXPECT_EQ(view.editor_text, "");
    EXPECT_EQ(view.titles.back(), "absent.cir");
    EXPECT_FALSE(view.save_enabled);
}

TEST(StudioMainWindowPresenterChecks, edit_marks_dirty_and_enables_save) {
    // arrange: a loaded netlist
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-edit");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << ".END\n";
    }
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    // act: the user types into the editor
    view.editor_text = ".END\n* typed\n";
    presenter.on_netlist_edited();
    // assert: the title carries the marker and save is available
    EXPECT_EQ(view.titles.back(), "* tran.cir");
    EXPECT_TRUE(view.save_enabled);
}

TEST(StudioMainWindowPresenterChecks, save_writes_the_backing_file_and_clears_dirty) {
    // arrange: a loaded netlist the user edited
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-save");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << ".END\n";
    }
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    view.editor_text = ".END\n* saved line\n";
    presenter.on_netlist_edited();
    // act
    presenter.on_save();
    // assert: the file received the edit, the marker is gone and save is disabled again
    std::ifstream file(netlist);
    const std::string saved((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(saved, ".END\n* saved line\n");
    EXPECT_EQ(view.titles.back(), "tran.cir");
    EXPECT_FALSE(view.save_enabled);
    EXPECT_EQ(view.save_as_count, 0);
}

TEST(StudioMainWindowPresenterChecks, untitled_save_runs_save_as_and_renames_the_title) {
    // arrange: an untitled netlist the user typed into
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-save-as");
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    view.editor_text = "* typed from scratch\n.END\n";
    presenter.on_netlist_edited();
    view.save_as_path = scratch.path() / "newname";
    // act
    presenter.on_save();
    // assert: the missing extension defaulted to the netlist format and the file received the text
    std::ifstream file(scratch.path() / "newname.cir");
    const std::string saved((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(saved, "* typed from scratch\n.END\n");
    EXPECT_EQ(view.titles.back(), "newname.cir");
    EXPECT_FALSE(view.save_enabled);
    // assert: the rebound source writes in place without asking again
    view.editor_text = "* second edit\n";
    presenter.on_netlist_edited();
    EXPECT_TRUE(view.save_enabled);
    presenter.on_save();
    std::ifstream again(scratch.path() / "newname.cir");
    const std::string resaved((std::istreambuf_iterator<char>(again)), std::istreambuf_iterator<char>());
    EXPECT_EQ(resaved, "* second edit\n");
    EXPECT_EQ(view.save_as_count, 1);
}

TEST(StudioMainWindowPresenterChecks, cancelled_save_as_keeps_the_netlist_dirty) {
    // arrange: an untitled netlist the user typed into
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-save-cancel");
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    view.editor_text = "* typed from scratch\n.END\n";
    presenter.on_netlist_edited();
    view.save_as_path = std::nullopt;
    // act
    presenter.on_save();
    // assert: the netlist stays dirty and no file was written
    EXPECT_EQ(view.titles.back(), "* ");
    EXPECT_TRUE(view.save_enabled);
    EXPECT_TRUE(std::filesystem::is_empty(scratch.path()));
}

TEST(StudioMainWindowPresenterChecks, run_refuses_an_invalid_xyce_executable) {
    // arrange: a netlist loaded and a configuration that names no executable
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-invalid");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << ".tran 1m 10m\n.END\n";
    }
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    view.statuses.clear();
    // act
    presenter.on_run_clicked();
    // assert: the run was refused with the configuration error and no process started
    EXPECT_EQ(view.statuses.back(), "Configured Xyce executable path is invalid");
    EXPECT_EQ(view.start_count, 0);
    EXPECT_FALSE(view.running_label);
    EXPECT_FALSE(view.output_panel_visible);
}

TEST(StudioMainWindowPresenterChecks, run_refuses_an_empty_netlist) {
    // arrange: a valid executable and an editor that holds nothing
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-empty");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    view.statuses.clear();
    // act
    presenter.on_run_clicked();
    // assert: the run was refused with the content error and no process started
    EXPECT_EQ(view.statuses.back(), "No netlist content to simulate");
    EXPECT_EQ(view.start_count, 0);
    EXPECT_FALSE(view.output_panel_visible);
}

TEST(StudioMainWindowPresenterChecks, run_starts_the_process_and_streams_the_log) {
    // arrange: a valid executable and a loaded netlist
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-stream");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << ".tran 1m 10m\n.END\n";
    }
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    view.statuses.clear();
    // act: start the run
    presenter.on_run_clicked();
    // assert: the process was launched with the configured executable over a private netlist copy
    EXPECT_EQ(view.start_count, 1);
    EXPECT_EQ(view.start_program, fake_exe.string());
    EXPECT_EQ(view.start_working_directory, scratch.path());
    std::ifstream temp_netlist(view.start_netlist_path);
    const std::string written((std::istreambuf_iterator<char>(temp_netlist)), std::istreambuf_iterator<char>());
    EXPECT_EQ(written, ".tran 1m 10m\n.END\n");
    // assert: the shell entered the running state with a reset log in a visible panel
    EXPECT_TRUE(view.output_panel_visible);
    EXPECT_EQ(view.clear_count, 1);
    EXPECT_TRUE(view.output_lines.empty());
    EXPECT_TRUE(view.running_label);
    EXPECT_TRUE(view.run_enabled);
    EXPECT_EQ(view.statuses.back(), "");
    // act: stream one stdout line, then the copy Xyce writes on the other stream
    presenter.on_simulation_stdout("Netlist warning in file tran.cir at or near line 1");
    presenter.on_simulation_stderr("Netlist warning in file tran.cir at or near line 1");
    presenter.on_simulation_stdout("***** Percent complete: 50 %");
    // assert: the duplicate copy was dropped and the other two lines were shown
    EXPECT_EQ(view.output_lines.size(), 2);
    EXPECT_EQ(view.output_lines[0].first, "Netlist warning in file tran.cir at or near line 1");
    EXPECT_EQ(view.output_lines[1].first, "***** Percent complete: 50 %");
    // act: the process ends by itself
    presenter.on_simulation_finished(0, false);
    // assert: the run is over, the log stays visible and the status reports success
    EXPECT_FALSE(view.running_label);
    EXPECT_TRUE(view.output_panel_visible);
    EXPECT_EQ(view.statuses.back(), "Simulation finished");
    EXPECT_EQ(view.cancel_count, 0);
}

TEST(StudioMainWindowPresenterChecks, a_failed_run_reports_the_exit_code) {
    // arrange: a valid executable, a loaded netlist and a started run
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-failed");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 2\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << ".tran 1m 10m\n.END\n";
    }
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    presenter.on_run_clicked();
    view.statuses.clear();
    // act
    presenter.on_simulation_finished(2, false);
    // assert: the run is over and the status carries the exit code
    EXPECT_FALSE(view.running_label);
    EXPECT_EQ(view.statuses.back(), "Simulation failed with exit code 2");
}

TEST(StudioMainWindowPresenterChecks, a_second_run_click_cancels_and_the_run_reports_canceled) {
    // arrange: a valid executable, a loaded netlist and a started run
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-cancel");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << ".tran 1m 10m\n.END\n";
    }
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    presenter.on_run_clicked();
    view.statuses.clear();
    // act: the run action doubles as the stop action while the simulation runs
    presenter.on_run_clicked();
    // assert: the presenter asked the view to stop the process
    EXPECT_EQ(view.cancel_count, 1);
    EXPECT_EQ(view.start_count, 1);
    // act: the process reports the cancel
    presenter.on_simulation_finished(0, true);
    // assert: the run is over and the status reports the cancel
    EXPECT_FALSE(view.running_label);
    EXPECT_EQ(view.statuses.back(), "Simulation canceled");
}

TEST(StudioMainWindowPresenterChecks, closing_the_output_panel_hides_it_until_the_next_run) {
    // arrange: a valid executable, a loaded netlist and a started run
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-close");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << ".tran 1m 10m\n.END\n";
    }
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    presenter.on_run_clicked();
    // finish the run, which leaves the panel inspectable
    presenter.on_simulation_finished(0, false);
    // act: the user closes the output panel
    presenter.on_output_close();
    // assert: the panel is hidden
    EXPECT_FALSE(view.output_panel_visible);
    // act: a new run reveals the panel again and resets the log
    presenter.on_run_clicked();
    // assert: the panel is visible again with a reset log
    EXPECT_TRUE(view.output_panel_visible);
    EXPECT_EQ(view.clear_count, 2);
}

TEST(StudioMainWindowPresenterChecks, the_run_action_needs_netlist_content) {
    // arrange
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    // assert: the run action starts disabled on the untitled shell
    EXPECT_FALSE(view.run_enabled);
    // act: the user types the first netlist content
    view.editor_text = ".END\n";
    presenter.on_netlist_edited();
    // assert: the run action is available once the editor holds content
    EXPECT_TRUE(view.run_enabled);
}

TEST(StudioMainWindowPresenterChecks, the_configure_action_needs_netlist_content) {
    // arrange
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    // assert: the configure action starts disabled on the untitled shell
    EXPECT_FALSE(view.configure_enabled);
    // act: the user types the first netlist content
    view.editor_text = ".END\n";
    presenter.on_netlist_edited();
    // assert: the configure action is available once the editor holds content
    EXPECT_TRUE(view.configure_enabled);
}

TEST(StudioMainWindowPresenterChecks, prepare_reads_the_transient_directives) {
    // arrange: an editor holding one transient directive
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    view.editor_text = "R1 1 0 1k\n.TRAN 1m 10m\n.END\n";
    // act
    const SimulationConfig& prepared = presenter.prepare_simulation_parameters();
    // assert: the dialog opens on the configuration the netlist declares
    EXPECT_EQ(prepared.analysis_type, "TRAN");
    ASSERT_TRUE(std::holds_alternative<TransientSimulationParameters>(prepared.analysis));
    const auto& transient = std::get<TransientSimulationParameters>(prepared.analysis);
    EXPECT_EQ(transient.initial_step_value, "1m");
    EXPECT_EQ(transient.final_time_value, "10m");
}

TEST(StudioMainWindowPresenterChecks, accepted_parameters_rewrite_the_editor_and_mark_it_dirty) {
    // arrange: an editor holding the original transient directive
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    view.editor_text = "R1 1 0 1k\n.TRAN 1m 10m\n.END\n";
    static_cast<void>(presenter.prepare_simulation_parameters());
    // the configuration the dialog accepted carries a new final time
    SimulationConfig accepted = SimulationConfig::from_xyce_directives({});
    accepted.analysis_type = "TRAN";
    accepted.analysis = TransientSimulationParameters("2m", "20m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt);
    // act
    presenter.on_simulation_parameters_accepted(accepted);
    // assert: the editor carries the accepted directive instead of the old one
    EXPECT_NE(view.editor_text.find(".TRAN 2m 20m"), std::string::npos);
    EXPECT_EQ(view.editor_text.find(".TRAN 1m 10m"), std::string::npos);
    // assert: the rewrite marked the netlist dirty, which the save action follows
    EXPECT_TRUE(view.save_enabled);
    ASSERT_FALSE(view.titles.empty());
    EXPECT_EQ(view.titles.back().front(), '*');
}

TEST(StudioMainWindowPresenterChecks, accepted_parameters_reuse_the_sanitized_netlist_body) {
    // arrange: a netlist whose only managed directive is the transient analysis
    const PluginConfig config;
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    view.editor_text = "* matrix\nV1 1 0 5\nR1 1 0 1k\n.TRAN 1m 10m\n.END\n";
    static_cast<void>(presenter.prepare_simulation_parameters());
    // the configuration the dialog accepted carries a new initial step
    SimulationConfig accepted = SimulationConfig::from_xyce_directives({});
    accepted.analysis_type = "TRAN";
    accepted.analysis = TransientSimulationParameters("5m", "10m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt);
    // act
    presenter.on_simulation_parameters_accepted(accepted);
    // assert: the devices survive and exactly one transient directive remains
    EXPECT_NE(view.editor_text.find("V1 1 0 5"), std::string::npos);
    EXPECT_NE(view.editor_text.find("R1 1 0 1k"), std::string::npos);
    EXPECT_NE(view.editor_text.find(".TRAN 5m 10m"), std::string::npos);
    EXPECT_EQ(view.editor_text.find(".TRAN 1m 10m"), std::string::npos);
}

TEST(StudioMainWindowPresenterChecks, a_successful_run_offers_its_output_to_the_charts) {
    // arrange: a netlist with an explicit print destination the run must strip
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-auto-load");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write a transient netlist with a print file the run keeps private
        std::ofstream file(netlist);
        file << "R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV FILE=ignored.csv V(1)\n.END\n";
    }
    const PluginConfig config("/bin/echo");
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    bool called = false;
    studio::RunOutput captured_output;
    std::shared_ptr<TempOutputDirectory> captured_artifacts;
    presenter.set_run_output_handler([&](const studio::RunOutput& output, std::shared_ptr<TempOutputDirectory> artifacts) {
        called = true;
        captured_output = output;
        captured_artifacts = std::move(artifacts);
        return studio::RunOutputResult{.outcome = studio::RunOutputOutcome::Primary, .analysis_loaded = true};
    });
    presenter.on_open_file(netlist);
    // act: launch the run and inspect the private netlist it wrote
    presenter.on_run_clicked();
    ASSERT_FALSE(view.start_netlist_path.empty());
    {
        // read the private netlist the runner was handed
        std::ifstream file(view.start_netlist_path);
        const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // assert: the FILE= destination is removed so the output stays in the run directory
        EXPECT_EQ(written.find("FILE="), std::string::npos);
    }
    // arrange: the fake run leaves an analysis output beside its private netlist
    const std::filesystem::path output = view.start_netlist_path.string() + ".csv";
    {
        // write the table the charts parse
        std::ofstream file(output);
        file << "TIME,V(1)\n0,0\n1,1\n2,2\n";
    }
    // act: report a successful run
    presenter.on_simulation_finished(0, false);
    // assert: the run handler received the configuration, paths and run lease
    EXPECT_TRUE(called);
    EXPECT_EQ(captured_output.netlist_path, view.start_netlist_path);
    EXPECT_EQ(captured_output.title, "tran.cir");
    EXPECT_NE(captured_artifacts, nullptr);
    EXPECT_TRUE(captured_artifacts->owns(captured_output.netlist_path));
    ASSERT_TRUE(captured_output.config.analysis_print_parameters().has_value());
    EXPECT_EQ(captured_output.config.analysis_print_parameters()->print_format, "CSV");
    EXPECT_EQ(view.statuses.back(), "Simulation finished");
}

TEST(StudioMainWindowPresenterChecks, a_print_file_option_on_a_continuation_line_is_removed_and_copied_back) {
    // arrange: a netlist whose print destination sits on a continuation line
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-print-continuation");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write the split print directive
        std::ofstream file(netlist);
        file << "R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV\n+ FILE=requested.csv V(1)\n.END\n";
    }
    const PluginConfig config("/bin/echo");
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    bool called = false;
    presenter.set_run_output_handler([&](const studio::RunOutput&, std::shared_ptr<TempOutputDirectory>) {
        called = true;
        return studio::RunOutputResult{.outcome = studio::RunOutputOutcome::Primary, .analysis_loaded = true};
    });
    // act: launch the run and inspect the private netlist it wrote
    presenter.on_run_clicked();
    ASSERT_FALSE(view.start_netlist_path.empty());
    {
        // read the private netlist the runner was handed
        std::ifstream file(view.start_netlist_path);
        const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // assert: the continuation was joined and the FILE= destination removed
        EXPECT_EQ(written.find("FILE="), std::string::npos);
        EXPECT_NE(written.find("V(1)"), std::string::npos);
    }
    // arrange: the run writes the default csv file
    {
        // write the produced table
        std::ofstream file(view.start_netlist_path.string() + ".csv");
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    // act: report a successful run
    presenter.on_simulation_finished(0, false);
    // assert: the requested destination receives the produced file
    EXPECT_TRUE(called);
    std::ifstream copied(scratch.path() / "requested.csv");
    ASSERT_TRUE(copied.is_open());
    const std::string copied_text((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
    EXPECT_EQ(copied_text, "TIME,V(1)\n0,0\n1,1\n");
    EXPECT_EQ(view.statuses.back(), "Simulation finished");
}

TEST(StudioMainWindowPresenterChecks, a_canceled_run_skips_the_run_output_handler) {
    // arrange: a started run
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-cancel-output");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        // write a simulator that exits cleanly
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write a transient netlist with a print
        std::ofstream file(netlist);
        file << ".TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n";
    }
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    int calls = 0;
    presenter.set_run_output_handler([&](const studio::RunOutput&, std::shared_ptr<TempOutputDirectory>) {
        calls++;
        return studio::RunOutputResult{.outcome = studio::RunOutputOutcome::Primary, .analysis_loaded = true};
    });
    presenter.on_run_clicked();
    {
        // write an output the canceled run must not adopt
        std::ofstream file(view.start_netlist_path.string() + ".csv");
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    // act: the process reports a cancel
    presenter.on_simulation_finished(0, true);
    // assert: no dataset was adopted and the status reports the cancel
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(view.statuses.back(), "Simulation canceled");
}

TEST(StudioMainWindowPresenterChecks, a_failed_run_skips_the_run_output_handler) {
    // arrange: a started run
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-failed-output");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        // write a simulator that exits with an error
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 2\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write a transient netlist with a print
        std::ofstream file(netlist);
        file << ".TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n";
    }
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    int calls = 0;
    presenter.set_run_output_handler([&](const studio::RunOutput&, std::shared_ptr<TempOutputDirectory>) {
        calls++;
        return studio::RunOutputResult{.outcome = studio::RunOutputOutcome::Primary, .analysis_loaded = true};
    });
    presenter.on_run_clicked();
    {
        // write an output the failed run must not adopt
        std::ofstream file(view.start_netlist_path.string() + ".csv");
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    // act: the process reports a failure
    presenter.on_simulation_finished(2, false);
    // assert: no dataset was adopted and the status reports the exit code
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(view.statuses.back(), "Simulation failed with exit code 2");
}

TEST(StudioMainWindowPresenterChecks, a_successful_run_without_its_expected_output_reports_the_miss) {
    // arrange: a started run with a print directive but no produced file
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-no-output");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        // write a simulator that exits cleanly
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write a transient netlist with a print
        std::ofstream file(netlist);
        file << "R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n";
    }
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    int calls = 0;
    presenter.set_run_output_handler([&](const studio::RunOutput&, std::shared_ptr<TempOutputDirectory>) {
        calls++;
        return studio::RunOutputResult{};
    });
    presenter.on_run_clicked();
    // act: the run finishes without writing its print output
    presenter.on_simulation_finished(0, false);
    // assert: the handler was asked and the status names the missing output
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(view.statuses.back(), "Simulation finished but output file could not be found");
}

TEST(StudioMainWindowPresenterChecks, a_run_notifies_the_started_handler_once) {
    // arrange: a valid executable and a loaded netlist
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-started");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        // write a simulator that exits cleanly
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write a transient netlist
        std::ofstream file(netlist);
        file << "R1 1 0 1k\n.TRAN 1m 10m\n.END\n";
    }
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    int starts = 0;
    presenter.set_run_started_handler([&] { starts++; });
    // act: start a run
    presenter.on_run_clicked();
    // assert: the start handler ran once
    EXPECT_EQ(starts, 1);
    // act: the second click cancels the live run
    presenter.on_run_clicked();
    // assert: no second process was started
    EXPECT_EQ(starts, 1);
    EXPECT_EQ(view.start_count, 1);
}

TEST(StudioMainWindowPresenterChecks, a_plot_directive_never_reaches_the_run_netlist) {
    // arrange: a netlist with a chart-only plot directive
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-plot");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write the plot directive between the analysis and the end marker
        std::ofstream file(netlist);
        file << "R1 1 0 1k\n.TRAN 1m 10m\n.PLOT V(1)\n.END\n";
    }
    const PluginConfig config("/bin/echo");
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    // act: start the run and inspect the private netlist
    presenter.on_run_clicked();
    ASSERT_FALSE(view.start_netlist_path.empty());
    // assert: the simulator receives the analysis but not the chart directive
    std::ifstream file(view.start_netlist_path);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(written.find(".PLOT"), std::string::npos);
    EXPECT_NE(written.find(".TRAN"), std::string::npos);
}

TEST(StudioMainWindowPresenterChecks, a_duplicate_finish_is_ignored) {
    // arrange: a started run with a recording status bar
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-duplicate-finish");
    const std::filesystem::path fake_exe = scratch.path() / "Xyce";
    {
        // write a simulator that exits cleanly
        std::ofstream file(fake_exe);
        file << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(fake_exe, std::filesystem::perms::owner_all);
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write a transient netlist
        std::ofstream file(netlist);
        file << "R1 1 0 1k\n.TRAN 1m 10m\n.END\n";
    }
    const PluginConfig config(fake_exe.string());
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    presenter.on_run_clicked();
    // act: report the same finish twice
    presenter.on_simulation_finished(0, false);
    const std::size_t statuses = view.statuses.size();
    presenter.on_simulation_finished(0, false);
    // assert: the late finish changed nothing
    EXPECT_EQ(view.statuses.size(), statuses);
}

TEST(StudioMainWindowPresenterChecks, a_netlist_without_end_keeps_its_analysis_in_the_run_netlist) {
    // arrange: a netlist that omits the end marker
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-no-end");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write the analysis without .END
        std::ofstream file(netlist);
        file << "R1 1 0 1k\n.TRAN 1m 10m\n";
    }
    const PluginConfig config("/bin/echo");
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    // act: start the run and inspect the private netlist
    presenter.on_run_clicked();
    ASSERT_FALSE(view.start_netlist_path.empty());
    // assert: the analysis survives and the rendered netlist gains the end marker
    std::ifstream file(view.start_netlist_path);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(written.find(".TRAN"), std::string::npos);
    EXPECT_NE(written.find(".END"), std::string::npos);
}

TEST(StudioMainWindowPresenterChecks, a_subcircuit_end_does_not_hide_the_missing_netlist_end) {
    // arrange: a netlist without a top-level .END but with a .ENDS line
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-ends");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write the subcircuit and the analysis
        std::ofstream file(netlist);
        file << "Title\n.SUBCKT sub 1 2\nR1 1 2 1k\n.ENDS\n.TRAN 1m 10m\n";
    }
    const PluginConfig config("/bin/echo");
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    // act: start the run and inspect the private netlist
    presenter.on_run_clicked();
    ASSERT_FALSE(view.start_netlist_path.empty());
    // assert: the analysis and the real end marker survive the .ENDS line
    std::ifstream file(view.start_netlist_path);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(written.find(".ENDS"), std::string::npos);
    EXPECT_NE(written.find(".TRAN"), std::string::npos);
    EXPECT_NE(written.find("\n.END\n"), std::string::npos);
}

TEST(StudioMainWindowPresenterChecks, a_comment_that_ends_in_end_does_not_move_the_directives) {
    // arrange: a comment line that ends with the end marker text before the devices
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-comment-end");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write the misleading comment above the circuit
        std::ofstream file(netlist);
        file << "Title\n* .END\nR1 1 0 1k\n.TRAN 1m 10m\n.END\n";
    }
    const PluginConfig config("/bin/echo");
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    // act: start the run and inspect the private netlist
    presenter.on_run_clicked();
    ASSERT_FALSE(view.start_netlist_path.empty());
    // assert: the directives stay after the devices and before the real end marker
    std::ifstream file(view.start_netlist_path);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::size_t device = written.find("R1 1 0 1k");
    const std::size_t analysis = written.find(".TRAN");
    const std::size_t end = written.rfind("\n.END\n");
    ASSERT_NE(device, std::string::npos);
    ASSERT_NE(analysis, std::string::npos);
    ASSERT_NE(end, std::string::npos);
    EXPECT_LT(device, analysis);
    EXPECT_LT(analysis, end);
}

TEST(StudioMainWindowPresenterChecks, an_unreadable_analysis_output_preserves_the_user_copy) {
    // arrange: a run whose analysis print writes the produced file
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-run-preserve-copy");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        // write the print with its user destination
        std::ofstream file(netlist);
        file << "R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV FILE=requested.csv V(1)\n.END\n";
    }
    const PluginConfig config("/bin/echo");
    FakeView view;
    studio::MainWindowPresenter presenter(view, config);
    presenter.on_open_file(netlist);
    presenter.set_run_output_handler([&](const studio::RunOutput&, std::shared_ptr<TempOutputDirectory>) {
        // the charts adopted a fallback file, not the configured analysis print
        return studio::RunOutputResult{.outcome = studio::RunOutputOutcome::Primary, .analysis_loaded = false};
    });
    presenter.on_run_clicked();
    ASSERT_FALSE(view.start_netlist_path.empty());
    {
        // the previous user copy
        std::ofstream file(scratch.path() / "requested.csv");
        file << "previous\n";
    }
    {
        // the produced analysis file exists but the charts did not read it
        std::ofstream file(view.start_netlist_path.string() + ".csv");
        file << "produced\n";
    }
    // act: report the successful run
    presenter.on_simulation_finished(0, false);
    // assert: the user copy keeps its previous content
    std::ifstream copied(scratch.path() / "requested.csv");
    const std::string copied_text((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
    EXPECT_EQ(copied_text, "previous\n");
    EXPECT_EQ(view.statuses.back(), "Simulation finished");
}

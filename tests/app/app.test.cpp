#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "app/app.h"
#include "config/plugin_config.h"
#include "expression/expression_manager.h"
#include "io/xyce_output_file.h"
#include "simulation/simulation_config.h"
#include "ui/main_window_view_def.h"

namespace
{
    // RecordingView captures presenter interactions without requiring
    // a Slint runtime
    class RecordingView : public MainWindowViewDef
    {
    public:
        void set_title(const std::string&) override {}
        void set_status_text(const std::string&) override {}
        void apply_action_enablement(const ActionStateEnablement&) override {}
        void set_simulation_running(bool) override {}
        void show_netlist_view() override {}
        void show_charts_view() override {}
        void set_netlist_editor_content(const std::string&) override {}
        std::string netlist_editor_content() const override { return {}; }
        void set_netlist_editor_read_only(bool) override {}
        bool charts_shown() const override { return false; }
        void show_simulation_output_panel() override {}
        void hide_simulation_output_panel() override {}
        void clear_simulation_output() override {}
        void append_simulation_output_line(const std::string&) override {}
        bool simulation_output_panel_hidden() const override { return true; }
        bool simulation_output_has_content() const override { return false; }
        void update_charts(int, ExpressionManager&, const StepInformation&, AbscissaScale, const std::vector<std::vector<std::string>>&, bool) override {}
        void release_charts(int) override {}
        void release_all_charts() override {}
        void show_fft_dialog(size_t) override {}
        void show_step_tool_dialog(size_t) override {}
        std::optional<SimulationConfig> show_simulation_parameters_dialog(const SimulationConfig&) override { return std::nullopt; }
        std::optional<PluginConfig> show_plugin_config_dialog(const PluginConfig&) override { return std::nullopt; }
        void start_simulation_process(const std::string&, const std::filesystem::path&, const std::filesystem::path&) override {}
        void cancel_simulation_process() override {}
        void spawn_raw_file_window(std::shared_ptr<XyceOutputFile>) override {}
        void set_event_handler(MainWindowViewDefEvents&) override {}
    };
} // namespace

// ========================================================================================
// App singleton
// ========================================================================================

TEST(AppChecks, instance_returns_same_reference) {
    // arrange / act
    App& a = App::instance();
    App& b = App::instance();
    // assert
    EXPECT_EQ(&a, &b);
}

// ========================================================================================
// initialize() CLI parsing
// ========================================================================================

TEST(AppChecks, initialize_parses_log_level_long_form) {
    // arrange
    const char* argv[] = {"test", "--log-level", "debug"};
    int argc = 3;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    EXPECT_EQ(app.log_level(), "debug");
}

TEST(AppChecks, initialize_parses_log_level_equals_form) {
    // arrange
    const char* argv[] = {"test", "--log-level=warn"};
    int argc = 2;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    EXPECT_EQ(app.log_level(), "warn");
}

TEST(AppChecks, initialize_parses_log_level_short_form) {
    // arrange
    const char* argv[] = {"test", "-l", "error"};
    int argc = 3;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    EXPECT_EQ(app.log_level(), "error");
}

TEST(AppChecks, initialize_normalizes_to_lowercase) {
    // arrange
    const char* argv[] = {"test", "--log-level", "DEBUG"};
    int argc = 3;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    EXPECT_EQ(app.log_level(), "debug");
}

TEST(AppChecks, initialize_parses_netlist_space_form) {
    // arrange
    const char* argv[] = {"test", "--netlist", "/tmp/amplifier.cir"};
    int argc = 3;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    ASSERT_TRUE(app.netlist_path().has_value());
    EXPECT_EQ(app.netlist_path()->string(), "/tmp/amplifier.cir");
}

TEST(AppChecks, initialize_parses_netlist_equals_form) {
    // arrange
    const char* argv[] = {"test", "--netlist=/tmp/amplifier.cir"};
    int argc = 2;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    ASSERT_TRUE(app.netlist_path().has_value());
    EXPECT_EQ(app.netlist_path()->string(), "/tmp/amplifier.cir");
}

TEST(AppChecks, initialize_parses_raw_space_form) {
    // arrange
    const char* argv[] = {"test", "--raw", "/tmp/output.raw"};
    int argc = 3;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    ASSERT_TRUE(app.raw_path().has_value());
    EXPECT_EQ(app.raw_path()->string(), "/tmp/output.raw");
}

TEST(AppChecks, initialize_parses_prn_csd_csv_output_forms) {
    // arrange — every analysis output extension the main window loads is accepted through --raw
    App& app = App::instance();
    // act / assert — prn
    {
        const char* argv[] = {"test", "--raw", "/tmp/output.prn"};
        app.initialize(3, const_cast<char**>(argv));
    }
    ASSERT_TRUE(app.raw_path().has_value());
    EXPECT_EQ(app.raw_path()->string(), "/tmp/output.prn");
    // act / assert — csd
    {
        const char* argv[] = {"test", "--raw", "/tmp/output.csd"};
        app.initialize(3, const_cast<char**>(argv));
    }
    ASSERT_TRUE(app.raw_path().has_value());
    EXPECT_EQ(app.raw_path()->string(), "/tmp/output.csd");
    // act / assert — csv
    {
        const char* argv[] = {"test", "--raw", "/tmp/output.csv"};
        app.initialize(3, const_cast<char**>(argv));
    }
    ASSERT_TRUE(app.raw_path().has_value());
    EXPECT_EQ(app.raw_path()->string(), "/tmp/output.csv");
    // act / assert — dat
    {
        const char* argv[] = {"test", "--raw", "/tmp/output.dat"};
        app.initialize(3, const_cast<char**>(argv));
    }
    ASSERT_TRUE(app.raw_path().has_value());
    EXPECT_EQ(app.raw_path()->string(), "/tmp/output.dat");
}

TEST(AppChecks, initialize_accepts_uppercase_output_extension) {
    // arrange — the extension comparison is case-insensitive
    const char* argv[] = {"test", "--raw=/tmp/RESULTS.CSV"};
    App& app = App::instance();
    // act
    app.initialize(2, const_cast<char**>(argv));
    // assert
    ASSERT_TRUE(app.raw_path().has_value());
    EXPECT_EQ(app.raw_path()->string(), "/tmp/RESULTS.CSV");
}

TEST(AppChecks, initialize_rejects_raw_with_unrelated_extension) {
    // arrange — a .txt file is not an analysis output
    const char* argv[] = {"test", "--raw", "/tmp/notes.txt"};
    App& app = App::instance();
    // act
    app.initialize(3, const_cast<char**>(argv));
    // assert
    EXPECT_FALSE(app.raw_path().has_value());
}

TEST(AppChecks, initialize_parses_xyce_space_form) {
    // arrange
    const char* argv[] = {"test", "--xyce", "/usr/local/bin/Xyce"};
    int argc = 3;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    ASSERT_TRUE(app.xyce_path().has_value());
    EXPECT_EQ(app.xyce_path().value(), "/usr/local/bin/Xyce");
}

TEST(AppChecks, initialize_parses_all_file_options_together) {
    // arrange
    const char* argv[] = {"test", "--netlist", "/tmp/amplifier.cir", "--raw=/tmp/output.raw", "--xyce", "/usr/local/bin/Xyce"};
    int argc = 6;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    ASSERT_TRUE(app.netlist_path().has_value());
    ASSERT_TRUE(app.raw_path().has_value());
    ASSERT_TRUE(app.xyce_path().has_value());
    EXPECT_EQ(app.netlist_path()->string(), "/tmp/amplifier.cir");
    EXPECT_EQ(app.raw_path()->string(), "/tmp/output.raw");
    EXPECT_EQ(app.xyce_path().value(), "/usr/local/bin/Xyce");
}

TEST(AppChecks, initialize_rejects_netlist_with_wrong_extension) {
    // arrange
    const char* argv[] = {"test", "--netlist", "/tmp/output.raw"};
    int argc = 3;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    EXPECT_FALSE(app.netlist_path().has_value());
}

TEST(AppChecks, initialize_rejects_raw_with_wrong_extension) {
    // arrange
    const char* argv[] = {"test", "--raw", "/tmp/amplifier.cir"};
    int argc = 3;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // assert
    EXPECT_FALSE(app.raw_path().has_value());
}

TEST(AppChecks, initialize_resets_file_options_when_absent) {
    // arrange
    const char* argv[] = {"test", "--netlist", "/tmp/amplifier.cir"};
    int argc = 3;
    App& app = App::instance();
    // act
    app.initialize(argc, const_cast<char**>(argv));
    // continue: initialize again without the file options
    const char* empty_argv[] = {"test"};
    app.initialize(1, const_cast<char**>(empty_argv));
    // assert
    EXPECT_FALSE(app.netlist_path().has_value());
    EXPECT_FALSE(app.raw_path().has_value());
    EXPECT_FALSE(app.xyce_path().has_value());
}

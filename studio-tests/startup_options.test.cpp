#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "app/startup_options.h"

TEST(StudioStartupOptionsChecks, every_option_takes_the_joined_and_separated_forms) {
    // arrange: the long forms in both spellings
    const std::vector<std::string_view> arguments = {"--xyce", "/bin/echo", "--netlist=board.cir", "--raw=out.csv", "--dataset", "other.csv", "--log-level", "DEBUG"};
    // act
    const studio::StartupOptions options = studio::parse_startup_options(arguments);
    // assert: every override reached its field
    EXPECT_EQ(options.xyce_path, "/bin/echo");
    EXPECT_EQ(options.netlist, std::filesystem::path("board.cir"));
    ASSERT_EQ(options.datasets.size(), 2U);
    EXPECT_EQ(options.datasets[0], std::filesystem::path("out.csv"));
    EXPECT_EQ(options.datasets[1], std::filesystem::path("other.csv"));
    EXPECT_EQ(options.log_level, "debug");
}

TEST(StudioStartupOptionsChecks, the_short_log_flag_is_accepted) {
    // arrange: the short spelling the legacy frontend offers
    const std::vector<std::string_view> arguments = {"-l", "warn"};
    // act
    const studio::StartupOptions options = studio::parse_startup_options(arguments);
    // assert
    EXPECT_EQ(options.log_level, "warn");
}

TEST(StudioStartupOptionsChecks, a_trailing_option_without_a_value_is_ignored) {
    // arrange: options that end the command line
    const std::vector<std::string_view> arguments = {"--netlist", "--raw", "-l"};
    // act
    const studio::StartupOptions options = studio::parse_startup_options(arguments);
    // assert: nothing was overridden
    EXPECT_TRUE(options.netlist.empty());
    EXPECT_TRUE(options.datasets.empty());
    EXPECT_TRUE(options.log_level.empty());
}

TEST(StudioStartupOptionsChecks, a_non_netlist_override_is_rejected) {
    // arrange: a dataset named through the netlist option
    const std::vector<std::string_view> arguments = {"--netlist", "board.csv"};
    // act
    const studio::StartupOptions options = studio::parse_startup_options(arguments);
    // assert
    EXPECT_TRUE(options.netlist.empty());
}

TEST(StudioStartupOptionsChecks, unknown_arguments_are_ignored) {
    // arrange: an argument the frontend does not offer around a known one
    const std::vector<std::string_view> arguments = {"--verbose", "loose.cir", "--netlist=run.cir"};
    // act
    const studio::StartupOptions options = studio::parse_startup_options(arguments);
    // assert
    EXPECT_EQ(options.netlist, std::filesystem::path("run.cir"));
    EXPECT_TRUE(options.datasets.empty());
}

TEST(StudioStartupOptionsChecks, the_last_netlist_wins) {
    // arrange: two netlist overrides
    const std::vector<std::string_view> arguments = {"--netlist", "one.cir", "--netlist", "two.cir"};
    // act
    const studio::StartupOptions options = studio::parse_startup_options(arguments);
    // assert
    EXPECT_EQ(options.netlist, std::filesystem::path("two.cir"));
}

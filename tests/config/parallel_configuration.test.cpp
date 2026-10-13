#include <string>

#include <gtest/gtest.h>

#include "config/parallel_configuration.h"

TEST(ParallelConfigurationChecks, trimmed_or_empty_returns_the_trimmed_value) {
    // arrange / act
    const auto trimmed = trimmed_or_empty("  /usr/bin/env \t\n");
    // assert
    ASSERT_EQ(trimmed, "/usr/bin/env");
}

TEST(ParallelConfigurationChecks, trimmed_or_empty_returns_empty_for_whitespace_only) {
    // arrange / act
    const auto trimmed = trimmed_or_empty(" \t\r\n ");
    // assert
    ASSERT_TRUE(trimmed.empty());
}

TEST(ParallelConfigurationChecks, trimmed_or_empty_returns_empty_for_an_empty_value) {
    // arrange / act
    const auto trimmed = trimmed_or_empty("");
    // assert
    ASSERT_TRUE(trimmed.empty());
}

TEST(ParallelConfigurationChecks, describe_capabilities_reports_a_parallel_build) {
    // arrange
    const XyceCapabilities capabilities{true, "Parallel with MPI"};
    // act
    const auto state = describe_capabilities(capabilities, true);
    // assert
    EXPECT_NE(state.text.find("Parallel with MPI"), std::string::npos);
    EXPECT_TRUE(state.parallel_capable);
    EXPECT_FALSE(state.capabilities_unknown);
    EXPECT_TRUE(state.parallel_enabled);
}

TEST(ParallelConfigurationChecks, describe_capabilities_reports_a_serial_build_and_drops_the_request) {
    // arrange
    const XyceCapabilities capabilities{false, "Serial"};
    // act
    const auto state = describe_capabilities(capabilities, true);
    // assert
    EXPECT_NE(state.text.find("no MPI support"), std::string::npos);
    EXPECT_FALSE(state.parallel_capable);
    EXPECT_FALSE(state.capabilities_unknown);
    EXPECT_FALSE(state.parallel_enabled);
}

TEST(ParallelConfigurationChecks, describe_capabilities_reports_an_unclassifiable_binary) {
    // arrange / act
    const auto state = describe_capabilities(std::nullopt, true);
    // assert
    EXPECT_NE(state.text.find("Could not read"), std::string::npos);
    EXPECT_FALSE(state.parallel_capable);
    EXPECT_TRUE(state.capabilities_unknown);
    EXPECT_FALSE(state.parallel_enabled);
}

TEST(ParallelConfigurationChecks, describe_capabilities_keeps_a_disabled_request_disabled) {
    // arrange
    const XyceCapabilities capabilities{true, "Parallel with MPI"};
    // act
    const auto state = describe_capabilities(capabilities, false);
    // assert
    EXPECT_TRUE(state.parallel_capable);
    EXPECT_FALSE(state.parallel_enabled);
}

TEST(ParallelConfigurationChecks, describe_pending_capabilities_reports_the_probe_in_flight) {
    // arrange / act
    const auto state = describe_pending_capabilities(false);
    // assert
    EXPECT_NE(state.text.find("Detecting"), std::string::npos);
    EXPECT_FALSE(state.parallel_capable);
    EXPECT_FALSE(state.capabilities_unknown);
    EXPECT_FALSE(state.parallel_enabled);
}

TEST(ParallelConfigurationChecks, describe_pending_capabilities_carries_a_parallel_request_through) {
    // arrange / act — the pending round has classified nothing, so it must not clear
    // what the configuration seeded
    const auto state = describe_pending_capabilities(true);
    // assert
    EXPECT_TRUE(state.parallel_enabled);
}

TEST(ParallelConfigurationChecks, a_pending_probe_does_not_clear_a_seeded_parallel_request) {
    // arrange — a configuration that asked for a parallel run
    const XyceCapabilities capabilities{true, "Parallel with MPI"};
    // act — the pending state followed by the completed probe
    const auto pending = describe_pending_capabilities(true);
    const auto settled = describe_capabilities(capabilities, pending.parallel_enabled);
    // assert — the request survives the probe, so the panel still shows it checked
    EXPECT_TRUE(pending.parallel_enabled);
    EXPECT_TRUE(settled.parallel_enabled);
    EXPECT_TRUE(settled.parallel_capable);
}

TEST(ParallelConfigurationChecks, a_pending_probe_still_clears_the_request_for_a_serial_binary) {
    // arrange — the same seeded request, against a binary that reports no MPI support
    const XyceCapabilities capabilities{false, "Serial"};
    // act
    const auto pending = describe_pending_capabilities(true);
    const auto settled = describe_capabilities(capabilities, pending.parallel_enabled);
    // assert — the completed probe is what drops the request
    EXPECT_TRUE(pending.parallel_enabled);
    EXPECT_FALSE(settled.parallel_enabled);
    EXPECT_FALSE(settled.parallel_capable);
}

TEST(ParallelConfigurationChecks, build_form_config_accepts_a_serial_run) {
    // arrange
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, false, false, "", 4, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::None);
    EXPECT_EQ(config.xyce_executable_path(), self);
    EXPECT_FALSE(config.parallel_enabled());
}

TEST(ParallelConfigurationChecks, build_form_config_accepts_a_parallel_run) {
    // arrange
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, self, 4, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::None);
    EXPECT_TRUE(config.parallel_enabled());
    EXPECT_EQ(config.mpi_launcher_path(), self);
    EXPECT_EQ(config.process_count(), 4u);
}

TEST(ParallelConfigurationChecks, build_form_config_trims_the_paths_and_arguments) {
    // arrange
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config("  " + self + "  ", true, true, " " + self + " ", 4, false, 0, "  --oversubscribe  ", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::None);
    EXPECT_EQ(config.xyce_executable_path(), self);
    EXPECT_EQ(config.mpi_launcher_path(), self);
    EXPECT_EQ(config.mpi_launcher_arguments(), "--oversubscribe");
}

TEST(ParallelConfigurationChecks, build_form_config_drops_a_request_for_a_serial_binary) {
    // arrange — the user toggled parallel but the binary reports no MPI support
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, false, self, 4, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::None);
    EXPECT_FALSE(config.parallel_enabled());
}

TEST(ParallelConfigurationChecks, build_form_config_clamps_the_count_to_the_reported_slots) {
    // arrange — twelve logical processors but six slots offered
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, self, 12, true, 6, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::None);
    EXPECT_EQ(config.process_count(), 6u);
}

TEST(ParallelConfigurationChecks, build_form_config_keeps_the_count_when_no_slots_were_reported) {
    // arrange
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, self, 12, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::None);
    EXPECT_EQ(config.process_count(), 12u);
}

TEST(ParallelConfigurationChecks, build_form_config_raises_a_sub_floor_count_to_two) {
    // arrange
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, self, 1, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::None);
    EXPECT_EQ(config.process_count(), 2u);
}

TEST(ParallelConfigurationChecks, build_form_config_rejects_a_launcher_reporting_too_few_slots) {
    // arrange — a launcher that answered the probe but offers only a single slot
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, self, 0, true, 1, "", config);
    // assert — the accurate reason, not the misleading launcher-invalid message
    EXPECT_EQ(rejection, ParallelFormRejection::LauncherTooFewSlots);
    EXPECT_EQ(config.process_count(), 1u);
}

TEST(ParallelConfigurationChecks, build_form_config_rejects_a_launcher_reporting_zero_slots) {
    // arrange — the probe succeeded but the machine offers no slots at all
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, self, 4, true, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::LauncherTooFewSlots);
    EXPECT_EQ(config.process_count(), 0u);
}

TEST(ParallelConfigurationChecks, build_form_config_accepts_a_launcher_reporting_exactly_the_floor) {
    // arrange — two slots is the smallest launcher a parallel run can use
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, self, 8, true, 2, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::None);
    EXPECT_EQ(config.process_count(), 2u);
}

TEST(ParallelConfigurationChecks, build_form_config_raises_a_nonpositive_count_when_no_slots_were_reported) {
    // arrange
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, self, 0, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::None);
    EXPECT_EQ(config.process_count(), 2u);
}

TEST(ParallelConfigurationChecks, build_form_config_rejects_an_empty_xyce_path) {
    // arrange
    PluginConfig config;
    // act
    const auto rejection = build_form_config("   ", false, false, "", 4, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::XyceMissing);
}

TEST(ParallelConfigurationChecks, build_form_config_rejects_a_nonexistent_xyce_path) {
    // arrange
    PluginConfig config;
    // act
    const auto rejection = build_form_config("/nonexistent/path/to/Xyce", false, false, "", 4, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::XyceInvalid);
}

TEST(ParallelConfigurationChecks, build_form_config_rejects_a_parallel_run_without_a_launcher) {
    // arrange
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, "  ", 4, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::LauncherMissing);
}

TEST(ParallelConfigurationChecks, build_form_config_rejects_a_parallel_run_with_a_missing_launcher) {
    // arrange
    const auto self = std::string(testing::internal::GetArgvs()[0]);
    PluginConfig config;
    // act
    const auto rejection = build_form_config(self, true, true, "/nonexistent/path/to/mpirun", 4, false, 0, "", config);
    // assert
    EXPECT_EQ(rejection, ParallelFormRejection::LauncherInvalid);
}

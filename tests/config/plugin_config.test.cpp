#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "config/plugin_config.h"

TEST(PluginConfigChecks, is_valid_returns_false_for_empty_path) {
    // arrange
    PluginConfig config("");
    // act
    bool valid = config.is_xyce_executable_valid();
    // assert
    ASSERT_FALSE(valid);
}

TEST(PluginConfigChecks, is_valid_returns_false_for_non_existent_path) {
    // arrange
    PluginConfig config("/nonexistent/path/to/Xyce");
    // act
    bool valid = config.is_xyce_executable_valid();
    // assert
    ASSERT_FALSE(valid);
}

TEST(PluginConfigChecks, is_valid_returns_true_for_self) {
    // arrange
    std::string self_path = testing::internal::GetArgvs()[0];
    PluginConfig config(self_path);
    // act
    bool valid = config.is_xyce_executable_valid();
    // assert
    ASSERT_TRUE(valid);
}

TEST(PluginConfigChecks, discover_returns_empty_when_not_on_path) {
    // arrange / act
    std::string found = PluginConfig::discover_xyce_executable();
    // assert
    if (found.empty()) {
        SUCCEED();
    }
    else {
        PluginConfig config(found);
        ASSERT_TRUE(config.is_xyce_executable_valid());
    }
}

TEST(PluginConfigChecks, default_config_validity) {
    // arrange / act
    PluginConfig config = PluginConfig::default_config();
    // assert
    if (!config.xyce_executable_path().empty()) {
        ASSERT_TRUE(config.is_xyce_executable_valid());
    }
}

TEST(PluginConfigChecks, round_trips_xyce_path) {
    // arrange
    std::string test_path = "/usr/bin/env";
    PluginConfig original(test_path);
    // act
    original.save();
    PluginConfig loaded = PluginConfig::load();
    // assert
    ASSERT_EQ(loaded.xyce_executable_path(), test_path);
    // cleanup: restore the path to empty to avoid test pollution
    PluginConfig empty("");
    empty.save();
}

TEST(PluginConfigChecks, defaults_to_a_serial_run_with_a_single_process) {
    // arrange / act
    const PluginConfig config("");
    // assert
    EXPECT_FALSE(config.parallel_enabled());
    EXPECT_TRUE(config.mpi_launcher_path().empty());
    EXPECT_EQ(config.process_count(), 1u);
    EXPECT_TRUE(config.mpi_launcher_arguments().empty());
}

TEST(PluginConfigChecks, parallel_accessors_report_the_configured_values) {
    // arrange
    PluginConfig config("/usr/bin/env");
    // act
    config.set_parallel_enabled(true);
    config.set_mpi_launcher_path("/usr/bin/env");
    config.set_process_count(8);
    config.set_mpi_launcher_arguments("--oversubscribe");
    // assert
    EXPECT_TRUE(config.parallel_enabled());
    EXPECT_EQ(config.mpi_launcher_path(), "/usr/bin/env");
    EXPECT_EQ(config.process_count(), 8u);
    EXPECT_EQ(config.mpi_launcher_arguments(), "--oversubscribe");
}

TEST(PluginConfigChecks, parallel_configuration_is_valid_for_a_serial_run) {
    // arrange — a serial run has no launcher to validate
    PluginConfig config("/usr/bin/env");
    // act
    const auto valid = config.is_parallel_configuration_valid();
    // assert
    ASSERT_TRUE(valid);
}

TEST(PluginConfigChecks, parallel_configuration_is_valid_for_an_executable_launcher_with_several_processes) {
    // arrange
    PluginConfig config("/usr/bin/env");
    config.set_parallel_enabled(true);
    config.set_mpi_launcher_path(testing::internal::GetArgvs()[0]);
    config.set_process_count(4);
    // act
    const auto valid = config.is_parallel_configuration_valid();
    // assert
    ASSERT_TRUE(valid);
}

TEST(PluginConfigChecks, parallel_configuration_is_invalid_without_a_launcher_path) {
    // arrange
    PluginConfig config("/usr/bin/env");
    config.set_parallel_enabled(true);
    config.set_process_count(4);
    // act
    const auto valid = config.is_parallel_configuration_valid();
    // assert
    ASSERT_FALSE(valid);
}

TEST(PluginConfigChecks, parallel_configuration_is_invalid_for_a_nonexistent_launcher) {
    // arrange
    PluginConfig config("/usr/bin/env");
    config.set_parallel_enabled(true);
    config.set_mpi_launcher_path("/nonexistent/path/to/mpirun");
    config.set_process_count(4);
    // act
    const auto valid = config.is_parallel_configuration_valid();
    // assert
    ASSERT_FALSE(valid);
}

TEST(PluginConfigChecks, parallel_configuration_is_invalid_for_a_directory_launcher) {
    // arrange
    PluginConfig config("/usr/bin/env");
    config.set_parallel_enabled(true);
    config.set_mpi_launcher_path("/usr/local");
    config.set_process_count(4);
    // act
    const auto valid = config.is_parallel_configuration_valid();
    // assert
    ASSERT_FALSE(valid);
}

TEST(PluginConfigChecks, parallel_configuration_is_invalid_for_a_launcher_without_the_executable_bit) {
#if defined(_WIN32)
    GTEST_SKIP() << "Windows does not have an executable permission bit; any regular file passes validity";
#else
    // arrange — a regular file without the executable bit
    const auto path = std::filesystem::temp_directory_path() / "kicad_xyce_launcher_not_executable.sh";
    {
        std::ofstream file(path);
        file << "data";
    }
    PluginConfig config("/usr/bin/env");
    config.set_parallel_enabled(true);
    config.set_mpi_launcher_path(path.string());
    config.set_process_count(4);
    // act
    const auto valid = config.is_parallel_configuration_valid();
    // assert
    ASSERT_FALSE(valid);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(path, ec);
#endif
}

TEST(PluginConfigChecks, parallel_configuration_is_invalid_for_a_single_process) {
    // arrange
    PluginConfig config("/usr/bin/env");
    config.set_parallel_enabled(true);
    config.set_mpi_launcher_path(testing::internal::GetArgvs()[0]);
    config.set_process_count(1);
    // act
    const auto valid = config.is_parallel_configuration_valid();
    // assert
    ASSERT_FALSE(valid);
}

TEST(PluginConfigChecks, round_trips_the_parallel_configuration) {
    // arrange
    PluginConfig original(testing::internal::GetArgvs()[0]);
    original.set_parallel_enabled(true);
    original.set_mpi_launcher_path("/usr/bin/env");
    original.set_process_count(6);
    original.set_mpi_launcher_arguments("--oversubscribe -x FOO");
    // act
    original.save();
    const PluginConfig loaded = PluginConfig::load();
    // assert
    EXPECT_EQ(loaded.xyce_executable_path(), original.xyce_executable_path());
    EXPECT_TRUE(loaded.parallel_enabled());
    EXPECT_EQ(loaded.mpi_launcher_path(), "/usr/bin/env");
    EXPECT_EQ(loaded.process_count(), 6u);
    EXPECT_EQ(loaded.mpi_launcher_arguments(), "--oversubscribe -x FOO");
    // cleanup: restore the path to empty to avoid test pollution
    PluginConfig empty("");
    empty.save();
}

TEST(PluginConfigChecks, loads_a_config_file_written_before_the_parallel_keys_existed) {
    // arrange — a configuration file holding only the original key
    const auto config_directory = std::filesystem::temp_directory_path() / "xyce_studio_single_key_config";
    std::filesystem::create_directories(config_directory);
    const auto config_file = config_directory / "xyce-studio" / "config.json";
    // the configuration file always lives in the application directory below the config root
    std::filesystem::create_directories(config_file.parent_path());
    {
        std::ofstream file(config_file);
        file << "{\n    \"xyce_executable_path\": \"/usr/bin/env\"\n}\n";
    }
    const auto previous = getenv("XDG_CONFIG_HOME");
    const std::string previous_value = previous != nullptr ? previous : "";
#ifdef _WIN32
    _putenv_s("APPDATA", config_directory.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", config_directory.string().c_str(), 1);
#endif
    // act
    const auto loaded = PluginConfig::load();
#ifdef _WIN32
    _putenv_s("APPDATA", previous_value.c_str());
#else
    if (previous != nullptr)
        setenv("XDG_CONFIG_HOME", previous_value.c_str(), 1);
    else
        unsetenv("XDG_CONFIG_HOME");
#endif
    // assert — the path loads and every parallel setting keeps its default
    EXPECT_EQ(loaded.xyce_executable_path(), "/usr/bin/env");
    EXPECT_FALSE(loaded.parallel_enabled());
    EXPECT_TRUE(loaded.mpi_launcher_path().empty());
    EXPECT_EQ(loaded.process_count(), 1u);
    EXPECT_TRUE(loaded.mpi_launcher_arguments().empty());
    // cleanup
    std::error_code ec;
    std::filesystem::remove_all(config_directory, ec);
}

TEST(PluginConfigChecks, loads_a_config_file_written_with_the_legacy_key_spelling) {
    // arrange — the legacy file carries only the old key spelling and no parallel keys
    const auto config_directory = std::filesystem::temp_directory_path() / "xyce_studio_legacy_config";
    std::filesystem::create_directories(config_directory);
    const auto config_file = config_directory / "xyce-studio" / "config.json";
    // the configuration file always lives in the application directory below the config root
    std::filesystem::create_directories(config_file.parent_path());
    {
        std::ofstream file(config_file);
        file << "{\n    \"XyceExecutablePath\": \"/usr/bin/env\"\n}\n";
    }
    const auto previous = getenv("XDG_CONFIG_HOME");
    const std::string previous_value = previous != nullptr ? previous : "";
#ifdef _WIN32
    _putenv_s("APPDATA", config_directory.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", config_directory.string().c_str(), 1);
#endif
    // act
    const auto loaded = PluginConfig::load();
#ifdef _WIN32
    _putenv_s("APPDATA", previous_value.c_str());
#else
    if (previous != nullptr)
        setenv("XDG_CONFIG_HOME", previous_value.c_str(), 1);
    else
        unsetenv("XDG_CONFIG_HOME");
#endif
    // assert
    EXPECT_EQ(loaded.xyce_executable_path(), "/usr/bin/env");
    EXPECT_FALSE(loaded.parallel_enabled());
    EXPECT_EQ(loaded.process_count(), 1u);
    // cleanup
    std::error_code ec;
    std::filesystem::remove_all(config_directory, ec);
}

TEST(PluginConfigChecks, loads_a_config_file_with_malformed_parallel_values) {
    // arrange — every parallel value is of the wrong type
    const auto config_directory = std::filesystem::temp_directory_path() / "xyce_studio_malformed_config";
    std::filesystem::create_directories(config_directory);
    const auto config_file = config_directory / "xyce-studio" / "config.json";
    // the configuration file always lives in the application directory below the config root
    std::filesystem::create_directories(config_file.parent_path());
    {
        std::ofstream file(config_file);
        file << "{\n    \"xyce_executable_path\": \"/usr/bin/env\",\n    \"parallel_enabled\": \"yes\",\n    \"process_count\": \"many\"\n}\n";
    }
    const auto previous = getenv("XDG_CONFIG_HOME");
    const std::string previous_value = previous != nullptr ? previous : "";
#ifdef _WIN32
    _putenv_s("APPDATA", config_directory.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", config_directory.string().c_str(), 1);
#endif
    // act
    const auto loaded = PluginConfig::load();
#ifdef _WIN32
    _putenv_s("APPDATA", previous_value.c_str());
#else
    if (previous != nullptr)
        setenv("XDG_CONFIG_HOME", previous_value.c_str(), 1);
    else
        unsetenv("XDG_CONFIG_HOME");
#endif
    // assert
    EXPECT_EQ(loaded.xyce_executable_path(), "/usr/bin/env");
    EXPECT_FALSE(loaded.parallel_enabled());
    EXPECT_EQ(loaded.process_count(), 1u);
    // cleanup
    std::error_code ec;
    std::filesystem::remove_all(config_directory, ec);
}

TEST(PluginConfigChecks, is_valid_returns_false_for_directory) {
    // arrange
    PluginConfig config("/usr/local");
    // act
    bool valid = config.is_xyce_executable_valid();
    // assert
    ASSERT_FALSE(valid);
}

TEST(PluginConfigChecks, is_valid_returns_false_for_non_executable_file) {
#if defined(_WIN32)
    GTEST_SKIP() << "Windows does not have an executable permission bit; any regular file passes validity";
#else
    // arrange — a regular file without the executable bit
    const auto path = std::filesystem::temp_directory_path() / "kicad_xyce_not_executable.bin";
    {
        std::ofstream file(path);
        file << "data";
    }
    PluginConfig config(path.string());
    // act
    bool valid = config.is_xyce_executable_valid();
    // assert
    ASSERT_FALSE(valid);
    // cleanup
    std::error_code ec;
    std::filesystem::remove(path, ec);
#endif
}

TEST(PluginConfigChecks, is_valid_returns_true_for_executable_file) {
    // arrange — the running test binary is an executable file
    PluginConfig config(testing::internal::GetArgvs()[0]);
    // act
    bool valid = config.is_xyce_executable_valid();
    // assert
    ASSERT_TRUE(valid);
}

TEST(PluginConfigChecks, set_path_updates_the_configured_value) {
    // arrange
    PluginConfig config("");
    // act
#if defined(_WIN32)
    config.set_xyce_executable_path("C:\\Windows\\System32\\cmd.exe");
    // assert
    ASSERT_EQ(config.xyce_executable_path(), "C:\\Windows\\System32\\cmd.exe");
#else
    config.set_xyce_executable_path("/usr/bin/env");
    // assert
    ASSERT_EQ(config.xyce_executable_path(), "/usr/bin/env");
#endif
    ASSERT_TRUE(config.is_xyce_executable_valid());
}

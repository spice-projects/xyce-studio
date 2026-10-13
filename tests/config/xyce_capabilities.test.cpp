#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include <gtest/gtest.h>

#include "config/xyce_capabilities.h"

TEST(XyceCapabilitiesChecks, parse_classifies_a_parallel_build_from_its_first_line) {
    // arrange
    const std::string banner = "Parallel with MPI\nVerilog-A derived (ADMS) models\nFFT (FFTW)\n";
    // act
    const auto capabilities = parse_xyce_capabilities(banner);
    // assert
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_TRUE(capabilities->is_parallel);
    EXPECT_EQ(capabilities->capability_line, "Parallel with MPI");
}

TEST(XyceCapabilitiesChecks, parse_classifies_a_serial_build) {
    // arrange
    const std::string banner = "Serial\nVerilog-A derived (ADMS) models\nBuild compiler is C++17 compliant\n";
    // act
    const auto capabilities = parse_xyce_capabilities(banner);
    // assert
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_FALSE(capabilities->is_parallel);
    EXPECT_EQ(capabilities->capability_line, "Serial");
}

TEST(XyceCapabilitiesChecks, parse_skips_leading_blank_lines) {
    // arrange
    const std::string banner = "\n\nParallel with MPI\n";
    // act
    const auto capabilities = parse_xyce_capabilities(banner);
    // assert
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_TRUE(capabilities->is_parallel);
}

TEST(XyceCapabilitiesChecks, parse_strips_a_trailing_carriage_return) {
    // arrange
    const std::string banner = "Serial\r\nFFT (FFTW)\r\n";
    // act
    const auto capabilities = parse_xyce_capabilities(banner);
    // assert
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_EQ(capabilities->capability_line, "Serial");
}

TEST(XyceCapabilitiesChecks, parse_ignores_the_parallel_word_in_a_later_line) {
    // arrange
    const std::string banner = "Serial\nWelcome to the Xyce Parallel Electronic Simulator\n";
    // act
    const auto capabilities = parse_xyce_capabilities(banner);
    // assert
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_FALSE(capabilities->is_parallel);
}

TEST(XyceCapabilitiesChecks, parse_returns_nullopt_for_an_empty_banner) {
    // arrange / act
    const auto capabilities = parse_xyce_capabilities("");
    // assert
    EXPECT_FALSE(capabilities.has_value());
}

TEST(XyceCapabilitiesChecks, parse_returns_nullopt_for_a_banner_of_only_blank_lines) {
    // arrange / act
    const auto capabilities = parse_xyce_capabilities("\n \n\t\n");
    // assert
    EXPECT_FALSE(capabilities.has_value());
}

TEST(XyceCapabilitiesChecks, parse_slot_count_reads_the_job_map_total) {
    // arrange
    const std::string job_map = "Data for JOB prterun-host-123@1 offset 0 Total slots allocated 6\n            Num slots allocated: 6\tMax slots: 0\tNum procs: 1\n";
    // act
    const auto slots = parse_launcher_slot_count(job_map);
    // assert
    ASSERT_TRUE(slots.has_value());
    EXPECT_EQ(*slots, 6u);
}

TEST(XyceCapabilitiesChecks, parse_slot_count_accepts_a_single_digit_total) {
    // arrange
    const std::string job_map = "Total slots allocated 1";
    // act
    const auto slots = parse_launcher_slot_count(job_map);
    // assert
    ASSERT_TRUE(slots.has_value());
    EXPECT_EQ(*slots, 1u);
}

TEST(XyceCapabilitiesChecks, parse_slot_count_returns_nullopt_without_the_marker) {
    // arrange / act
    const auto slots = parse_launcher_slot_count("Invalid option given: --display map\n");
    // assert
    EXPECT_FALSE(slots.has_value());
}

TEST(XyceCapabilitiesChecks, parse_slot_count_returns_nullopt_for_an_empty_job_map) {
    // arrange / act
    const auto slots = parse_launcher_slot_count("");
    // assert
    EXPECT_FALSE(slots.has_value());
}

TEST(XyceCapabilitiesChecks, parse_slot_count_returns_nullopt_for_a_marker_without_digits) {
    // arrange / act
    const auto slots = parse_launcher_slot_count("Total slots allocated \n");
    // assert
    EXPECT_FALSE(slots.has_value());
}

TEST(XyceCapabilitiesChecks, parse_slot_count_returns_nullopt_for_a_value_too_large_for_the_type) {
    // arrange
    const std::string job_map = "Total slots allocated 99999999999999999999999";
    // act
    const auto slots = parse_launcher_slot_count(job_map);
    // assert
    EXPECT_FALSE(slots.has_value());
}

TEST(XyceCapabilitiesChecks, clamp_keeps_a_requested_count_within_the_offered_slots) {
    // arrange / act
    const auto clamped = clamp_process_count(4, 8);
    // assert
    EXPECT_EQ(clamped, 4u);
}

TEST(XyceCapabilitiesChecks, clamp_reduces_a_requested_count_beyond_the_offered_slots) {
    // arrange / act
    const auto clamped = clamp_process_count(12, 6);
    // assert
    EXPECT_EQ(clamped, 6u);
}

TEST(XyceCapabilitiesChecks, clamp_raises_a_single_process_request_to_the_parallel_floor) {
    // arrange / act
    const auto clamped = clamp_process_count(1, 8);
    // assert
    EXPECT_EQ(clamped, 2u);
}

TEST(XyceCapabilitiesChecks, clamp_keeps_the_requested_count_when_no_slot_count_was_reported) {
    // arrange / act
    const auto clamped = clamp_process_count(6, std::nullopt);
    // assert
    EXPECT_EQ(clamped, 6u);
}

TEST(XyceCapabilitiesChecks, clamp_returns_the_offered_slots_when_fewer_than_the_parallel_floor) {
    // arrange / act
    const auto clamped = clamp_process_count(8, 1);
    // assert
    EXPECT_EQ(clamped, 1u);
}

TEST(XyceCapabilitiesChecks, clamp_raises_a_zero_request_when_no_slot_count_was_reported) {
    // arrange / act
    const auto clamped = clamp_process_count(0, std::nullopt);
    // assert
    EXPECT_EQ(clamped, 2u);
}

TEST(XyceCapabilitiesChecks, probe_returns_nullopt_for_an_empty_executable_path) {
    // arrange / act
    const auto capabilities = probe_xyce_capabilities({});
    // assert
    EXPECT_FALSE(capabilities.has_value());
}

TEST(XyceCapabilitiesChecks, probe_returns_nullopt_when_the_executable_cannot_be_launched) {
    // arrange
    const auto missing = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_absent";
    // arrange / act
    const auto capabilities = probe_xyce_capabilities(missing);
    // assert
    EXPECT_FALSE(capabilities.has_value());
}

TEST(XyceCapabilitiesChecks, probe_reads_the_capability_banner_of_a_stand_in_binary) {
    // arrange
    const auto stand_in = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_parallel.sh";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\nif [ \"$1\" = \"-capabilities\" ]; then echo 'Parallel with MPI'; echo 'FFT (FFTW)'; fi\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto capabilities = probe_xyce_capabilities(stand_in);
    // assert
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_TRUE(capabilities->is_parallel);
#if !defined(_WIN32)
    std::filesystem::remove(stand_in);
#endif
}

TEST(XyceCapabilitiesChecks, probe_returns_nullopt_when_the_binary_fails_the_capability_query) {
    // arrange
    const auto stand_in = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_failing.sh";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\necho 'Netlist error'\nexit 1\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto capabilities = probe_xyce_capabilities(stand_in);
    // assert
    EXPECT_FALSE(capabilities.has_value());
#if !defined(_WIN32)
    std::filesystem::remove(stand_in);
#endif
}

TEST(XyceCapabilitiesChecks, probe_returns_nullopt_when_the_binary_prints_no_banner) {
    // arrange
    const auto stand_in = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_silent.sh";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\nexit 0\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto capabilities = probe_xyce_capabilities(stand_in);
    // assert
    EXPECT_FALSE(capabilities.has_value());
#if !defined(_WIN32)
    std::filesystem::remove(stand_in);
#endif
}

TEST(XyceCapabilitiesChecks, probe_slot_count_returns_nullopt_for_an_empty_launcher_path) {
    // arrange / act
    const auto slots = probe_launcher_slot_count({}, "Xyce");
    // assert
    EXPECT_FALSE(slots.has_value());
}

TEST(XyceCapabilitiesChecks, probe_slot_count_reads_the_total_from_a_stand_in_launcher) {
    // arrange
    const auto stand_in = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_launcher.sh";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\necho 'Data for JOB offset 0 Total slots allocated 6' 1>&2\nexit 0\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto slots = probe_launcher_slot_count(stand_in, "Xyce");
    // assert
    ASSERT_TRUE(slots.has_value());
    EXPECT_EQ(*slots, 6u);
#if !defined(_WIN32)
    std::filesystem::remove(stand_in);
#endif
}

TEST(XyceCapabilitiesChecks, probe_slot_count_falls_back_to_standard_output) {
    // arrange
    const auto stand_in = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_launcher_out.sh";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\necho 'Total slots allocated 4'\nexit 0\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto slots = probe_launcher_slot_count(stand_in, "Xyce");
    // assert
    ASSERT_TRUE(slots.has_value());
    EXPECT_EQ(*slots, 4u);
#if !defined(_WIN32)
    std::filesystem::remove(stand_in);
#endif
}

TEST(XyceCapabilitiesChecks, probe_slot_count_returns_nullopt_when_the_launcher_reports_nothing) {
    // arrange
    const auto stand_in = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_launcher_silent.sh";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\nexit 0\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto slots = probe_launcher_slot_count(stand_in, "Xyce");
    // assert
    EXPECT_FALSE(slots.has_value());
#if !defined(_WIN32)
    std::filesystem::remove(stand_in);
#endif
}

TEST(XyceCapabilitiesChecks, probe_launcher_version_returns_nullopt_for_an_empty_path) {
    // arrange / act
    const auto version = probe_launcher_version({});
    // assert
    EXPECT_FALSE(version.has_value());
}

TEST(XyceCapabilitiesChecks, probe_launcher_version_returns_nullopt_when_the_launcher_cannot_run) {
    // arrange
    const auto missing = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_absent_launcher";
    // arrange / act
    const auto version = probe_launcher_version(missing);
    // assert
    EXPECT_FALSE(version.has_value());
}

TEST(XyceCapabilitiesChecks, probe_launcher_version_reads_the_first_banner_line) {
    // arrange
    const auto stand_in = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_version.sh";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\necho 'mpirun (Open MPI) 5.0.11'\necho 'Report bugs at https://example.invalid'\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto version = probe_launcher_version(stand_in);
    // assert
    ASSERT_TRUE(version.has_value());
    EXPECT_EQ(*version, "mpirun (Open MPI) 5.0.11");
#if !defined(_WIN32)
    std::filesystem::remove(stand_in);
#endif
}

TEST(XyceCapabilitiesChecks, probe_launcher_version_falls_back_to_standard_error) {
    // arrange
    const auto stand_in = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_version_err.sh";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\necho 'mpirun (MPICH) 4.2' 1>&2\nexit 0\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto version = probe_launcher_version(stand_in);
    // assert
    ASSERT_TRUE(version.has_value());
    EXPECT_EQ(*version, "mpirun (MPICH) 4.2");
#if !defined(_WIN32)
    std::filesystem::remove(stand_in);
#endif
}

TEST(XyceCapabilitiesChecks, probe_launcher_version_returns_nullopt_for_a_silent_launcher) {
    // arrange
    const auto stand_in = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_version_silent.sh";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\nexit 0\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto version = probe_launcher_version(stand_in);
    // assert
    EXPECT_FALSE(version.has_value());
#if !defined(_WIN32)
    std::filesystem::remove(stand_in);
#endif
}

TEST(XyceCapabilitiesChecks, discover_finds_a_launcher_named_on_the_path) {
    // arrange
    const auto directory = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_path";
    std::filesystem::create_directories(directory);
    const auto stand_in = directory / "mpirun";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\necho launched\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto previous = std::getenv("PATH") != nullptr ? std::string{std::getenv("PATH")} : std::string{};
#ifdef _WIN32
    _putenv_s("PATH", directory.string().c_str());
#else
    setenv("PATH", directory.string().c_str(), 1);
#endif
    const auto discovered = discover_mpi_launcher();
#ifdef _WIN32
    _putenv_s("PATH", previous.c_str());
#else
    setenv("PATH", previous.c_str(), 1);
#endif
    // assert
    EXPECT_EQ(discovered, stand_in.string());
    std::filesystem::remove_all(directory);
}

TEST(XyceCapabilitiesChecks, discover_returns_empty_when_no_launcher_is_on_the_path) {
    // arrange
    const auto directory = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_path_empty";
    std::filesystem::create_directories(directory);
    const auto previous = std::getenv("PATH") != nullptr ? std::string{std::getenv("PATH")} : std::string{};
#ifdef _WIN32
    _putenv_s("PATH", directory.string().c_str());
#else
    setenv("PATH", directory.string().c_str(), 1);
#endif
    // act
    const auto discovered = discover_mpi_launcher();
#ifdef _WIN32
    _putenv_s("PATH", previous.c_str());
#else
    setenv("PATH", previous.c_str(), 1);
#endif
    // assert
    EXPECT_TRUE(discovered.empty());
    std::filesystem::remove_all(directory);
}

TEST(XyceCapabilitiesChecks, discover_returns_empty_without_a_path_variable) {
    // arrange
    const auto previous = std::getenv("PATH") != nullptr ? std::string{std::getenv("PATH")} : std::string{};
#ifdef _WIN32
    _putenv_s("PATH", "");
#else
    unsetenv("PATH");
#endif
    // act
    const auto discovered = discover_mpi_launcher();
#ifdef _WIN32
    _putenv_s("PATH", previous.c_str());
#else
    setenv("PATH", previous.c_str(), 1);
#endif
    // assert
    EXPECT_TRUE(discovered.empty());
}

TEST(XyceCapabilitiesChecks, discover_skips_an_empty_path_entry) {
    // arrange
    const auto directory = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_path_gap";
    std::filesystem::create_directories(directory);
    const auto stand_in = directory / "mpiexec";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "#!/bin/sh\necho launched\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0755);
#endif
    const auto path_with_gap = ":" + directory.string();
    const auto previous = std::getenv("PATH") != nullptr ? std::string{std::getenv("PATH")} : std::string{};
#ifdef _WIN32
    _putenv_s("PATH", path_with_gap.c_str());
#else
    setenv("PATH", path_with_gap.c_str(), 1);
#endif
    const auto discovered = discover_mpi_launcher();
#ifdef _WIN32
    _putenv_s("PATH", previous.c_str());
#else
    setenv("PATH", previous.c_str(), 1);
#endif
    // assert
    EXPECT_EQ(discovered, stand_in.string());
    std::filesystem::remove_all(directory);
}

TEST(XyceCapabilitiesChecks, discover_ignores_a_launcher_without_the_executable_permission) {
    // arrange
    const auto directory = std::filesystem::temp_directory_path() / "xyce_studio_capabilities_path_plain";
    std::filesystem::create_directories(directory);
    const auto stand_in = directory / "mpirun";
    // arrange / act
    {
        std::ofstream file(stand_in);
        file << "not executable\n";
    }
#if !defined(_WIN32)
    ::chmod(stand_in.c_str(), 0644);
#endif
    const auto previous = std::getenv("PATH") != nullptr ? std::string{std::getenv("PATH")} : std::string{};
#ifdef _WIN32
    _putenv_s("PATH", directory.string().c_str());
#else
    setenv("PATH", directory.string().c_str(), 1);
#endif
    const auto discovered = discover_mpi_launcher();
#ifdef _WIN32
    _putenv_s("PATH", previous.c_str());
#else
    setenv("PATH", previous.c_str(), 1);
#endif
    // assert
    EXPECT_TRUE(discovered.empty());
    std::filesystem::remove_all(directory);
}

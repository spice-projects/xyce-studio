#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include <gtest/gtest.h>

#include "config/plugin_config.h"
#include "config/xyce_capabilities.h"
#include "core/executable_probe.h"

namespace
{
    // write a stand-in script that reports the given capability banner and return its path
    std::filesystem::path write_capability_script(const std::string& name, const std::string& body) {
        // the script lives in a private directory so discovery never sees it on PATH
        const auto directory = std::filesystem::temp_directory_path() / "xyce_studio_real_probe";
        std::filesystem::create_directories(directory);
        // the script itself
        const auto script = directory / name;
        std::ofstream file(script);
        file << body;
        file.close();
#if !defined(_WIN32)
        ::chmod(script.c_str(), 0755);
#endif
        return script;
    }

    // a stand-in that reports the parallel banner Xyce prints
    std::filesystem::path write_parallel_xyce() {
        // the stand-in answers the capability flag and stays silent otherwise
        return write_capability_script("real_parallel_xyce.sh", "#!/bin/sh\nif [ \"$1\" = \"-capabilities\" ]; then echo 'Parallel with MPI'; echo 'FFT (FFTW)'; exit 0; fi\nexit 0\n");
    }

    // a stand-in that reports the serial banner Xyce prints
    std::filesystem::path write_serial_xyce() {
        // the stand-in answers the capability flag the way a serial build does
        return write_capability_script("real_serial_xyce.sh", "#!/bin/sh\nif [ \"$1\" = \"-capabilities\" ]; then echo 'Serial'; echo 'Build compiler is C++17 compliant'; exit 0; fi\nexit 0\n");
    }

    // a stand-in launcher that reports the job map an Open MPI launcher prints
    std::filesystem::path write_slot_reporting_launcher() {
        // the stand-in reports six slots, the shape of the real Open MPI output
        return write_capability_script("real_mpirun.sh", "#!/bin/sh\necho \"Data for JOB prterun-host-1@1 offset 0 Total slots allocated 6\" 1>&2\necho '            Num slots allocated: 6\tMax slots: 0\tNum procs: 1' 1>&2\nexit 0\n");
    }

    // a stand-in launcher that identifies itself like Open MPI
    std::filesystem::path write_open_mpi_launcher() {
        // the stand-in answers the version flag the way Open MPI does
        return write_capability_script("real_mpi_version.sh", "#!/bin/sh\nif [ \"$1\" = \"--version\" ]; then echo 'mpirun (Open MPI) 5.0.11'; echo 'Report bugs at https://www.open-mpi.org/community/help/'; exit 0; fi\nexit 0\n");
    }

    // a stand-in launcher answering both the job map and the version flag, the way Open MPI does
    std::filesystem::path write_full_open_mpi_launcher() {
        // the stand-in reports six slots and identifies itself as Open MPI
        return write_capability_script("real_mpi_full.sh", "#!/bin/sh\nif [ \"$1\" = \"--version\" ]; then echo 'mpirun (Open MPI) 5.0.11'; exit 0; fi\necho \"Data for JOB prterun-host-1@1 offset 0 Total slots allocated 6\" 1>&2\necho '            Num slots allocated: 6\tMax slots: 0\tNum procs: 1' 1>&2\nexit 0\n");
    }

    // a stand-in launcher that does not answer at all
    std::filesystem::path write_silent_launcher() {
        // the stand-in exits successfully without reporting anything
        return write_capability_script("real_silent_launcher.sh", "#!/bin/sh\nexit 0\n");
    }

    // remove the private stand-in directory
    void remove_stand_in_directory() {
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::temp_directory_path() / "xyce_studio_real_probe", ec);
    }
} // namespace

TEST(RealXyceCapabilityProbeChecks, classifies_a_parallel_capable_binary) {
    // arrange
    const auto binary = write_parallel_xyce();
    // act
    const auto capabilities = probe_xyce_capabilities(binary);
    // assert
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_TRUE(capabilities->is_parallel);
    EXPECT_EQ(capabilities->capability_line, "Parallel with MPI");
    // cleanup
    remove_stand_in_directory();
}

TEST(RealXyceCapabilityProbeChecks, classifies_a_serial_binary) {
    // arrange
    const auto binary = write_serial_xyce();
    // act
    const auto capabilities = probe_xyce_capabilities(binary);
    // assert
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_FALSE(capabilities->is_parallel);
    EXPECT_EQ(capabilities->capability_line, "Serial");
    // cleanup
    remove_stand_in_directory();
}

TEST(RealXyceCapabilityProbeChecks, reads_the_slot_count_a_launcher_reports) {
    // arrange
    const auto launcher = write_slot_reporting_launcher();
    const auto binary = write_parallel_xyce();
    // act
    const auto slots = probe_launcher_slot_count(launcher, binary);
    // assert
    ASSERT_TRUE(slots.has_value());
    EXPECT_EQ(*slots, 6u);
    // cleanup
    remove_stand_in_directory();
}

TEST(RealXyceCapabilityProbeChecks, reads_the_launcher_version_banner) {
    // arrange
    const auto launcher = write_open_mpi_launcher();
    // act
    const auto version = probe_launcher_version(launcher);
    // assert
    ASSERT_TRUE(version.has_value());
    EXPECT_EQ(*version, "mpirun (Open MPI) 5.0.11");
    EXPECT_TRUE(probe_text_contains(*version, "Open MPI"));
    // cleanup
    remove_stand_in_directory();
}

TEST(RealXyceCapabilityProbeChecks, returns_no_version_for_a_launcher_that_stays_silent) {
    // arrange
    const auto launcher = write_silent_launcher();
    // act
    const auto version = probe_launcher_version(launcher);
    // assert
    EXPECT_FALSE(version.has_value());
    // cleanup
    remove_stand_in_directory();
}

TEST(RealXyceCapabilityProbeChecks, returns_no_slots_for_a_launcher_that_stays_silent) {
    // arrange
    const auto launcher = write_silent_launcher();
    const auto binary = write_parallel_xyce();
    // act
    const auto slots = probe_launcher_slot_count(launcher, binary);
    // assert
    EXPECT_FALSE(slots.has_value());
    // cleanup
    remove_stand_in_directory();
}

TEST(RealXyceCapabilityProbeChecks, a_parallel_configuration_round_trips_a_usable_launcher) {
    // arrange
    const auto launcher = write_full_open_mpi_launcher();
    const auto binary = write_parallel_xyce();
    PluginConfig config(binary.string());
    config.set_parallel_enabled(true);
    config.set_mpi_launcher_path(launcher.string());
    // the slot count the launcher actually reported
    config.set_process_count(clamp_process_count(12, probe_launcher_slot_count(launcher, binary)));
    // act
    const auto valid = config.is_parallel_configuration_valid();
    // assert — twelve logical processors were clamped down to the six offered slots
    ASSERT_TRUE(valid);
    EXPECT_EQ(config.process_count(), 6u);
    // cleanup
    remove_stand_in_directory();
}

TEST(RealXyceCapabilityProbeChecks, a_capability_probe_stays_inside_its_timeout) {
    // arrange — a binary that never answers the capability query, emitting well
    // inside the budget so the capture is not a race against the kill
    const auto binary = write_capability_script("real_hanging_xyce.sh", "#!/bin/sh\nsleep 0.2\necho started\nsleep 30\n");
    // act
    const auto result = run_executable_probe(binary.string(), {"-capabilities"}, {}, std::chrono::milliseconds(2000));
    // assert — the probe gave up on its own instead of waiting the full default budget
    EXPECT_FALSE(result.exit_code.has_value());
    EXPECT_NE(result.timed_out_output.find("started"), std::string::npos);
    // cleanup
    remove_stand_in_directory();
}

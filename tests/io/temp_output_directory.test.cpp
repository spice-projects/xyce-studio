#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "io/temp_output_directory.h"

TEST(TempOutputDirectoryChecks, create_writes_the_netlist_and_release_removes_the_directory) {
    // arrange
    auto directory = TempOutputDirectory::create();
    ASSERT_TRUE(directory != nullptr);
    const auto path = directory->path();
    const auto netlist_path = directory->netlist_path();
    // act
    const bool written = directory->write_netlist("* test\n.END\n");
    std::ifstream netlist(netlist_path);
    const std::string content((std::istreambuf_iterator<char>(netlist)), std::istreambuf_iterator<char>());
    // assert
    EXPECT_TRUE(written);
    EXPECT_TRUE(std::filesystem::is_directory(path));
    EXPECT_EQ(content, "* test\n.END\n");
    directory.reset();
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(TempOutputDirectoryChecks, owning_directory_contains_its_netlist_but_not_its_parent) {
    // arrange
    const auto directory = TempOutputDirectory::create();
    ASSERT_TRUE(directory != nullptr);
    // act
    const bool owns_netlist = directory->owns(directory->netlist_path());
    const bool owns_parent = directory->owns(directory->path().parent_path());
    const bool owns_self = directory->owns(directory->path());
    // assert
    EXPECT_TRUE(owns_netlist);
    EXPECT_FALSE(owns_parent);
    EXPECT_FALSE(owns_self);
}

TEST(TempOutputDirectoryChecks, create_produces_unique_directories) {
    // arrange
    const auto first = TempOutputDirectory::create();
    const auto second = TempOutputDirectory::create();
    ASSERT_TRUE(first != nullptr);
    ASSERT_TRUE(second != nullptr);
    // act
    const auto first_path = first->path();
    const auto second_path = second->path();
    // assert
    EXPECT_NE(first_path, second_path);
    EXPECT_TRUE(std::filesystem::exists(first_path));
    EXPECT_TRUE(std::filesystem::exists(second_path));
}

#if !defined(_WIN32)
TEST(TempOutputDirectoryChecks, created_directory_is_owner_only) {
    // arrange
    const auto directory = TempOutputDirectory::create();
    ASSERT_TRUE(directory != nullptr);
    // act
    const auto permissions = std::filesystem::status(directory->path()).permissions();
    // assert
    EXPECT_NE(permissions & std::filesystem::perms::owner_all, std::filesystem::perms::none);
    EXPECT_EQ(permissions & std::filesystem::perms::group_all, std::filesystem::perms::none);
    EXPECT_EQ(permissions & std::filesystem::perms::others_all, std::filesystem::perms::none);
}
#endif

TEST(TempOutputDirectoryChecks, multiple_leases_keep_the_directory_until_the_last_release) {
    // arrange
    auto directory = TempOutputDirectory::create();
    ASSERT_TRUE(directory != nullptr);
    const auto path = directory->path();
    auto lease = directory;
    // act
    directory.reset();
    // assert
    EXPECT_TRUE(std::filesystem::exists(path));
    lease.reset();
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(TempOutputDirectoryChecks, explicitly_opened_file_pins_the_directory_until_manual_cleanup) {
    // arrange
    auto directory = TempOutputDirectory::create();
    ASSERT_TRUE(directory != nullptr);
    const auto path = directory->path();
    const auto output_path = path / "netlist.cir.raw";
    {
        std::ofstream output(output_path, std::ios::out | std::ios::binary);
        output << "output";
    }
    // act
    TempOutputDirectory::pin_path(output_path);
    directory.reset();
    // assert
    EXPECT_TRUE(std::filesystem::exists(output_path));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

#if defined(_WIN32)
TEST(TempOutputDirectoryChecks, explicitly_opened_file_with_different_case_pins_the_directory) {
    // arrange
    auto directory = TempOutputDirectory::create();
    ASSERT_TRUE(directory != nullptr);
    const auto path = directory->path();
    const auto output_path = path / "netlist.cir.raw";
    {
        std::ofstream output(output_path, std::ios::out | std::ios::binary);
        output << "output";
    }
    auto upper_path = output_path.string();
    std::transform(upper_path.begin(), upper_path.end(), upper_path.begin(), [](const unsigned char character) { return static_cast<char>(std::toupper(character)); });
    // act
    TempOutputDirectory::pin_path(upper_path);
    directory.reset();
    // assert
    EXPECT_TRUE(std::filesystem::exists(output_path));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}
#endif

TEST(TempOutputDirectoryChecks, explicitly_opened_file_outside_the_run_directory_does_not_pin_it) {
    // arrange
    auto directory = TempOutputDirectory::create();
    ASSERT_TRUE(directory != nullptr);
    const auto path = directory->path();
    const auto outside_path = std::filesystem::temp_directory_path() / "xyce-studio-temp-output-directory-outside.txt";
    std::error_code cleanup_ec;
    std::filesystem::remove(outside_path, cleanup_ec);
    {
        std::ofstream outside(outside_path, std::ios::out | std::ios::binary);
        outside << "outside";
    }
    // act
    TempOutputDirectory::pin_path(outside_path);
    directory.reset();
    // assert
    EXPECT_FALSE(std::filesystem::exists(path));
    std::error_code ec;
    std::filesystem::remove(outside_path, ec);
}

TEST(TempOutputDirectoryChecks, write_netlist_fails_when_the_directory_is_gone) {
    // arrange
    auto directory = TempOutputDirectory::create();
    ASSERT_TRUE(directory != nullptr);
    std::error_code ec;
    std::filesystem::remove_all(directory->path(), ec);
    // act
    const bool written = directory->write_netlist("* test\n.END\n");
    // assert
    EXPECT_FALSE(written);
}

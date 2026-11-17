#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "kicad/kicad_session.h"

TEST(KiCadSessionChecks, an_unreachable_socket_reports_no_session) {
    // arrange: a plugin launch whose project is valid but whose API socket is gone
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "kicad-session-unreachable";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    {
        std::ofstream schematic(root / "board.kicad_sch");
        schematic << "(kicad_sch)";
    }
#if defined(_WIN32)
    _putenv_s("KICAD_API_SOCKET", "ipc:///tmp/xyce-studio-no-server.sock");
    _putenv_s("KICAD_API_TOKEN", "token");
    _putenv_s("KIPRJMOD", root.string().c_str());
#else
    ::setenv("KICAD_API_SOCKET", "ipc:///tmp/xyce-studio-no-server.sock", 1);
    ::setenv("KICAD_API_TOKEN", "token", 1);
    ::setenv("KIPRJMOD", root.string().c_str(), 1);
#endif
    // act
    const std::optional<KiCadSession> session = KiCadSession::from_environment();
    // assert: a dead socket degrades to the standalone app instead of throwing
    EXPECT_FALSE(session.has_value());
    // release the environment and the scratch directory
#if defined(_WIN32)
    _putenv_s("KICAD_API_SOCKET", "");
    _putenv_s("KICAD_API_TOKEN", "");
    _putenv_s("KIPRJMOD", "");
#else
    ::unsetenv("KICAD_API_SOCKET");
    ::unsetenv("KICAD_API_TOKEN");
    ::unsetenv("KIPRJMOD");
#endif
    std::filesystem::remove_all(root, error);
}

TEST(KiCadSessionChecks, a_missing_project_directory_reports_no_session) {
    // arrange: the API launch variables without the project directory KiCad sets
#if defined(_WIN32)
    _putenv_s("KICAD_API_SOCKET", "ipc:///tmp/xyce-studio-no-server.sock");
    _putenv_s("KICAD_API_TOKEN", "token");
    _putenv_s("KIPRJMOD", "");
#else
    ::setenv("KICAD_API_SOCKET", "ipc:///tmp/xyce-studio-no-server.sock", 1);
    ::setenv("KICAD_API_TOKEN", "token", 1);
    ::unsetenv("KIPRJMOD");
#endif
    // act
    const std::optional<KiCadSession> session = KiCadSession::from_environment();
    // assert: a launch without a project is no plugin session
    EXPECT_FALSE(session.has_value());
    // release the environment
#if defined(_WIN32)
    _putenv_s("KICAD_API_SOCKET", "");
    _putenv_s("KICAD_API_TOKEN", "");
#else
    ::unsetenv("KICAD_API_SOCKET");
    ::unsetenv("KICAD_API_TOKEN");
#endif
}

TEST(KiCadSessionChecks, an_empty_project_directory_reports_no_session) {
    // arrange: an empty project directory, which KiCad never sets
#if defined(_WIN32)
    _putenv_s("KICAD_API_SOCKET", "ipc:///tmp/xyce-studio-no-server.sock");
    _putenv_s("KICAD_API_TOKEN", "token");
    _putenv_s("KIPRJMOD", "");
#else
    ::setenv("KICAD_API_SOCKET", "ipc:///tmp/xyce-studio-no-server.sock", 1);
    ::setenv("KICAD_API_TOKEN", "token", 1);
    ::setenv("KIPRJMOD", "", 1);
#endif
    // act
    const std::optional<KiCadSession> session = KiCadSession::from_environment();
    // assert: an empty directory names no project
    EXPECT_FALSE(session.has_value());
    // release the environment
#if defined(_WIN32)
    _putenv_s("KICAD_API_SOCKET", "");
    _putenv_s("KICAD_API_TOKEN", "");
    _putenv_s("KIPRJMOD", "");
#else
    ::unsetenv("KICAD_API_SOCKET");
    ::unsetenv("KICAD_API_TOKEN");
    ::unsetenv("KIPRJMOD");
#endif
}

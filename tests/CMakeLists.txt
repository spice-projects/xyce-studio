cmake_minimum_required(VERSION 3.24)

project(xyce-studio-tests LANGUAGES CXX)

# c++ 23
set(CMAKE_CXX_STANDARD 23)
# enforce the c++23 standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# disable compiler specific things
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE TEST_FILES "./*.cpp")

message(STATUS "Collected test files:\n${TEST_FILES}")

# the test runner tests against the shared core library, so the shared application sources are not compiled again for this target
add_executable(xyce-studio-tests ${TEST_FILES})
# depend on the generated proto sources before building
add_dependencies(xyce-studio-tests kicad-proto)

# link the core library (which brings the app headers, defines, and the slint/spdlog/nng/protobuf/platform dependencies), plus gtest
target_link_libraries(xyce-studio-tests PRIVATE xyce-studio-core GTest::gtest GTest::gtest_main)

if(WIN32)
    # Copy runtime DLLs (slint_cpp.dll, vcpkg DLLs) next to the test executable
    # so ctest and direct execution find them at runtime.
    add_custom_command(TARGET xyce-studio-tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:xyce-studio-tests> $<TARGET_FILE_DIR:xyce-studio-tests> COMMAND_EXPAND_LISTS)
endif()

# enable more warnings: msvc uses /W4, gnu-style frontends use -Wall -Wextra -Wpedantic
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(xyce-studio-tests PRIVATE /W4)
else()
    target_compile_options(xyce-studio-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

# auto-discover Gtests as Ctests
include(CTest)
include(GoogleTest)
gtest_discover_tests(xyce-studio-tests DISCOVERY_TIMEOUT 60)
# add_test(NAME AllGoogleTests COMMAND xyce-studio-tests)

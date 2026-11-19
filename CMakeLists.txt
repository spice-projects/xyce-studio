cmake_minimum_required(VERSION 3.28)

# the oldest macOS the application runs on, which the build states rather than inheriting from the sdk
set(XYCE_OSX_MINIMUM "13.3")

# the application's own build states the floor in its cache, while a consumer that adds it as a subdirectory keeps its own
if(APPLE AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${XYCE_OSX_MINIMUM}" CACHE STRING "the oldest macOS the application runs on")
endif()

# the vcpkg install runs inside project(), so the floor reaches the triplet through the environment it reads
if(APPLE AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "${XYCE_OSX_MINIMUM}")
    endif()
endif()

# use overlay triplets (in .triplets) on all platforms. the macOS triplets
# build skia as a dynamic library (its static archive corrupts __unwind_info
# tables on arm64, breaking C++ exception unwinding);
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
    set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/.triplets" CACHE STRING "vcpkg overlay triplets")
endif()
if(NOT DEFINED VCPKG_OVERLAY_PORTS)
    set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/.ports" CACHE STRING "vcpkg overlay ports")
endif()

if(APPLE)
    # enable objective-c++ language for .mm files
    project(xyce-studio LANGUAGES CXX OBJCXX)
else()
    # on windows and linux, objective-c++ is not supported, so only enable c++ language
    project(xyce-studio LANGUAGES CXX)
endif()

# a consumer inherits the targets rather than the cache, so the floor is applied to them after the project
if(APPLE)
    # a floor below the application's minimum cannot work, because the dependencies are stamped with it
    if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS XYCE_OSX_MINIMUM)
        message(FATAL_ERROR "xyce-studio: CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} is below the application's minimum ${XYCE_OSX_MINIMUM}, which the dependencies and the tested surface cover. Set CMAKE_OSX_DEPLOYMENT_TARGET to ${XYCE_OSX_MINIMUM} or higher.")
    endif()
    # an empty value means the consumer has no opinion, so the application adopts its tested floor
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${XYCE_OSX_MINIMUM}")
    endif()
endif()

# c++20 is the language of the whole tree, and it is the standard the framework it builds on exposes
set(CMAKE_CXX_STANDARD 20)
# enforce the c++20 standard rather than accepting an older one silently
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# disable compiler specific extensions, because the sources are portable c++
set(CMAKE_CXX_EXTENSIONS OFF)

# the compiler contract is one target, so the studio tree cannot drift from the framework it builds on
add_library(xyce-build-flags INTERFACE)

if(MSVC)
    # msvc has no -W flags, so /W4 is the closest strictness and the conformance switches pin the standard it answers
    target_compile_options(xyce-build-flags INTERFACE /W4 /permissive- /Zc:__cplusplus /EHsc /utf-8)
    # windows 10 1809 is the supported floor, and NOMINMAX keeps the headers from defining the min and max macros
    target_compile_definitions(xyce-build-flags INTERFACE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
else()
    # shadowing is the diagnostic the tree was not held to, and a designated initializer that misses a field is a bug
    target_compile_options(xyce-build-flags INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wmissing-field-initializers)
endif()

# generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# verify that we are actually using MSVC on Windows
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "On Windows this project requires MSVC (cl.exe). "
                        "CMAKE_CXX_COMPILER='${CMAKE_CXX_COMPILER}' Compiler ID='${CMAKE_CXX_COMPILER_ID}'.\n"
                        "Possible fixes:\n"
                        "  1. Unset the CC and CXX environment variables (if they point to LLVM/Clang)\n"
                        "  2. Run CMake from a Visual Studio Developer Command Prompt (x64)\n"
                        "  3. Delete CMakeCache.txt and reconfigure\n"
                        "  4. Set the CMAKE_GENERATOR_TOOLSET cache variable to 'host=x64'")
endif()

if(APPLE)
    # objective c++ shares the c++20 standard, because the .mm files are platform code of the same tree
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

# a top-level build is the developer's tree, while a consumer that adds this tree builds the application only
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(XYCE_DEV_BUILD ON)
else()
    set(XYCE_DEV_BUILD OFF)
endif()

option(XYCE_BUILD_TESTS "Build the legacy unit test binary" ${XYCE_DEV_BUILD})
option(XYCE_BUILD_STUDIO_TESTS "Build the studio unit tests" ${XYCE_DEV_BUILD})
option(XYCE_BUILD_SLINT_FRONTEND "Build the legacy Slint frontend" ON)
option(XYCE_BUILD_STUDIO_FRONTEND "Build the new framework frontend" ON)

# a suite cannot exist without the application it exercises, so the pair is decided here rather than in every preset
if(XYCE_BUILD_STUDIO_TESTS AND NOT XYCE_BUILD_STUDIO_FRONTEND)
    message(STATUS "xyce-studio: the studio suite needs the studio frontend, so XYCE_BUILD_STUDIO_TESTS is turned off")
    set(XYCE_BUILD_STUDIO_TESTS OFF CACHE BOOL "Build the studio unit tests" FORCE)
endif()

find_package(spdlog CONFIG REQUIRED)
find_package(nng CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)

if(APPLE)
    set(CMAKE_BUILD_RPATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" "${CMAKE_BINARY_DIR}/_deps/slint-build")
endif()

# profile build type, activate with `cmake --preset debug -DCMAKE_BUILD_TYPE=Profile` && `cmake --build --preset debug`
set(CMAKE_CXX_FLAGS_PROFILE "-O2 -fprofile-instr-generate -fcoverage-mapping")
set(CMAKE_EXE_LINKER_FLAGS_PROFILE "-fprofile-instr-generate")

add_subdirectory(core)

if(XYCE_BUILD_SLINT_FRONTEND)
    add_subdirectory(slint)
endif()

if(XYCE_BUILD_STUDIO_FRONTEND)
    add_subdirectory(studio)
endif()

if(XYCE_BUILD_STUDIO_TESTS)
    add_subdirectory(studio-tests)
endif()

if(XYCE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

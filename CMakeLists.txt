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

# use the overlay triplets in .triplets on all platforms
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
    set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/.triplets" CACHE STRING "vcpkg overlay triplets")
endif()
if(NOT DEFINED VCPKG_OVERLAY_PORTS)
    # .ports carries the albor port behind the albor-package feature; a sibling checkout is used
    # through ALBOR_LOCAL_SOURCE_DIR instead of a second copy of the port
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
    target_compile_options(xyce-build-flags INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wmissing-field-initializers
        -Wconversion -Wsign-conversion)
    # a designated initializer that omits a field is the aggregate counterpart of the same rule, which only clang states
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(xyce-build-flags INTERFACE -Wmissing-designated-field-initializers)
    endif()
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

option(XYCE_BUILD_TESTS "Build the shared domain unit tests" ${XYCE_DEV_BUILD})
option(XYCE_BUILD_STUDIO_TESTS "Build the studio unit tests" ${XYCE_DEV_BUILD})

find_package(spdlog CONFIG REQUIRED)
find_package(nng CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)

# the instrumented builds are options, so a preset states the intent and every target inherits the flags
option(XYCE_COVERAGE "Instrument the tests so a coverage report can be generated" OFF)
option(XYCE_ASAN "Instrument every target with AddressSanitizer" OFF)

# msvc has no source-based coverage; the windows job collects it from the pdb with opencppcoverage instead
if(XYCE_COVERAGE AND NOT MSVC)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "XYCE_COVERAGE requires clang; configure the coverage preset with clang++")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
endif()

if(XYCE_ASAN)
    # the msvc runtime checks conflict with the sanitizer flags, so this preset stays on clang and gcc
    if(MSVC)
        message(FATAL_ERROR "XYCE_ASAN is supported on the Clang/GCC toolchains; use the normal presets on MSVC")
    endif()
    # every load and store is checked, and the frame pointer keeps the report readable
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    # the block scope check catches an object destroyed before the binding that still points at it
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fsanitize-address-use-after-scope)
    endif()
    add_link_options(-fsanitize=address)
endif()

# ctest is an entry point beside running a suite binary directly, so every suite registers from its own directory
enable_testing()

add_subdirectory(core)
add_subdirectory(studio)

if(XYCE_BUILD_STUDIO_TESTS)
    add_subdirectory(studio-tests)
endif()

if(XYCE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# the shipping app must carry no dev-only automation host, so its absence is checked rather than assumed
if(UNIX AND NOT ALBOR_AUTOMATION AND TARGET xyce-studio)
    add_test(NAME link-boundary
             COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/build/check-link-boundary.sh" "$<TARGET_FILE:xyce-studio>")
endif()

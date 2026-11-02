cmake_minimum_required(VERSION 3.25)

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
    # minimum os version for the apple binaries
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version" FORCE)
    # enable objective-c++ language for .mm files
    project(xyce-studio LANGUAGES CXX OBJCXX)
else()
    # on windows and linux, objective-c++ is not supported, so only enable c++ language
    project(xyce-studio LANGUAGES CXX)
endif()

# c++ 23
set(CMAKE_CXX_STANDARD 23)
# enforce the c++23 standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# disable compiler specific things
set(CMAKE_CXX_EXTENSIONS OFF)

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
    # enable objective-c++ language for .mm files
    enable_language(OBJCXX)
    # c++ 23 (mm files)
    set(CMAKE_OBJCXX_STANDARD 23)
    # enforce the c++23 standard
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    # disable compiler specific things
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

option(BUILD_TESTS "Build tests" ON)
option(BUILD_STUDIO_TESTS "Build the studio unit tests" OFF)
option(BUILD_SLINT_FRONTEND "Build legacy Slint frontend" ON)
option(BUILD_STUDIO_FRONTEND "Build new framework frontend" ON)

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

if(BUILD_SLINT_FRONTEND)
    add_subdirectory(slint)
endif()

if(BUILD_STUDIO_FRONTEND)
    add_subdirectory(studio)
endif()

if(BUILD_STUDIO_TESTS)
    add_subdirectory(studio-tests)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

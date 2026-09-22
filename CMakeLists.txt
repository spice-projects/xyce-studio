cmake_minimum_required(VERSION 3.25)

# use overlay triplets (in ./triplets) on all platforms. the macOS triplets
# build skia as a dynamic library (its static archive corrupts __unwind_info
# tables on arm64, breaking C++ exception unwinding);
set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/.triplets" CACHE STRING "vcpkg overlay triplets" FORCE)

if(APPLE)
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

find_package(spdlog CONFIG REQUIRED)
find_package(nng CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(unofficial-skia CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)

if(APPLE)
    # slint style
    set(SLINT_STYLE "cupertino" CACHE STRING "Slint widget style" FORCE)
elseif (WIN32)
    # slint style
    set(SLINT_STYLE "fluent" CACHE STRING "Slint widget style" FORCE)
elseif (LINUX)
    # slint style
    set(SLINT_STYLE "cupertino" CACHE STRING "Slint widget style" FORCE)
endif()

# slint renderer
set(SLINT_FEATURE_RENDERER_SKIA ON CACHE BOOL "Include Skia renderer" FORCE)

# disable unneeded Slint features to reduce Rust compilation time
set(SLINT_FEATURE_RENDERER_FEMTOVG OFF CACHE BOOL "Disable FemtoVG renderer (use Skia only)" FORCE)
set(SLINT_FEATURE_ACCESSIBILITY OFF CACHE BOOL "Disable OS accessibility APIs" FORCE)
set(SLINT_FEATURE_TESTING OFF CACHE BOOL "Disable Slint testing API" FORCE)
set(SLINT_FEATURE_SOFTWARE_RENDERER_PATH OFF CACHE BOOL "Disable path rendering in software renderer" FORCE)

# check debug build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # slint live-preview: debug builds can load/reload .slint files from disk at runtime
    set(SLINT_FEATURE_LIVE_PREVIEW ON CACHE BOOL "Enable Slint live-preview" FORCE)
    # slint mcp server: debug builds can be inspected/driven by AI agents over HTTP
    set(SLINT_FEATURE_MCP ON CACHE BOOL "Enable Slint embedded MCP server" FORCE)
    # interpreter is required for live-preview
    set(SLINT_FEATURE_INTERPRETER ON CACHE BOOL "Enable Slint interpreter" FORCE)
else()
    # slint live-preview: release/profile builds keep the ahead-of-time compiled designs
    set(SLINT_FEATURE_LIVE_PREVIEW OFF CACHE BOOL "Enable Slint live-preview" FORCE)
    # slint mcp server: disabled in release/profile builds for security reasons (no HTTP server listening on a port)
    set(SLINT_FEATURE_MCP OFF CACHE BOOL "Enable Slint embedded MCP server" FORCE)
    # interpreter is not needed when we compile .slint ahead-of-time
    set(SLINT_FEATURE_INTERPRETER OFF CACHE BOOL "Enable Slint interpreter" FORCE)
endif()

# enable rpath install_name on macOS so that the dylib is found in the build tree
set(SLINT_IS_TOPLEVEL_BUILD ON CACHE BOOL "Build Slint with rpath install_name on macOS" FORCE)

# slint
# slint: pinned to the immutable v1.18.0 tag; the release/1 moving branch
# breaks the CI dependency caches whenever upstream publishes a new release
# onto it (the FetchContent git-update rebase hits upstream conflicts and the
# new toolchain version changes the generated code and deprecation warnings)
include(FetchContent)
FetchContent_Declare(Slint GIT_REPOSITORY https://github.com/slint-ui/slint.git GIT_TAG v1.18.0 SOURCE_SUBDIR api/cpp)
FetchContent_MakeAvailable(Slint)

# slint's generated internal headers trigger -Wgnu-anonymous-struct and -Wextra-semi
# under our warning set; collect slint's interface include directories here and expose
# them as system directories on the core library below, so warnings from slint headers
# are suppressed for the core library and every consumer of it.
# slint_target_sources additionally adds the build dir itself as a plain include
# directory for the generated widget headers (main_window.h, ...); those are generated
# code too, so treat them as system headers as well (e.g. the unused private
# 'live_preview' field emitted when live-preview is enabled)
set(SLINT_SYSTEM_INCLUDE_DIRS)
get_target_property(SLINT_INTERFACE_INCLUDES Slint INTERFACE_INCLUDE_DIRECTORIES)
foreach(include_dir IN LISTS SLINT_INTERFACE_INCLUDES)
    # keep build-interface paths only; install-interface paths never apply to this build
    if(include_dir MATCHES "^\\$<BUILD_INTERFACE:(.+)>$")
        list(APPEND SLINT_SYSTEM_INCLUDE_DIRS "${CMAKE_MATCH_1}")
    endif()
endforeach()
list(APPEND SLINT_SYSTEM_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}")

# generate C++ code from the vendored KiCad API protos
set(KICAD_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/kicad/proto")
set(KICAD_PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/kicad-proto")
protobuf_generate(PROTOS "${KICAD_PROTO_DIR}/common/envelope.proto" "${KICAD_PROTO_DIR}/common/commands/base_commands.proto" "${KICAD_PROTO_DIR}/common/types/base_types.proto" IMPORT_DIRS "${KICAD_PROTO_DIR}" PROTOC_OUT_DIR "${KICAD_PROTO_OUT_DIR}" OUT_VAR KICAD_PROTO_GENERATED_SOURCES)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "src/*.cpp")

# header only library: pocketfft
find_path(POCKETFFT_INCLUDE_DIRS "pocketfft_hdronly.h")

# include should be relative to src
include_directories("src")

if(APPLE)
    # gather objective-c++ sources
    file(GLOB_RECURSE PLATFORM_SOURCES "src/*.osx.mm")
    # combine remaining cpp sources with .mm sources
    set(SOURCE_FILES ${CPP_SOURCES} ${PLATFORM_SOURCES})
elseif (WIN32)
    # gather win32 sources
    file(GLOB_RECURSE PLATFORM_SOURCES "src/*.win32.c++")
    # combine remaining cpp sources with .mm sources
    set(SOURCE_FILES ${CPP_SOURCES} ${PLATFORM_SOURCES})
elseif (LINUX)
    # opengl is required on linux
    find_package(OpenGL REQUIRED)
    # gather linux sources
    file(GLOB_RECURSE PLATFORM_SOURCES "src/*.linux.c++")
    # combine remaining cpp sources with .mm sources
    set(SOURCE_FILES ${CPP_SOURCES} ${PLATFORM_SOURCES})
endif()

# remove platform-specific main() sources from the shared library, they are compiled into the executable only
list(REMOVE_ITEM SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.osx.mm" "${CMAKE_CURRENT_SOURCE_DIR}/src/main.win32.c++" "${CMAKE_CURRENT_SOURCE_DIR}/src/main.linux.c++")

# compile the generated proto sources into a library shared by the app and tests
add_library(kicad-proto STATIC ${KICAD_PROTO_GENERATED_SOURCES})

# expose the generated proto headers
target_include_directories(kicad-proto PUBLIC "${KICAD_PROTO_OUT_DIR}")
target_link_libraries(kicad-proto PUBLIC protobuf::libprotobuf)

# clang-cl (LLVM) raises -Winvalid-offsetof when compiling protobuf's generated
# TcParser tables, which reference private impl-members on non-standard-layout
# message classes. This is generated third-party code, so silence it for this
# target only under clang-cl on Windows.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(kicad-proto PRIVATE -Wno-invalid-offsetof)
endif()

# shared application code, compiled once and linked into both consumers
add_library(xyce-studio-core STATIC ${SOURCE_FILES})
# expose the application header directory to consumers (executable and tests)
target_include_directories(xyce-studio-core PUBLIC "src")
# link the core library with the vendored pocketfft library
target_include_directories(xyce-studio-core PRIVATE ${POCKETFFT_INCLUDE_DIRS})
# expose slint's headers as system directories (see note above the slint fetch)
target_include_directories(xyce-studio-core SYSTEM PUBLIC ${SLINT_SYSTEM_INCLUDE_DIRS})

# msvc uses /W4, gnu-style frontends (g++, clang++) use -Wall -Wextra -Wpedantic
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(xyce-studio-core PRIVATE /W4)
else()
    target_compile_options(xyce-studio-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# third-party dependencies, PUBLIC so both the executable and the tests transitively link the same set of libraries
target_link_libraries(xyce-studio-core PUBLIC Slint::Slint spdlog::spdlog nng::nng kicad-proto unofficial::skia::skia nfd::nfd)

if(WIN32)
    # prevent Windows headers from defining min/max macros that conflict with std::numeric_limits
    target_compile_definitions(xyce-studio-core PRIVATE NOMINMAX)
endif()

if(APPLE)
    # link AppKit for NSApp dock icon and other cocoa apis, OpenGL for CGL (via Winit), Carbon for TIS/LMGetKbdType (via Winit)
    target_link_libraries(xyce-studio-core PUBLIC "-framework AppKit" "-framework Metal" "-framework MetalKit" "-framework GameController" "-framework UniformTypeIdentifiers" "-framework QuartzCore" "-framework OpenGL" "-framework Carbon")
    # suppress duplicate library warnings on macOS, which are emitted when linking against the same framework multiple times (e.g. AppKit and Metal)
    target_link_options(xyce-studio-core PUBLIC "-Wl,-no_warn_duplicate_libraries")
elseif(WIN32)
    # link windows graphics and system libraries required by the ui backend
    target_link_libraries(xyce-studio-core PUBLIC d3d11 d3d12 d3dcompiler dxgi gdi32 user32 shell32 ole32 oleaut32 uuid comdlg32 advapi32 version ws2_32 wininet oleacc uxtheme)
elseif(LINUX)
    target_link_libraries(xyce-studio-core PUBLIC OpenGL::GL expat)
endif()

# slint: embed images/fonts referenced with @image-url() into the binary
set_property(TARGET xyce-studio-core PROPERTY SLINT_EMBED_RESOURCES embed-files)

# only slint files in the widgets directory are compiled into the library
file(GLOB_RECURSE SLINT_WIDGET_SOURCES "src/ui/widgets/*.slint")
foreach(widget_source IN LISTS SLINT_WIDGET_SOURCES)
    # use the .slint file name (without extension) as the namespace for the generated C++ code
    get_filename_component(widget_namespace ${widget_source} NAME_WE)
    # generate C++ code from the .slint file and add it to the core library
    slint_target_sources(xyce-studio-core ${widget_source} NAMESPACE ${widget_namespace})
    # silence all warnings for this fully generated code
    if(MSVC)
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/slint_generated_${widget_namespace}_1.cpp" PROPERTIES COMPILE_OPTIONS "/W0")
    else()
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/slint_generated_${widget_namespace}_1.cpp" PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endforeach()

if(APPLE)
    # skia and slint are dynamic libraries: point the runtime loader at the vcpkg install dir and slint build dir so executables find the dylibs in the build tree
    set(CMAKE_BUILD_RPATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" "${CMAKE_CURRENT_BINARY_DIR}/_deps/slint-build")
endif()

if(APPLE)
    # # resources
    # set(APP_RESOURCES plugin-icon-64x64.png)
    # # create bundle
    # add_executable(xyce-studio MACOSX_BUNDLE src/main.osx.mm ${APP_RESOURCES})
    # # boundle properties
    # set_target_properties(xyce-studio PROPERTIES 
    #     MACOSX_BUNDLE_BUNDLE_NAME "Xyce Studio" 
    #     MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.spice-projects.xyce-studio"
    #     RESOURCE "${APP_RESOURCES}"
    #     MACOSX_BUNDLE_ICON_FILE "plugin-icon-64x64.png"
    # )
    # declare application
    add_executable(xyce-studio src/main.osx.mm)
elseif(WIN32)
    # declare application
    add_executable(xyce-studio src/main.win32.c++)
    # set Windows API version to Windows 10 for CommandLineToArgW
    target_compile_definitions(xyce-studio PRIVATE _WIN32_WINNT=0x0A00)
    # mark it as win32 executable
    set_target_properties(xyce-studio PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(LINUX)
    # declare application
    add_executable(xyce-studio src/main.linux.c++)
endif()

# link with the core library defined above
target_link_libraries(xyce-studio PRIVATE xyce-studio-core)

if(APPLE)
    set_target_properties(xyce-studio PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(xyce-studio PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

# profile build type, activate with `cmake --preset debug -DCMAKE_BUILD_TYPE=Profile` && `cmake --build --preset debug`
set(CMAKE_CXX_FLAGS_PROFILE "-O2 -fprofile-instr-generate -fcoverage-mapping")
set(CMAKE_EXE_LINKER_FLAGS_PROFILE "-fprofile-instr-generate")

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Runtime dependency gathering for CI packaging. Invoked via
# `cmake --install .build-release --component RuntimeDeps --prefix .build-release`
# during CI to copy all transitively-linked shared libraries into artifacts/
# next to the executable. The whole artifacts/ directory is uploaded.
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
install(TARGETS xyce-studio RUNTIME_DEPENDENCY_SET xyce-studio-deps RUNTIME DESTINATION artifacts COMPONENT RuntimeDeps BUNDLE DESTINATION artifacts COMPONENT RuntimeDeps)

# directories searched by the runtime dependency walk below
set(XYCE_RUNTIME_DEPENDENCY_DIRS
    $<TARGET_FILE_DIR:xyce-studio>
    ${CMAKE_BINARY_DIR}/_deps/slint-build
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib
)
if(WIN32)
    # app-local c++ runtime: msvcp140/vcruntime140(_1) are neither inbox (like
    # the api-ms-win-crt ucrt forwarders) nor part of vcpkg; they live next to
    # link.exe inside the msvc toolchain dir, so add that dir and let the walk
    # collect them next to the executable (app-local deployment of the c++
    # runtime is explicitly permitted by the visual studio license)
    get_filename_component(XYCE_MSVC_BIN_DIR "${CMAKE_LINKER}" DIRECTORY)
    list(APPEND XYCE_RUNTIME_DEPENDENCY_DIRS "${XYCE_MSVC_BIN_DIR}")
endif()

install(RUNTIME_DEPENDENCY_SET xyce-studio-deps
  DIRECTORIES ${XYCE_RUNTIME_DEPENDENCY_DIRS}
  PRE_EXCLUDE_REGEXES
    [[api-ms-.*]]
    [[ext-ms-.*]]
    [[ucrtbase.*]]
  POST_EXCLUDE_REGEXES
    [[^/usr/lib]]
    [[^/System]]
    [[.*system32/.*]]
    [[.*windows/.*]]
  RUNTIME DESTINATION artifacts COMPONENT RuntimeDeps
  LIBRARY DESTINATION artifacts COMPONENT RuntimeDeps
  FRAMEWORK DESTINATION artifacts COMPONENT RuntimeDeps
)

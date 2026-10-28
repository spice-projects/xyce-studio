cmake_minimum_required(VERSION 3.25)

# legacy Slint UI frontend target: xyce-studio

find_package(unofficial-skia CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)

if(APPLE)
    set(SLINT_STYLE "cupertino" CACHE STRING "Slint widget style" FORCE)
elseif(WIN32)
    set(SLINT_STYLE "fluent" CACHE STRING "Slint widget style" FORCE)
elseif(LINUX)
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
    set(SLINT_FEATURE_LIVE_PREVIEW ON CACHE BOOL "Enable Slint live-preview" FORCE)
    set(SLINT_FEATURE_MCP ON CACHE BOOL "Enable Slint embedded MCP server" FORCE)
    set(SLINT_FEATURE_INTERPRETER ON CACHE BOOL "Enable Slint interpreter" FORCE)
else()
    set(SLINT_FEATURE_LIVE_PREVIEW OFF CACHE BOOL "Enable Slint live-preview" FORCE)
    set(SLINT_FEATURE_MCP OFF CACHE BOOL "Enable Slint embedded MCP server" FORCE)
    set(SLINT_FEATURE_INTERPRETER OFF CACHE BOOL "Enable Slint interpreter" FORCE)
endif()

# enable rpath install_name on macOS so that the dylib is found in the build tree
set(SLINT_IS_TOPLEVEL_BUILD ON CACHE BOOL "Build Slint with rpath install_name on macOS" FORCE)

# slint: pinned to immutable v1.18.1 tag
include(FetchContent)
FetchContent_Declare(Slint GIT_REPOSITORY https://github.com/slint-ui/slint.git GIT_TAG v1.18.1 SOURCE_SUBDIR api/cpp)
FetchContent_MakeAvailable(Slint)

set(SLINT_SYSTEM_INCLUDE_DIRS)
get_target_property(SLINT_INTERFACE_INCLUDES Slint INTERFACE_INCLUDE_DIRECTORIES)
foreach(include_dir IN LISTS SLINT_INTERFACE_INCLUDES)
    if(include_dir MATCHES "^\\$<BUILD_INTERFACE:(.+)>$")
        list(APPEND SLINT_SYSTEM_INCLUDE_DIRS "${CMAKE_MATCH_1}")
    endif()
endforeach()
list(APPEND SLINT_SYSTEM_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}")

# gather cpp sources
file(GLOB_RECURSE CPP_SOURCES "src/*.cpp")

if(APPLE)
    file(GLOB_RECURSE PLATFORM_SOURCES "src/*.osx.mm")
elseif(WIN32)
    file(GLOB_RECURSE PLATFORM_SOURCES "src/*.win32.c++")
elseif(LINUX)
    find_package(OpenGL REQUIRED)
    file(GLOB_RECURSE PLATFORM_SOURCES "src/*.linux.c++")
endif()

set(SOURCE_FILES ${CPP_SOURCES} ${PLATFORM_SOURCES})
list(REMOVE_ITEM SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.osx.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.win32.c++"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.linux.c++"
)

# define xyce-studio-core static library
add_library(xyce-studio-core STATIC ${SOURCE_FILES})

target_include_directories(xyce-studio-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(xyce-studio-core SYSTEM PUBLIC ${SLINT_SYSTEM_INCLUDE_DIRS})

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(xyce-studio-core PRIVATE /W4)
else()
    target_compile_options(xyce-studio-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(xyce-studio-core PUBLIC
    xyce::core
    Slint::Slint
    unofficial::skia::skia
    nfd::nfd
)

if(WIN32)
    target_compile_definitions(xyce-studio-core PRIVATE NOMINMAX)
endif()

if(APPLE)
    target_link_libraries(xyce-studio-core PUBLIC
        "-framework AppKit"
        "-framework Metal"
        "-framework MetalKit"
        "-framework GameController"
        "-framework UniformTypeIdentifiers"
        "-framework QuartzCore"
        "-framework OpenGL"
        "-framework Carbon"
    )
    target_link_options(xyce-studio-core PUBLIC "-Wl,-no_warn_duplicate_libraries")
elseif(WIN32)
    target_link_libraries(xyce-studio-core PUBLIC
        d3d11 d3d12 d3dcompiler dxgi gdi32 user32 shell32 ole32
        oleaut32 uuid comdlg32 advapi32 version ws2_32 wininet oleacc uxtheme
    )
elseif(LINUX)
    target_link_libraries(xyce-studio-core PUBLIC OpenGL::GL expat)
endif()

set_property(TARGET xyce-studio-core PROPERTY SLINT_EMBED_RESOURCES embed-files)

# compile slint widgets into the library
file(GLOB_RECURSE SLINT_WIDGET_SOURCES "src/ui/widgets/*.slint")
foreach(widget_source IN LISTS SLINT_WIDGET_SOURCES)
    get_filename_component(widget_namespace ${widget_source} NAME_WE)
    slint_target_sources(xyce-studio-core ${widget_source} NAMESPACE ${widget_namespace})
    if(MSVC)
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/slint_generated_${widget_namespace}_1.cpp" PROPERTIES COMPILE_OPTIONS "/W0")
    else()
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/slint_generated_${widget_namespace}_1.cpp" PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endforeach()

if(APPLE)
    set(CMAKE_BUILD_RPATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" "${CMAKE_BINARY_DIR}/_deps/slint-build")
endif()

if(APPLE)
    add_executable(xyce-studio src/main.osx.mm)
elseif(WIN32)
    add_executable(xyce-studio src/main.win32.c++)
    target_compile_definitions(xyce-studio PRIVATE _WIN32_WINNT=0x0A00)
    set_target_properties(xyce-studio PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(LINUX)
    add_executable(xyce-studio src/main.linux.c++)
endif()

target_link_libraries(xyce-studio PRIVATE xyce-studio-core)
set_target_properties(xyce-studio PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(APPLE)
    set_target_properties(xyce-studio PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(xyce-studio PROPERTIES INSTALL_RPATH "$ORIGIN")
    set_target_properties(xyce-studio PROPERTIES BUILD_RPATH "$ORIGIN")
    target_link_options(xyce-studio PRIVATE "LINKER:--disable-new-dtags")
endif()

# CI packaging install rules
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

install(CODE [[file(REMOVE_RECURSE "${CMAKE_INSTALL_PREFIX}/artifacts")]] COMPONENT RuntimeDeps)
install(TARGETS xyce-studio RUNTIME_DEPENDENCY_SET xyce-studio-deps RUNTIME DESTINATION artifacts COMPONENT RuntimeDeps BUNDLE DESTINATION artifacts COMPONENT RuntimeDeps)

set(XYCE_RUNTIME_DEPENDENCY_DIRS
    $<TARGET_FILE_DIR:xyce-studio>
    ${CMAKE_BINARY_DIR}/_deps/slint-build
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib
)

if(WIN32)
    set(XYCE_APPLOCAL_CRT_DLLS "")
    foreach(XYCE_CRT_TOOL IN ITEMS "${CMAKE_CXX_COMPILER}" "${CMAKE_LINKER}")
        get_filename_component(XYCE_CRT_TOOL_DIR "${XYCE_CRT_TOOL}" DIRECTORY)
        if(XYCE_CRT_TOOL_DIR)
            file(GLOB XYCE_CRT_TOOL_DLLS "${XYCE_CRT_TOOL_DIR}/msvcp140*.dll" "${XYCE_CRT_TOOL_DIR}/vcruntime140*.dll")
            list(APPEND XYCE_APPLOCAL_CRT_DLLS ${XYCE_CRT_TOOL_DLLS})
        endif()
    endforeach()
    if(DEFINED ENV{VCToolsRedistDir})
        file(GLOB XYCE_CRT_REDIST_DLLS "$ENV{VCToolsRedistDir}x64/Microsoft.VC*/CRT/msvcp140*.dll" "$ENV{VCToolsRedistDir}x64/Microsoft.VC*/CRT/vcruntime140*.dll")
        list(APPEND XYCE_APPLOCAL_CRT_DLLS ${XYCE_CRT_REDIST_DLLS})
    endif()
    list(REMOVE_DUPLICATES XYCE_APPLOCAL_CRT_DLLS)
    if(NOT XYCE_APPLOCAL_CRT_DLLS)
        message(FATAL_ERROR "windows: app-local c++ runtime dlls (msvcp140/vcruntime140) not found next to the compiler/linker or in VCToolsRedistDir; the packaged app would not run without the vc++ redistributable")
    endif()
    install(FILES ${XYCE_APPLOCAL_CRT_DLLS} DESTINATION artifacts COMPONENT RuntimeDeps)
endif()

install(RUNTIME_DEPENDENCY_SET xyce-studio-deps
  DIRECTORIES ${XYCE_RUNTIME_DEPENDENCY_DIRS}
  PRE_EXCLUDE_REGEXES
    [[api-ms-.*]]
    [[ext-ms-.*]]
    [[vcruntime.*]]
    [[msvcp.*]]
    [[ucrtbase.*]]
    [[libc\.so\..*]]
    [[libm\.so\..*]]
    [[ld-linux-.*\.so\..*]]
    [[libpthread\.so\..*]]
    [[libdl\.so\..*]]
    [[librt\.so\..*]]
    [[libresolv\.so\..*]]
  POST_EXCLUDE_REGEXES
    [[^/usr/lib]]
    [[^/System]]
    [[.*system32/.*]]
    [[.*windows/.*]]
  RUNTIME DESTINATION artifacts COMPONENT RuntimeDeps
  LIBRARY DESTINATION artifacts COMPONENT RuntimeDeps
  FRAMEWORK DESTINATION artifacts COMPONENT RuntimeDeps
)

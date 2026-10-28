cmake_minimum_required(VERSION 3.25)

# shared domain core library: pure C++23, zero UI / Slint / Skia dependencies

# generate C++ code from the vendored KiCad API protos
set(KICAD_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/kicad/proto")
set(KICAD_PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/kicad-proto")
protobuf_generate(
    PROTOS
        "${KICAD_PROTO_DIR}/common/envelope.proto"
        "${KICAD_PROTO_DIR}/common/commands/base_commands.proto"
        "${KICAD_PROTO_DIR}/common/types/base_types.proto"
    IMPORT_DIRS "${KICAD_PROTO_DIR}"
    PROTOC_OUT_DIR "${KICAD_PROTO_OUT_DIR}"
    OUT_VAR KICAD_PROTO_GENERATED_SOURCES
)

# compile the generated proto sources into a library
add_library(kicad-proto STATIC ${KICAD_PROTO_GENERATED_SOURCES})
target_include_directories(kicad-proto PUBLIC "${KICAD_PROTO_OUT_DIR}")
target_link_libraries(kicad-proto PUBLIC protobuf::libprotobuf)

if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(kicad-proto PRIVATE -Wno-invalid-offsetof)
endif()

# gather all core domain sources
file(GLOB_RECURSE CORE_SOURCES "src/*.cpp")

# header-only library: pocketfft
find_path(POCKETFFT_INCLUDE_DIRS "pocketfft_hdronly.h")

# define xyce-core static library
add_library(xyce-core STATIC ${CORE_SOURCES})
add_library(xyce::core ALIAS xyce-core)

# expose core/src as PUBLIC include directory
target_include_directories(xyce-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(xyce-core PRIVATE ${POCKETFFT_INCLUDE_DIRS})

# compiler warnings
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(xyce-core PRIVATE /W4)
else()
    target_compile_options(xyce-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_compile_definitions(xyce-core PRIVATE NOMINMAX)
endif()

# link core dependencies
target_link_libraries(xyce-core PUBLIC
    spdlog::spdlog
    nng::nng
    kicad-proto
    protobuf::libprotobuf
)

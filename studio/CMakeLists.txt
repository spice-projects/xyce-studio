cmake_minimum_required(VERSION 3.25)

file(GLOB_RECURSE STUDIO_CPP_SOURCES "src/*.cpp")

add_executable(xyce-studio-next ${STUDIO_CPP_SOURCES})

target_include_directories(xyce-studio-next PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(xyce-studio-next PRIVATE /W4)
else()
    target_compile_options(xyce-studio-next PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_compile_definitions(xyce-studio-next PRIVATE NOMINMAX)
endif()

target_link_libraries(xyce-studio-next PRIVATE
    xyce::core
    spdlog::spdlog
)
set_target_properties(xyce-studio-next PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

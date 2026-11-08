cmake_minimum_required(VERSION 3.24)

project(studio-tests LANGUAGES CXX)

# c++ 23
set(CMAKE_CXX_STANDARD 23)
# enforce the c++23 standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# disable compiler specific things
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GTest CONFIG REQUIRED)

# the presenter is independent of the window toolkit, so the suite links it directly
file(GLOB_RECURSE STUDIO_TEST_SOURCES CONFIGURE_DEPENDS "./*.cpp")

add_executable(studio-tests
    ${STUDIO_TEST_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/presenters/main_window_presenter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/presenters/plugin_config_presenter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/presenters/simulation_parameters_presenter.cpp")

target_include_directories(studio-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src")

# enable more warnings: msvc uses /W4, gnu-style frontends use -Wall -Wextra -Wpedantic
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(studio-tests PRIVATE /W4)
else()
    target_compile_options(studio-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_compile_definitions(studio-tests PRIVATE NOMINMAX)
endif()

target_link_libraries(studio-tests PRIVATE
    xyce::core
    GTest::gtest_main)

# the suite lives beside the studio build, under the folder that names it
set_target_properties(studio-tests PROPERTIES
    OUTPUT_NAME xyce-studio-tests
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/studio-tests")

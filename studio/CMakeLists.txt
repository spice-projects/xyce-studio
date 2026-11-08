cmake_minimum_required(VERSION 3.25)

# a local albor checkout enables the dev runtime and the embedded automation host,
# which the installed release package does not carry. leave it empty to consume the
# published package (release/CI) and set it to the checkout for development.
set(ALBOR_LOCAL_SOURCE_DIR "" CACHE PATH "local albor checkout used for development")

if(ALBOR_LOCAL_SOURCE_DIR)
    # the dev configuration is the only one allowed to link automation; the
    # framework's own suites, examples and reference renderers are not part of
    # the application build.
    set(ALBOR_DEV ON CACHE BOOL "" FORCE)
    set(ALBOR_AUTOMATION ON CACHE BOOL "" FORCE)
    set(ALBOR_TESTS OFF CACHE BOOL "" FORCE)
    set(ALBOR_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ALBOR_RENDERER_SKIA OFF CACHE BOOL "" FORCE)
    set(ALBOR_RENDERER_BLEND2D OFF CACHE BOOL "" FORCE)
    add_subdirectory("${ALBOR_LOCAL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/albor" EXCLUDE_FROM_ALL)
else()
    find_package(albor CONFIG REQUIRED)
endif()

file(GLOB_RECURSE STUDIO_CPP_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

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

# the embedded font registration must be declared before any .ui component,
# because the generated facades are checked against the same face list.
albor_build_font_assets(xyce-studio-next
    DEFAULT Inter
    FONTS
        Inter regular "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
        Inter semibold "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-SemiBold.ttf"
        "JetBrains Mono" regular "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Regular.ttf")

# the shared analysis rows generate first, because the panels compile against their headers
albor_build_native_record(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/form_label.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/ground_replace_row.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/sweep_mode_row.ui" IMPORTS "form_label.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/ic_section.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/measure_section.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/options_section.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/print_section.ui" IMPORTS "form_label.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/pce_section.ui" IMPORTS "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/dc_sweep_row_editor.ui" IMPORTS "form_label.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")

# the analysis panels generate next, each importing the shared rows it uses
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/transient_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "ic_section.ui" "measure_section.ui" "options_section.ui" "pce_section.ui" "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/dc_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "ic_section.ui" "measure_section.ui" "options_section.ui" "pce_section.ui" "print_section.ui" "sweep_mode_row.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/ac_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "measure_section.ui" "options_section.ui" "print_section.ui" "sweep_mode_row.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/op_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "ic_section.ui" "options_section.ui" "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/noise_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/hb_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/lin_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "print_section.ui" "sweep_mode_row.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")

albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/main_window/main_window.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/plugin_config/plugin_config_dialog.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_parameters_dialog.ui" IMPORTS "transient_parameters_panel.ui" "dc_parameters_panel.ui" "ac_parameters_panel.ui" "op_parameters_panel.ui" "noise_parameters_panel.ui" "hb_parameters_panel.ui" "lin_parameters_panel.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")

target_link_libraries(xyce-studio-next PRIVATE xyce::core spdlog::spdlog albor)

# the dev runtime carries the automation host when ALBOR_AUTOMATION is on, which
# is what makes the real application drivable by the UI scenarios.
if(ALBOR_LOCAL_SOURCE_DIR)
    target_link_libraries(xyce-studio-next PRIVATE albor-devrt-auto)
endif()

set_target_properties(xyce-studio-next PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

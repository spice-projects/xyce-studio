# a local albor checkout enables the dev runtime and the embedded automation host,
# which the installed release package does not carry. leave it empty to consume the
# published package (release/CI) and set it to the checkout for development.
set(ALBOR_LOCAL_SOURCE_DIR "" CACHE PATH "local albor checkout used for development")

if(ALBOR_LOCAL_SOURCE_DIR)
    # the checkout is a development configuration, so it links the dev runtime
    set(ALBOR_DEV ON CACHE BOOL "" FORCE)
    # the automation host may never exist in a release link, which albor rejects at configure time
    if(CMAKE_BUILD_TYPE MATCHES "^(Debug|Profile)$")
        set(ALBOR_AUTOMATION ON CACHE BOOL "" FORCE)
    else()
        set(ALBOR_AUTOMATION OFF CACHE BOOL "" FORCE)
    endif()
    # the framework's own suites, examples and reference renderers are not part of the application build
    set(ALBOR_TESTS OFF CACHE BOOL "" FORCE)
    set(ALBOR_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${ALBOR_LOCAL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/albor" EXCLUDE_FROM_ALL)
else()
    find_package(albor CONFIG REQUIRED)
endif()

# a build without the umbrella links no window, so the platform has to name its renderer instead of failing later
if(NOT TARGET albor)
    message(FATAL_ERROR "the albor umbrella is missing on this platform; link the renderer-specific target explicitly")
endif()

file(GLOB_RECURSE STUDIO_CPP_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(xyce-studio-next ${STUDIO_CPP_SOURCES})

target_include_directories(xyce-studio-next PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# the application publishes the standard it is written in, which is also the one the framework exposes
target_compile_features(xyce-studio-next PRIVATE cxx_std_20)

# the application carries the shared compiler contract
target_link_libraries(xyce-studio-next PRIVATE xyce-build-flags)

# the automation host has no pointer command, so the dev build exposes a press trigger for click scenarios
if(ALBOR_AUTOMATION)
    target_compile_definitions(xyce-studio-next PRIVATE XYCE_STUDIO_AUTOMATION=1)
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
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/print_section.ui" IMPORTS "form_label.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/option_row_editor.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/option_package_card.ui" IMPORTS "option_row_editor.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/pce_row_editor.ui" IMPORTS "form_label.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/pce_section.ui" IMPORTS "pce_row_editor.ui" "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/dc_sweep_row_editor.ui" IMPORTS "form_label.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")

# the analysis panels generate next, each importing the shared rows it uses
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/transient_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "ic_section.ui" "measure_section.ui" "option_package_card.ui" "pce_section.ui" "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/dc_parameters_panel.ui" IMPORTS "dc_sweep_row_editor.ui" "form_label.ui" "ground_replace_row.ui" "ic_section.ui" "measure_section.ui" "option_package_card.ui" "pce_section.ui" "print_section.ui" "sweep_mode_row.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/ac_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "measure_section.ui" "option_package_card.ui" "print_section.ui" "sweep_mode_row.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/op_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "ic_section.ui" "option_package_card.ui" "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/noise_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/hb_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "print_section.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/lin_parameters_panel.ui" IMPORTS "form_label.ui" "ground_replace_row.ui" "print_section.ui" "sweep_mode_row.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")

albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/main_window/main_window.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/charts/add_plot_dialog.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/charts/step_tool_dialog.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/charts/fft_dialog.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/options/options_dialog.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/plugin_config/plugin_config_dialog.ui")
albor_build_ui_component(xyce-studio-next "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_parameters_dialog.ui" IMPORTS "transient_parameters_panel.ui" "dc_parameters_panel.ui" "ac_parameters_panel.ui" "op_parameters_panel.ui" "noise_parameters_panel.ui" "hb_parameters_panel.ui" "lin_parameters_panel.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/views/simulation_parameters/simulation_ui.manifest")

target_link_libraries(xyce-studio-next PRIVATE xyce::core spdlog::spdlog albor)

# the dev runtime carries the automation host when ALBOR_AUTOMATION is on, which
# is what makes the real application drivable by the UI scenarios.
if(ALBOR_LOCAL_SOURCE_DIR)
    target_link_libraries(xyce-studio-next PRIVATE albor-devrt-auto)
endif()

set_target_properties(xyce-studio-next PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

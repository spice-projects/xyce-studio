# the studio presenter suite; the tree's standard and project scope come from the root

find_package(GTest CONFIG REQUIRED)

# the presenter is independent of the window toolkit, so the suite links it directly
file(GLOB_RECURSE STUDIO_TEST_SOURCES CONFIGURE_DEPENDS "./*.cpp")

add_executable(studio-tests
    ${STUDIO_TEST_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/app/startup_options.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/app/studio_app.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/app/window_set.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/charts/chart_text_measurer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/charts/charts_controller.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/presenters/charts_presenter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/presenters/main_window_presenter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/presenters/options_presenter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/presenters/plugin_config_presenter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/presenters/simulation_parameters_presenter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/views/charts/add_plot_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/views/charts/chart_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/views/charts/fourier_results_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src/views/main_window/document_tabs.cpp")

target_include_directories(studio-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../studio/src")

# the suite carries the same compiler contract as the application it exercises
target_link_libraries(studio-tests PRIVATE xyce-build-flags)

if(TARGET albor)
    target_link_libraries(studio-tests PRIVATE albor)
else()
    find_package(albor CONFIG REQUIRED)
    target_link_libraries(studio-tests PRIVATE albor)
endif()

target_link_libraries(studio-tests PRIVATE
    xyce::core
    GTest::gtest_main)

# the suite lives beside the studio build, under the folder that names it
set_target_properties(studio-tests PROPERTIES
    OUTPUT_NAME xyce-studio-tests
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/studio-tests")

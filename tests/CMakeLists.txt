# the shared domain unit tests; the tree's standard and project scope come from the root

find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE TEST_FILES "./*.cpp")

message(STATUS "Collected test files:\n${TEST_FILES}")

# the suite exercises the shared domain core, which is the only library the remaining tests need
add_executable(xyce-studio-tests ${TEST_FILES})
# depend on the generated proto sources before building
add_dependencies(xyce-studio-tests kicad-proto)

# link the core library, which brings the domain headers and its spdlog/nng/protobuf dependencies, plus gtest
target_link_libraries(xyce-studio-tests PRIVATE xyce::core GTest::gtest_main)

# the suite carries the same compiler contract as the code it exercises
target_link_libraries(xyce-studio-tests PRIVATE xyce-build-flags)

if(WIN32)
    # Copy runtime DLLs next to the test executable so ctest and direct execution find them at runtime.
    add_custom_command(TARGET xyce-studio-tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:xyce-studio-tests> $<TARGET_FILE_DIR:xyce-studio-tests> COMMAND_EXPAND_LISTS)
endif()

# auto-discover Gtests as Ctests, which the root has already enabled
include(GoogleTest)
gtest_discover_tests(xyce-studio-tests DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 900)

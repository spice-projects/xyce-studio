# the albor port, which pins the source the package builds from and installs the checked-in cmake story of it (AD-19)
#
# albor is not in the vcpkg registry, so this overlay port is vendored here and fetches the published
# release from GitHub at the tag commit below. ALBOR_PORT_SOURCE_PATH overrides the fetch for a local
# pass. Bump the REF and the version together: the version invalidates a cached package from an older commit.

if(DEFINED ENV{ALBOR_PORT_SOURCE_PATH})
    # a local pass builds the checkout itself, which is announced rather than silently adopted (AD-19)
    set(local_source "$ENV{ALBOR_PORT_SOURCE_PATH}")
    if(local_source STREQUAL "" OR NOT IS_DIRECTORY "${local_source}" OR NOT EXISTS "${local_source}/CMakeLists.txt")
        message(FATAL_ERROR "albor port: ALBOR_PORT_SOURCE_PATH points at '${local_source}', which is not a framework checkout")
    endif()
    set(SOURCE_PATH "${local_source}")
    message(STATUS "albor port: building from the local checkout ${SOURCE_PATH} (unpinned)")
else()
    # pin the published v0.0.1 release tag; its commit is immutable
    vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL "https://github.com/AlborCore/albor.git"
        REF 7d2f0c6f52a780762802d667a0a49fa5eed110a7
        HEAD_REF main
    )
endif()

# the framework is only static, which a dynamic triplet's preference silently overrides without the declaration (AD-19)
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# the renderer feature the package carries is blend2d, which is the portable cpu backend (AD-26)
vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        blend2d ALBOR_RENDERER_BLEND2D
)

# linux keeps the portable renderer on, because it is the fallback beside the Vulkan umbrella and the software reference (AD-16, AD-26)
if(VCPKG_TARGET_IS_LINUX)
    list(REMOVE_ITEM FEATURE_OPTIONS "-DALBOR_RENDERER_BLEND2D=OFF")
endif()

# a pinned release from before the renderer retirement still declares the retired option, so the port
# disables it for that source; the current tree carries neither the option nor the module
set(RETIRED_RENDERER_OPTIONS "")
file(READ "${SOURCE_PATH}/CMakeLists.txt" ALBOR_ROOT_CMAKE)
if(ALBOR_ROOT_CMAKE MATCHES "ALBOR_RENDERER_SKIA")
    list(APPEND RETIRED_RENDERER_OPTIONS -DALBOR_RENDERER_SKIA=OFF)
endif()

# the port builds the framework and nothing else, because examples and the test binary are development surfaces (§2.2)
# a linux build resolves the system wayland, xkbcommon and frame libraries through pkg-config, which the port host provides;
# libdecor and the wayland protocol xmls ship in the source tree, so no libdecor package is named anywhere (AD-16, AD-19)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        ${RETIRED_RENDERER_OPTIONS}
        # the published tag may predate its version commit, so the port supplies the release version to the build (AD-19)
        -DALBOR_VERSION=${VERSION}
        -DALBOR_EXAMPLES=OFF
        -DALBOR_TESTS=OFF
)
vcpkg_cmake_install()

# a linux package without the wayland adapter or its renderer has no umbrella, so the port refuses a silently useless install (AD-16, AD-19)
if(VCPKG_TARGET_IS_LINUX)
    file(GLOB ALBOR_WAYLAND_LIBRARIES "${CURRENT_PACKAGES_DIR}/lib*/libalbor-platform-wayland.a")
    if(NOT ALBOR_WAYLAND_LIBRARIES)
        message(FATAL_ERROR "albor port: the wayland adapter was not built; install the Wayland, xkbcommon and frame development packages on the build host (see docs/linux.md)")
    endif()
    # the private frame is what makes a consumer install no libdecor package, so the package must carry its plugin object
    list(GET ALBOR_WAYLAND_LIBRARIES 0 ALBOR_WAYLAND_LIBRARY)
    execute_process(COMMAND ar t "${ALBOR_WAYLAND_LIBRARY}" OUTPUT_VARIABLE ALBOR_WAYLAND_OBJECTS RESULT_VARIABLE ALBOR_AR_RESULT ERROR_VARIABLE ALBOR_AR_ERROR)
    if(NOT ALBOR_AR_RESULT EQUAL 0)
        message(FATAL_ERROR "albor port: 'ar' could not read ${ALBOR_WAYLAND_LIBRARY} (${ALBOR_AR_ERROR}); install binutils on the build host")
    endif()
    if(NOT ALBOR_WAYLAND_OBJECTS MATCHES "libdecor-cairo\\.c\\.o")
        # the private frame is optional: without it the adapter uses the xdg-decoration fallback, so a
        # missing object must not block the package. print the members so a real packaging fault is visible.
        message(STATUS "albor port: no private libdecor frame in ${ALBOR_WAYLAND_LIBRARY}; the xdg-decoration fallback is used. archive members: ${ALBOR_WAYLAND_OBJECTS}")
    endif()
    file(GLOB ALBOR_BLEND2D_LIBRARIES "${CURRENT_PACKAGES_DIR}/lib*/libalbor-render-blend2d.a")
    if(NOT ALBOR_BLEND2D_LIBRARIES)
        message(FATAL_ERROR "albor port: the blend2d renderer was not built, so the linux umbrella is missing; the port forces ALBOR_RENDERER_BLEND2D=ON")
    endif()
endif()

# the checked-in install story puts the config, its version file and the helpers in lib/cmake/albor, whose
# relocation the fixup performs for the debug and release prefixes vcpkg installs (AD-19)
vcpkg_cmake_config_fixup(PACKAGE_NAME albor CONFIG_PATH lib/cmake/albor)

# the codegen drivers already land in tools/albor from the checked-in install rules, which is the layout a port ships (AD-19)
# the debug half never ships headers, tools or the config it carried, which the fixup consumed and the port clears (AD-19)
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/debug/tools"
)

# the usage note travels with the package, which is how a consumer learns the link contract (AD-19)
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/albor")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

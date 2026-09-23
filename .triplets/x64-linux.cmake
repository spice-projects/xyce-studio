# overlay triplet: identical to the built-in x64-linux triplet, with the
# addition of reading VCPKG_BUILD_TYPE from the environment so that only the
# requested build configuration (debug or release) is built by vcpkg.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT STREQUAL "skia")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

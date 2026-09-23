# overlay triplet: identical to the community x64-osx triplet except that the
# skia port is built as a dynamic library, matching the arm64-osx overlay
# triplet so both macOS architectures ship the same way.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)

# build cmake-based ports at the same floor as the app (see
# CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt); without it the port objects
# inherit the build host's os version and the final link warns about objects
# built for a newer macOS version than the one being linked
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.3)

if(PORT STREQUAL "skia")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

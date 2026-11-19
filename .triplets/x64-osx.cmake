# overlay triplet: identical to the community x64-osx triplet except that the
# skia port is built as a dynamic library, matching the arm64-osx overlay
# triplet so both macOS architectures ship the same way.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)

# the ports carry the same floor as the application, which exports it before the vcpkg install runs (see CMakeLists.txt)
if(DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(VCPKG_OSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}")
else()
    message(FATAL_ERROR "MACOSX_DEPLOYMENT_TARGET is not set, so the dependencies would inherit the sdk default")
endif()

if(PORT STREQUAL "skia")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

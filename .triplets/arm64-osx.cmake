# overlay triplet: identical to the built-in arm64-osx triplet except that the
# skia port is built as a dynamic library. skia's static archive corrupts the
# __unwind_info tables of any binary linked against it (LLVM bug #216154 class),
# which breaks C++ exception unwinding on arm64; loading skia from a dylib
# keeps the executable's own unwind tables intact.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

# build cmake-based ports at the same floor as the app (see
# CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt); without it the port objects
# inherit the build host's os version and the final link warns about objects
# built for a newer macOS version than the one being linked
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.3)

if(PORT STREQUAL "skia")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

#!/usr/bin/env bash
set -euo pipefail

# the fingerprint names the tools that feed vcpkg's package ABI, so a runner image roll alone does not rotate the cache
: "${VCPKG_ROOT:?VCPKG_ROOT must name the pinned vcpkg checkout}"

vcpkg_bin="$VCPKG_ROOT/vcpkg"
[ -x "$vcpkg_bin" ] || vcpkg_bin="$VCPKG_ROOT/vcpkg.exe"
vcpkg_version="$("$vcpkg_bin" version | sed -n '1p')"
if git -C "$VCPKG_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    vcpkg_identity="$(git -C "$VCPKG_ROOT" rev-parse HEAD)"
else
    vcpkg_identity=unknown
fi

{
    cmake --version | sed -n '1p'
    ninja --version 2>/dev/null || true
    printf '%s\n%s\n' "$vcpkg_identity" "$vcpkg_version"
    printf '%s\n' "${VCPKG_ABI_POLICY:-unknown-policy}"
    # the pinned toolset and sdk are the Windows stability boundary, not the image's cl.exe
    if command -v cl >/dev/null 2>&1; then
        printf '%s\n%s\n' "${MSVC_TOOLSET:-unknown-toolset}" "${WINDOWS_SDK:-unknown-sdk}"
    else
        c++ --version | sed -n '1p'
        xcodebuild -version 2>/dev/null | tr '\n' ' ' || true
        printf '\n'
        xcrun --sdk macosx --show-sdk-version 2>/dev/null || true
        printf '\n'
    fi
} | if command -v sha256sum >/dev/null 2>&1; then
    sha256sum | cut -d' ' -f1
else
    shasum -a 256 | cut -d' ' -f1
fi

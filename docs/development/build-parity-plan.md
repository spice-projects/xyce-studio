# Build Parity Plan: xyce-studio → albor's Build Contract

## 1. Overview & Objective

The studio frontend (`studio/`, `core/`, `studio-tests/`) consumes **albor** as a vcpkg
package and, for local development, as a sibling source checkout added through
`ALBOR_LOCAL_SOURCE_DIR`. The final merge puts albor's sources inside this repository, so
the build must already satisfy the contract albor requires of its consumers — otherwise the
merge commit becomes a build-system rewrite instead of a directory move plus
`add_subdirectory`.

This document plans that preparation **on the xyce-studio side only**. Every phase is
independently buildable and revertable.

### Reference: the albor contract

| Requirement | albor value |
| :--- | :--- |
| CMake | `3.28` (root and presets) |
| C++ | `20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF` |
| Objective-C++ | `20`, required, no extensions |
| macOS floor | `13.3`, stated once, handed to vcpkg through the environment |
| Windows floor | MSVC `/W4 /permissive- /Zc:__cplusplus /EHsc /utf-8`, `WINVER`/`_WIN32_WINNT`/`NTDDI_VERSION` = Windows 10 1809 |
| Warnings | `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wmissing-field-initializers` |
| Instrumentation | `ALBOR_COVERAGE`, `ALBOR_ASAN`, applied to every target |
| Tests | `enable_testing()` at root, `gtest_discover_tests(... TIMEOUT 900)` |
| Dependencies | `vcpkg-configuration.json` pins the registry baseline |
| Configurations | `debug`, `release`, `test`, `asan`, `coverage` presets over a shared hidden `base` |

### Scope

| In scope | Out of scope |
| :--- | :--- |
| Must 1–8 and Should 9–12 from the comparison | Install/export rules (item 13 — deferred) |
| Studio-side CMake, presets, manifests, docs, CI | `ALBOR_VERSION` / `vcpkg.json` version drift (item 14 — unchanged) |
| One build file under `slint/` (`vcpkg.json`) | Slint app sources and `slint/CMakeLists.txt` |

> [!IMPORTANT]
> The legacy Slint frontend remains the release path throughout this plan. Nothing here
> renames the `debug`/`release` presets, the `xyce-studio` target, or its output directory,
> because `slint_automation/slint_application.py` launches `.build-debug/xyce-studio`
> directly.

### Status Snapshot

- [x] Comparison against albor completed
- [x] Phase 0 — pre-flight decisions
- [x] Phase 1 — toolchain floor (Must 1, 2, 5)
- [x] Phase 2 — compiler contract (Must 3, 4)
- [x] Phase 3 — options and renderer (Must 7, 8)
- [x] Phase 4 — one dependency manifest (Must 6)
- [x] Phase 5 — presets and instrumentation (Should 9, 10)
- [x] Phase 6 — tests and repository checks (Should 11, 12)

Every phase landed as its own commit on `refactor/dual-root-structure`. The one open item is
`studio-release` linking, which waits on the albor package carrying `vector<T>` record
fields (item 14, deliberately unchanged).

---

## 2. Phase 0 — Pre-flight Decisions

No code changes. Three questions must be answered before any edit.

### 0.1 Confirm C++20

- [x] Flip the root standard to 20 and build both `debug` and `studio-debug`; the Phase 1
      builds are that proof.

Static analysis found no C++23-only facility in `core/src` or `studio/src`: every
`.contains()` call is on `std::map`/`std::set` (C++20), `std::format` is C++20, and
`std::span`/`std::bit_cast`/`std::source_location` are C++20. The build is the proof.

### 0.2 Decide the gtest pin

The registry baseline both repositories pin (`43643e1f5c5f…`) ships **gtest 1.17.0#3**;
`versions/g-/gtest.json` at that commit has no 1.18 entry. That does not block a
`version>=` constraint: the baseline only supplies the default version, while
`version>=` is resolved from the registry's version database, which vcpkg fetches at the
remote reference rather than the baseline commit. A dry install from the merged manifest
resolved `gtest:x64-osx@1.18.0` with the local checkout still sitting on the baseline.

Both CI checkouts (`866e9e25…`, `8c5c8843…`) carry `"version-semver": "1.18.0"` as well, so
the pin is safe on every path.

- [x] Verify availability in the CI checkouts:

      git -C "$VCPKG_ROOT" fetch -q origin
      for c in 866e9e258a76437074a5cf02d4c2d71b53bff2f8 \
               8c5c88430ac864cf4fe8de4be61f69f003d23ffe; do
          git -C "$VCPKG_ROOT" show "$c:versions/g-/gtest.json" \
              | grep -c '"version-semver": "1.18'
      done

- [x] Decide: pin `gtest >= 1.18.0`, keep the baseline unchanged. Raising it would
      invalidate every cached port for no resolution benefit.

### 0.3 Confirm the legacy manifest surface

- [x] Confirm `skia` and `nativefiledialog-extended` stay in the merged manifest until the
      Slint frontend is deleted. `skia` keeps its per-platform entries (the Linux one adds
      `freetype`/`fontconfig`); `nativefiledialog-extended` carries no platform split.

---

## 3. Phase 1 — Toolchain Floor

**Covers:** Must 1 (CMake 3.28), Must 2 (C++20), Must 5 (macOS floor).

### 3.1 CMake minimum

- [x] `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.28)`
- [x] Delete `cmake_minimum_required` from `core/`, `studio/`, `tests/`, `studio-tests/`
- [x] Delete `project(...)` from `tests/CMakeLists.txt` and `studio-tests/CMakeLists.txt`
      — they reset `PROJECT_SOURCE_DIR` and re-pin the standard
- [x] `CMakePresets.json`: add `"cmakeMinimumRequired": { "major": 3, "minor": 28, "patch": 0 }`

`slint/CMakeLists.txt` keeps its own minimum; it is redundant and disappears with the
frontend.

### 3.2 One standard

- [x] Root: `CMAKE_CXX_STANDARD 20`, `CMAKE_OBJCXX_STANDARD 20`; keep `REQUIRED ON` and
      `EXTENSIONS OFF` on both
- [x] Remove the duplicate `enable_language(OBJCXX)`; keep one `project()` call guarded for
      `APPLE`
- [x] Delete the standard blocks in `tests/` and `studio-tests/`
- [x] `core/CMakeLists.txt`: `target_compile_features(xyce-core PUBLIC cxx_std_20)`
- [x] `studio/CMakeLists.txt`: `target_compile_features(xyce-studio-next PRIVATE cxx_std_20)`

`PUBLIC` on `xyce-core` is what prevents a consumer (both test binaries and, later, the
merged app) from silently lowering the standard.

### 3.3 One macOS floor

Follow albor's pattern: state the floor once, validate instead of forcing, and hand it to
vcpkg once the triplets read it (Phase 4).

- [x] `set(XYCE_OSX_MINIMUM "13.3")`
- [x] Before `project()`: write `CMAKE_OSX_DEPLOYMENT_TARGET` when this is the top-level
      build and the consumer has no opinion
- [x] After `project()`: `FATAL_ERROR` when a consumer names a lower floor; default to
      13.3 when the consumer has no opinion
- [x] Remove the `FORCE` on `CMAKE_OSX_DEPLOYMENT_TARGET`
- [ ] **Moved to Phase 4:** the environment export and the triplet edit. Both only matter
      together, and vcpkg hashes the triplet **file contents** into every package ABI
      (confirmed with `vcpkg install --dry-run`: the old file reports "already installed",
      the edited one reports "will be rebuilt"), so the edit forces a full rebuild of every
      port including Skia. Batch it with Phase 4, which already pays that cost.

### 3.4 Documentation

- [x] `STYLE-GUIDE.md` — C++20
- [x] `README.md` — C++20, CMake 3.28
- [x] `docs/development/building.md` — C++20, CMake 3.28
- [x] `MIGRATION_PLAN.md` — the "Pure C++23" annotation
- [x] `core/CMakeLists.txt` — the header comment
- [x] `.github/workflows/default.yaml` — the "the preset is c++23" comments
- [x] `.github/copilot-instructions.md` — already says C++20, verify only

### 3.5 Ride-alongs in the same commit

Both are one-liners in files this phase already edits, and both are required for the
eventual merged configuration to configure at all.

- [x] `studio/CMakeLists.txt`: force `ALBOR_AUTOMATION` only in a debug configuration, not
      whenever a checkout path is set. albor sets `ALBOR_RELEASE` from `ALBOR_DEV`/
      `ALBOR_TEST`, so this does not trip a configure guard; it prevents a Release link
      from carrying the automation host at all.
- [x] **Dropped:** the earlier idea of repointing `tests/CMakeLists.txt` from
      `xyce-studio-core` to `xyce::core`. `tests/` is not a pure domain suite —
      `tests/ui/*.test.cpp` includes the legacy Slint headers (`ui/options_dialog_view.h`,
      `main_window::OptionRow`, `slint::SharedString`), so it genuinely links the Slint
      frontend. `XYCE_BUILD_TESTS=ON` together with `XYCE_BUILD_SLINT_FRONTEND=OFF` is
      therefore not a supported configuration until the legacy suite is retired with its
      frontend.

### 3.6 Verification

    cmake --preset debug          && cmake --build --preset debug
    cmake --preset studio-debug   && cmake --build --preset studio-debug
    ./.build-debug/tests/xyce-studio-tests
    ./.build-studio-debug/studio-tests/xyce-studio-tests
    .venv/bin/python -m unittest discover -s tests_ui -p "*_test.py"

---

## 4. Phase 2 — Compiler Contract

**Covers:** Must 3 (Windows conformance and API floor), Must 4 (warnings).

### 4.1 One INTERFACE target for the contract

The flags are duplicated in four places today. Put them in one target and link it to
`xyce-core`, `xyce-studio-next`, `xyce-studio-tests` and `studio-tests`.

```cmake
add_library(xyce-build-flags INTERFACE)
if(MSVC)
    target_compile_options(xyce-build-flags INTERFACE
        /W4 /permissive- /Zc:__cplusplus /EHsc /utf-8)
    target_compile_definitions(xyce-build-flags INTERFACE
        NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
else()
    target_compile_options(xyce-build-flags INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wmissing-field-initializers)
endif()
```

- [x] Add the target
- [x] Link it to the four studio-side targets
- [x] Delete the duplicated `target_compile_options` blocks
- [x] Delete the per-target `NOMINMAX` definitions
- [x] Fix the one diagnostic the new `-Wshadow` produced: the DC branch in
      `simulation_parameters_dialog_view.cpp` shadowed the panel record with the same name

> [!NOTE]
> **Why per-target rather than `add_compile_options`:** the legacy presets build Slint
> through `FetchContent` inside this tree, so a directory-scoped flag reaches Slint's C++.
> `/permissive-` and a `NTDDI_VERSION` floor on third-party code is not something to bundle
> into a parity commit. Once the Slint frontend is deleted this collapses into albor's
> global form.

> [!NOTE]
> `/Zc:__cplusplus` is not injected by CMake (verified against the pinned CMake 4.4.3
> modules). Without it MSVC reports `199711L`, and any feature test in albor's or a
> third-party header takes the wrong branch. This is a correctness fix, not style.

> [!NOTE]
> `-Wmissing-field-initializers` is already implied by `-Wextra`; the new diagnostic that
> will actually produce output is `-Wshadow`.

### 4.2 Phase 2b — the strict set (separate commit)

- [x] Add `-Wconversion -Wsign-conversion`
- [x] Add clang's `-Wmissing-designated-field-initializers`
- [x] Fix the resulting diagnostics
- [x] Bring `xyce-studio-core` and `xyce-studio` into the contract, which Phase 2 missed:
      the legacy library kept its own `-Wall -Wextra -Wpedantic`, its `NOMINMAX`, and the
      entry point its own `_WIN32_WINNT`

The codebase leans on designated initializers (for example
`studio/src/presenters/charts_presenter.cpp`), so expect real churn in the chart geometry.
Land it alone so a revert is clean.

The compile produced 24 diagnostics in the legacy preset and 67 in the studio preset. They
fell into three groups:

- `-Wsign-conversion`/`-Wimplicit-int-float-conversion` on indexes and counts: an explicit
  `static_cast<size_t>` where the value is already validated, at the index site rather than
  by changing a signed type that other code compares against.
- `-Wmissing-designated-field-initializers`: clang exempts a field that carries a default
  member initializer, so the optional fields of the Fourier result records
  (`norm_magnitude`, `norm_phase`, `step`, `thd`, `harmonics`, `gridsize`, `interpolation`)
  gained `= ""`, while the dialog state aggregates name the field they leave empty
  (`.expressions = {}`, `.keys = {}`, `.rows = {}`, `.values = {}`, `.bounds = {}`,
  `.layout = {}`).
- the units `albor_build_ui_component` writes: they are the generator's arithmetic, not the
  application's, so they compile with the compiler's default warning set, the same way the
  generated Slint units already do.

### 4.3 Verification

- [x] macOS: both presets build with zero warnings under clang
- [x] macOS: `release` builds with zero warnings too, which covers the `NDEBUG` branches
- [ ] Windows: verified by the `windows-latest` job

---

## 5. Phase 3 — Options and Renderer

**Covers:** Must 7 (option naming and embedded defaults), Must 8 (renderer selection).

### 5.1 Prefixed options

```cmake
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(XYCE_DEV_BUILD ON)
else()
    set(XYCE_DEV_BUILD OFF)
endif()
option(XYCE_BUILD_TESTS           "Build the legacy unit test binary" ${XYCE_DEV_BUILD})
option(XYCE_BUILD_STUDIO_TESTS    "Build the studio unit tests"        ${XYCE_DEV_BUILD})
option(XYCE_BUILD_SLINT_FRONTEND  "Build legacy Slint frontend"        ON)
option(XYCE_BUILD_STUDIO_FRONTEND "Build the new framework frontend"   ON)
```

`XYCE_BUILD_STUDIO_TESTS` additionally requires `XYCE_BUILD_STUDIO_FRONTEND`, which
reproduces today's behaviour (the `debug` preset disables the studio frontend, so its
studio tests must be off) without the preset stating it.

- [x] Rename the four options
- [x] Update every reference: `CMakePresets.json`, `MIGRATION_PLAN.md` §3.4/3.5, and any
      documentation that names them
- [x] Do not keep compatibility aliases; this is a private tree

### 5.2 Renderer selection

- [x] Delete `ALBOR_RENDERER_SKIA` — the option no longer exists (Skia was retired in
      albor commit `19eaf79`)
- [x] Delete `ALBOR_RENDERER_BLEND2D OFF` and let albor's own default apply. The default is
      ON on Linux, but OFF on macOS for a checkout: `ALBOR_DEV_BUILD` is OFF whenever albor
      is a subdirectory, which is what a fresh configure confirmed
- [x] Add a post-`add_subdirectory` guard:

```cmake
if(NOT TARGET albor)
    message(FATAL_ERROR "the albor umbrella is missing on this platform; link the renderer-specific target explicitly")
endif()
```

### 5.3 Verification

- [x] `studio-debug` still configures with no blend2d on macOS
- [ ] **Deferred to Phase 4:** a `-DALBOR_RENDERER_BLEND2D=ON` configure finds the
      dependency. It fails today at `find_package(blend2d)`, which is the one dependency
      the root manifest adds
- [x] `XYCE_BUILD_STUDIO_TESTS=ON` with `XYCE_BUILD_STUDIO_FRONTEND=OFF` configures without
      adding the studio suite

---

## 6. Phase 4 — One Dependency Manifest

**Covers:** Must 6. This is the only phase with a real CI cost.

### 6.1 Root manifest

Create `vcpkg.json` at the root: the union of `slint/vcpkg.json` and `studio/vcpkg.json`,
plus albor's runtime dependencies (needed by the in-tree albor once the overlay port is
gone), plus a transitional `albor` feature.

```jsonc
{
  "name": "xyce-studio",
  "version-string": "1.0.0",
  "dependencies": [
    // legacy frontend; deleted with it
    { "name": "skia", "version>=": "148", "platform": "osx",     "default-features": false },
    { "name": "skia", "version>=": "148", "platform": "windows", "default-features": false },
    { "name": "skia", "version>=": "148", "platform": "linux",   "default-features": false,
      "features": ["freetype", "fontconfig"] },
    { "name": "nativefiledialog-extended", "version>=": "1.3.0" },

    // shared by both roots
    { "name": "spdlog",    "version>=": "1.17.0#1", "default-features": false },
    { "name": "pocketfft", "version>=": "2024-11-30", "default-features": false },
    { "name": "gtest",     "version>=": "1.18.0", "default-features": false },
    { "name": "nng",       "version>=": "1.8.0", "default-features": false },
    { "name": "protobuf",  "version>=": "6.33.4#2", "default-features": false },

    // albor's runtime closure, so a local checkout configures without the port
    { "name": "harfbuzz", "platform": "osx",     "default-features": false,
      "features": ["freetype", "icu", "coretext"] },
    { "name": "harfbuzz", "platform": "windows", "default-features": false,
      "features": ["freetype", "icu", "directwrite", "gdi"] },
    { "name": "harfbuzz", "platform": "linux",   "default-features": false,
      "features": ["freetype", "icu"] },
    { "name": "blend2d", "version>=": "2025-03-08#1", "default-features": false,
      "features": ["jit"] },
    { "name": "bzip2",  "default-features": false },
    { "name": "icu",    "default-features": false },
    { "name": "libpng", "default-features": false },
    { "name": "zlib",   "version>=": "1.3.2", "default-features": false }
  ],
  "features": {
    "albor-package": {
      "description": "consume albor as a vcpkg port instead of the checkout named by ALBOR_LOCAL_SOURCE_DIR",
      "dependencies": ["albor"]
    }
  }
}
```

- [x] Create the manifest (gtest pin per Phase 0.2)

`spdlog` loses its `fmt` and `tz-offset` default features, because one install serves both
roots and the package's interface defines are global: the legacy frontend now compiles
against `std::format` too. Neither root names `fmt` directly, and C++20 supplies it.

### 6.2 macOS floor for the dependencies

vcpkg folds the triplet into every package ABI, so a change here can invalidate cached
ports including Skia. It lands here, in the phase that already pays for a possible rebuild.

- [x] Root, before `project()`: export the floor the build resolved, so the install process
      sees it (with the constant as the fallback when a consumer passes no target):

```cmake
if(APPLE AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "${XYCE_OSX_MINIMUM}")
    endif()
endif()
```

- [x] `.triplets/arm64-osx.cmake`, `.triplets/x64-osx.cmake`: delete the hardcoded
      `VCPKG_OSX_DEPLOYMENT_TARGET 13.3` and read `$ENV{MACOSX_DEPLOYMENT_TARGET}`, failing
      with a message when it is unset
- [x] Keep `.ports/skia/015-floor-macos-target-at-13.3.patch`; note in
      `.ports/skia/portfile.cmake` that it is the one remaining literal and dies with Skia

### 6.3 Root registry configuration

```json
{
  "default-registry": {
    "kind": "git",
    "baseline": "43643e1f5cf73db40d0d4bd610183348eb09b24e",
    "repository": "https://github.com/microsoft/vcpkg"
  },
  "overlay-ports": [ "../albor/ports/albor" ]
}
```

- [x] Create `vcpkg-configuration.json` at the root
- [x] Keep the sibling `overlay-ports` entry (relative to the repository, so it survives a
      worktree); it disappears at the merge, when `albor` is published
- [x] **Remove `overlay-triplets`** — `studio/vcpkg-configuration.json` pointed at albor's
      triplets, which would leave the Skia dynamic rule in this repository's `.triplets`
      unapplied for every studio preset

### 6.4 Delete the per-root manifests

- [x] Delete `studio/vcpkg.json` and `studio/vcpkg-configuration.json`
- [x] Delete `slint/vcpkg.json`
- [x] Delete every `VCPKG_MANIFEST_DIR` entry from `CMakePresets.json`

> A build directory configured before this phase caches `VCPKG_MANIFEST_DIR`, which now
> points at a deleted file and aborts the configure. Wipe the directory or re-run the
> configure with `-U VCPKG_MANIFEST_DIR`.

> [!WARNING]
> `slint/vcpkg.json` is the one Slint-side file this plan touches. It is a build manifest,
> not application code, and Must 6 cannot be satisfied while two manifests exist.

### 6.5 Presets and CI

- [x] `studio-debug`: no feature — the in-tree albor consumes the unconditional dependencies
- [x] `studio-release`: `"VCPKG_MANIFEST_FEATURES": "albor-package"`
- [x] Legacy presets: no feature, so CI needs no sibling checkout
- [x] `.github/workflows/default.yaml`: `vcpkgJsonGlob: slint/vcpkg.json` → `vcpkg.json`,
      and the cache keys hash the root manifest and configuration
- [ ] `.github/workflows/coverage.yml`: already references a root `vcpkg.json` and
      `vcpkg-configuration.json` (lines 47, 54, 62), which do not exist today — this phase
      repairs that workflow
- [ ] When the studio frontend enters CI, pass `--x-feature=albor-package` (or set
      `VCPKG_MANIFEST_FEATURES`) to `run-vcpkg`, because the action pre-installs before
      CMake evaluates the toolchain variable

### 6.6 Cost

The vcpkg cache key changes, so the first CI run after this lands rebuilds whatever the
new key misses, including Skia from source (roughly an hour). The legacy path also gains
`harfbuzz`, `icu`, `blend2d`, `asmjit`, `freetype`, `libpng`, `bzip2` and `zlib`. On the
development machine the first `studio-debug` configure spent 25 minutes installing them,
and the `debug` configure afterwards restored the whole set from `.vcpkg-cache`.

Land this phase alone and pre-warm the cache on a throwaway branch if the wait matters.

### 6.7 Verification

    rm -rf .build-studio-debug && cmake --preset studio-debug
    cmake --preset studio-release -U VCPKG_MANIFEST_DIR   # with ALBOR_LOCAL_SOURCE_DIR unset
    cmake --preset debug -U VCPKG_MANIFEST_DIR && cmake --build --preset debug

- [x] `studio-debug` configures from the merged manifest and builds the app
- [x] `debug` configures from the merged manifest and builds the app and its suite
- [x] `studio-release` configures: `albor-package` resolves the port, `find_package(albor)`
      succeeds, and the umbrella target exists
- [ ] `studio-release` builds. It stops at `albor-manifest-glue` with `E0309` because the
      port pins albor `fbf13915`, which predates `vector<T>` record fields (added in
      `5437649`), while `simulation_ui.manifest` already uses `vector<string>`. That is the
      deferred albor version drift (item 14), not a defect of this phase: the same port,
      package and manifest were already in place before the merge.

---

## 7. Phase 5 — Presets and Instrumentation

**Covers:** Should 9 (coverage/ASAN mechanism), Should 10 (shared base preset).

### 7.1 Base preset

- [x] Add a hidden `base` preset carrying `generator`, `binaryDir`
      (`${sourceDir}/.build-${presetName}`), the toolchain file, the ccache launchers,
      `CMAKE_EXPORT_COMPILE_COMMANDS` and `cmakeMinimumRequired: 3.28`
- [x] Make every configure preset inherit it

`cmakeMinimumRequired` already sits at the file level, where every preset inherits it, so the
base preset only carries what used to be repeated. `${presetName}` expands per inheriting
preset — `debug` keeps `.build-debug` and a new preset creates `.build-coverage`, never
`.build-base` — so no script, cache key or habit changes.

### 7.2 Instrumentation options

- [x] Add `XYCE_COVERAGE` and `XYCE_ASAN` to the root, each applying
      `add_compile_options`/`add_link_options`, refusing MSVC
- [x] Delete `CMAKE_CXX_FLAGS_PROFILE` / `CMAKE_EXE_LINKER_FLAGS_PROFILE` and the `Profile`
      build type
- [x] Add a `coverage` preset (Debug + `XYCE_COVERAGE=ON`, `.build-coverage`) and an `asan`
      preset (Debug + `XYCE_ASAN=ON`, builds the test target only)
- [x] Delete the `profile` configure and build presets

The options are set before `add_subdirectory(core)`, so the flags reach every target the
application creates. Both preset names expand to their own build directory through `base`.

### 7.3 Workflows

- [x] `coverage.yml`: `--preset profile` → `--preset coverage`; `.build-profile` →
      `.build-coverage`; keep `--target xyce-studio-tests` and `--gtest_filter=-Ui*`
- [x] `default.yaml`: replace the `macos-arm64 profile` matrix entry with `coverage` and
      add an ASAN entry (macOS only, matching albor's MSVC refusal)

Both instrumented entries skip the runtime-dependency install and the artifact upload,
because neither builds the packaged application.

### 7.4 Verification

- [x] `cmake --preset coverage && cmake --build --preset coverage`, then run the binary and
      confirm `llvm-profdata`/`llvm-cov` still resolve the profile
- [x] `cmake --preset asan`, build and run the suite clean

Coverage: 2999 tests pass, the run writes a 3.8 MB `.profraw`, `llvm-profdata merge` and
`llvm-cov export --format=lcov` both succeed, and the report names 201 source files.
ASAN: 2999 tests pass with no sanitizer report. Both builds report no compiler warning, and
`debug`/`studio-debug` still configure into `.build-debug`/`.build-studio-debug` through the
base preset.

---

## 8. Phase 6 — Tests and Repository Checks

**Covers:** Should 11 (root test registration and the GUI suite), Should 12 (link boundary
and format scope).

### 8.1 Root registration

- [x] Root: `enable_testing()` unconditional, before the `add_subdirectory` calls
- [x] `tests/` and `studio-tests/`: drop `include(CTest)`, keep `include(GoogleTest)`
- [x] Add `PROPERTIES TIMEOUT 900` to both `gtest_discover_tests` calls

`studio-tests` had no discovery call at all, so it only built a binary; it now registers its
own cases the same way the legacy suite does. `ctest` reports 3002 legacy tests and 210
studio tests on this machine.

`ctest` becomes an additional entry point; executing the test binary directly remains the
documented route in `AGENTS.md`.

### 8.2 GUI integration suite as a CTest test

```cmake
set(XYCE_UI_PYTHON "${CMAKE_SOURCE_DIR}/.venv/bin/python")
if(WIN32)
    set(XYCE_UI_PYTHON "${CMAKE_SOURCE_DIR}/.venv/Scripts/python.exe")
endif()
if(XYCE_BUILD_TESTS AND EXISTS "${XYCE_UI_PYTHON}")
    add_test(NAME ui-integration
             COMMAND "${XYCE_UI_PYTHON}" -m unittest discover -s tests_ui -p "*_test.py"
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(ui-integration PROPERTIES LABELS ui TIMEOUT 1800)
endif()
```

- [x] Add the guarded test

The guard also names `XYCE_BUILD_TESTS`, because the suite drives the legacy application: a
studio-only build must not inherit a test that launches a binary it does not produce.

> [!NOTE]
> `slint_automation/slint_application.py` launches `./.build-debug/xyce-studio`, i.e. the
> legacy app. That remains correct until the frontend switch; it is why this plan never
> renames the `debug` preset or the legacy output path.

### 8.3 Link boundary

- [x] Add `build/check-link-boundary.sh` mirroring albor's (`nm --defined-only` plus
      `grep -F`, default token `automation_host`)
- [x] Register it only where automation is expected to be absent:

```cmake
if(UNIX AND NOT ALBOR_AUTOMATION AND TARGET xyce-studio-next)
    add_test(NAME link-boundary
             COMMAND "${CMAKE_SOURCE_DIR}/build/check-link-boundary.sh" "$<TARGET_FILE:xyce-studio-next>")
endif()
```

> [!NOTE]
> The default token is the mangled namespace `albor10automation` rather than the plan's
> `automation_host`: only the initializer of the generated `automation_host_auto.cpp` unit
> carries that file-name spelling, while every host symbol carries the namespace, so the
> plan's token would miss them all. The check was run against a binary that links the host
> (fails and prints the symbol) and against the legacy application (passes).
>
> Albor's script is `nm`-based and runs on macOS/Linux. MSVC Release binaries carry no
> COFF symbols, so a Windows run would always pass; run the check on the macOS jobs only.
> The effective enforcement is the configure-time guard from Phase 3 plus `ALBOR_AUTOMATION`
> never being set on the release preset.

### 8.4 Format scope for the merged tree

- [x] Turn the hardcoded path list into a variable: `slint core studio studio-tests tests`,
      plus `albor` when that directory exists
- [x] Add albor's exclusions: `third-party/`, `albor/platform/linux/wayland/vendor/`
- [x] Accept `XYCE_COMMENT_BASE`, falling back to `ALBOR_COMMENT_BASE`
- [x] Update the header comment and `AGENTS.md`

The scope was verified with a temporary `albor/` probe: a misformatted file under `albor/`
fails the check and one under `albor/third-party/` is ignored.

### 8.5 Verification

    bash build/check-format.sh
    ctest -R 'ui-integration|link-boundary'

- [x] `bash build/check-format.sh` passes over the wider scope
- [x] `ctest -R ui-integration` in `.build-debug` passes (1735.7 s, one test)
- [x] `ctest -N -R link-boundary` lists the test in `.build-studio-release` and omits it from
      `.build-studio-debug`, where automation is linked

The release application cannot link while the albor drift of Phase 4 stands, so
`link-boundary` was exercised directly: it fails on a binary that carries the host and
passes on the legacy application, which is the same comparison the registered test makes.

---

## 9. Commit Slicing

| # | Commit | Phases | Notes |
| :--- | :--- | :--- | :--- |
| 1 | `Move the tree to the albor toolchain floor` | 1 + ride-alongs | `2efee0d`, mechanical, one concern per file |
| 2 | `Share one compiler contract across the studio targets` | 2 | `b06519c`, flags plus a small cleanup |
| 3 | `Prefix the build options and let albor pick the renderer` | 3 | `b994c72`, root plus presets |
| 4 | `Build both roots from one dependency manifest` | 4 | `ade8b64`, the expensive one — land alone |
| 5 | `Add a base preset and the instrumentation options` | 5 | `bd72e5a`, presets plus two workflows |
| 6 | `Register the suites and the repository checks` | 6 | `9038c0b`, scripts plus three CMake files |
| 7 | `Enable the conversion warnings` | 2b | `fc735e6`, churn only, separately revertable |

The subject is a plain imperative line, matching the repository's history, rather than a
Conventional Commits prefix.

## 10. Risks and Mitigations

| Risk | Mitigation |
| :--- | :--- |
| C++20 rejects something the survey missed | Phase 0.1 builds first; Phase 1 is a standalone commit |
| `/permissive-` breaks Slint's fetched C++ | flags are per-target, so the legacy frontend is untouched |
| Phase 4 invalidates the CI vcpkg cache (~1 h) | land alone; pre-warm on a throwaway branch |
| `gtest >= 1.18` cannot resolve at the pinned baseline | resolved: `version>=` reads the registry's version database, not the baseline; a dry install confirms 1.18.0 |
| Removing `overlay-triplets` changes port builds | expected — it activates the repository's own Skia rule; verify a clean configure |
| UI automation breaks | never rename the `debug` preset, its binary dir, the `xyce-studio` target or its output path |

## 11. Definition of Done

    cmake --preset debug          && cmake --build --preset debug
    cmake --preset studio-debug   && cmake --build --preset studio-debug
    cmake --preset studio-release && cmake --build --preset studio-release
    ./.build-debug/tests/xyce-studio-tests
    ./.build-studio-debug/studio-tests/xyce-studio-tests
    .venv/bin/python -m unittest discover -s tests_ui -p "*_test.py"
    bash build/check-format.sh

Plus green `default.yaml` on macOS and Windows, and a `coverage` run that still produces
the report.

Install/export rules remain absent (item 13 deferred) and no albor version string changes
(item 14 unchanged).

## 12. Appendix — Verified Facts

- Both repositories pin the same vcpkg baseline, `43643e1f5c5f…`; the local checkout sits
  exactly on it.
- That baseline's registry carries `gtest 1.17.0#3`, `blend2d 2025-03-08#1`,
  `harfbuzz 14.2.1`, `icu 78.3`, `zlib 1.3.2`, `bzip2 1.0.8`, `libpng 1.6.58`.
- A `version>=` constraint resolves from the registry's version database at the fetched
  reference, so `gtest 1.18.0` installs even while the local checkout sits on the baseline;
  the baseline's own entry only supplies the default version.
- `vcpkg format-manifest` sorts the dependency keys, so the root manifest is kept in that
  canonical order rather than the order the two old manifests used.
- The pinned CMake (4.4.3 locally, 4.4.4 in CI) does not inject `/Zc:__cplusplus`.
- `albor` is not in the pinned registry; it resolves only through the sibling
  `overlay-ports` entry, which is why the transitional feature exists.
- The `.clang-format` files of both repositories are byte-identical; the format scripts
  differ only in scope and in the environment variable name.

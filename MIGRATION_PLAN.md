# Project Plan: Dual-Root UI Framework Migration

## 1. Overview & Objective

This document outlines the architectural blueprint and step-by-step execution plan for migrating the UI to Albor, a new custom C++ UI framework, while keeping the current Slint-based implementation fully operational side-by-side.

### Primary Drivers
1. **Dialog & Window Architecture**: Slint lacks clean support for secondary OS dialog windows and multi-window workflows, forcing all dialogs into inline modal overlays inside a single monolithic window (`MainWindow.slint`). Albor, the new C++ UI framework, provides window-modal native dialogs and multi-window support.
2. **Presenter Modularization**: The current `SlintMainWindowPresenter` is a ~1,500-line monolith that manages both the main window and all 6 inline dialog panels. The new framework will replace this with dedicated, isolated presenters for each dialog and window.
3. **Linkage & Dependency Hygiene**: Slint has an LLVM exception-unwinding / RTTI bug at the Rust/C++ boundary, forcing all dependencies to be compiled as dynamic libraries via custom overlay triplets. The new framework eliminates this requirement and allows clean, static linking.
4. **CI & Packaging Continuity**: The existing GitHub Actions workflows (`.github/workflows/default.yaml`, `coverage.yml`) must continue building and packaging the Slint implementation with **identical binary outputs, preset names, and artifact layouts**. No changes to existing CI workflows are permitted during development of the new framework.
5. **Zero Downtime / Regression Protection**: Both frontends coexist as two separate roots (`slint/` and `studio/`), sharing a common UI-agnostic domain core (`core/`). The current app (`xyce-studio`) continues to build and pass all automated tests until the new app (`xyce-studio-next`) achieves complete feature parity; the new root carries its own presenter unit tests (`studio-tests/`) and UI automation (`tests_studio/`).

**Framework**: The new root is built on **Albor**, a separate C++ UI framework developed in its own repository and consumed as the `albor` vcpkg package. The `studio-debug` preset can build a sibling checkout through `ALBOR_LOCAL_SOURCE_DIR`; `studio/vcpkg-configuration.json` overlays that checkout's port and triplets for development.

### Status Snapshot (2026-11-10, through `030f4c5`)

- Phases 1–5 are complete, except the CI packaging re-verification in 4.4.
- Phase 6 is in progress: 6.1, 6.2, 6.3 and the plugin-config half of 6.6 are done; the FFT and options dialogs, Add Plot, the chart pipeline and KiCad/multi-window mode remain.
- Phase 7 has not started; the legacy Slint frontend is still the release path.
- Current coverage: 42 studio presenter unit tests (`studio-tests/`), 30 studio UI scenarios (`tests_studio/`), and the legacy suites recorded in Phase 4 (2,970 unit tests, 75 UI scenarios).

---

## 2. Target Architecture

```
xyce-studio/
├── CMakeLists.txt                         # Master CMake orchestrating core & frontends
├── CMakePresets.json                      # Presets with isolated build dirs, manifests & triplets
├── .triplets/                             # Overlay triplets for the legacy Slint build
├── .ports/                                # Overlay ports
│
├── core/                                  # SHARED DOMAIN ROOT (Pure C++23, zero UI dependencies)
│   ├── CMakeLists.txt                     # Produces static library: xyce-core
│   └── src/
│       ├── charts/                        # ChartEngine, ChartLayout, SmithChart (pure math/geometry)
│       ├── config/                        # PluginConfig persistence
│       ├── core/                          # Shared utility types
│       ├── dsp/                           # FFT, interpolation (spline, linear, pchip)
│       ├── expression/                    # AST parser, evaluator, probe names, unit utilities
│       ├── io/                            # File parsers: raw, prn, csd, csv, tecplot, touchstone
│       ├── kicad/                         # KiCad IPC connection, session, protobuf commands
│       ├── netlist/                       # Netlist parser, lexer, file/editor sources
│       ├── runner/                        # SimulationRunner with generic event dispatcher
│       └── simulation/                    # Simulation parameters, options catalog, log parser
│
├── slint/                                 # ROOT 1: legacy Slint implementation (release path)
│   ├── CMakeLists.txt                     # Target: xyce-studio (dynamic Skia & dependencies)
│   ├── vcpkg.json                         # Slint manifest (Skia, nfd, spdlog, pocketfft, etc.)
│   ├── vcpkg-configuration.json           # vcpkg baseline for the legacy build
│   └── src/
│       ├── app/                           # App singleton & platform entry points
│       ├── main.*                         # Platform main entry points
│       └── ui/                            # Monolithic presenter, views, widgets, chart renderer
│
├── studio/                                # ROOT 2: Albor implementation
│   ├── CMakeLists.txt                     # Target: xyce-studio-next (clean static linkage)
│   ├── vcpkg.json                         # Albor manifest (spdlog, nng, protobuf, albor)
│   ├── vcpkg-configuration.json           # Albor overlay port & triplets for development
│   └── src/
│       ├── main.cpp                       # Entry point & argument overrides
│       ├── app/                           # StudioApp lifecycle, embedded fonts & theme
│       ├── presenters/                    # Main window, plugin config & simulation parameters
│       ├── views/                         # Main window, plugin config & simulation dialogs
│       │   ├── main_window/
│       │   ├── plugin_config/
│       │   └── simulation_parameters/
│       └── charts/                        # Chart pipeline (pending, 6.7)
│
├── studio-tests/                          # Studio presenter unit tests (links xyce-core)
├── tests/                                 # Fast domain unit tests (links only xyce-core)
├── tests_ui/                              # Legacy Slint UI automation suite
└── tests_studio/                          # Albor UI automation suite
```

`albor/` is not part of this repository. The studio root consumes the `albor` package; for local development `studio/vcpkg-configuration.json` overlays a sibling checkout, and `ALBOR_LOCAL_SOURCE_DIR` adds it as a subdirectory. Studio views are Albor `.ui` components compiled with `albor_build_ui_component`; no Slint type crosses outside `slint/`.

---

## 3. Dependency, Triplet & CI Strategy

| Concern | Legacy Root (`slint/` / CI Standard) | New Framework Root (`studio/`) |
| :--- | :--- | :--- |
| **Executable Target** | `xyce-studio` (identical output name) | `xyce-studio-next` |
| **CMake Presets** | `debug`, `release`, `profile` (studio frontend off) | `studio-debug`, `studio-release` (Slint frontend off) |
| **Build Directory** | `.build-debug`, `.build-release`, `.build-profile` | `.build-studio-debug`, `.build-studio-release` |
| **Manifest Path** | `slint/vcpkg.json` + `slint/vcpkg-configuration.json` | `studio/vcpkg.json` + `studio/vcpkg-configuration.json` (`VCPKG_MANIFEST_DIR`) |
| **Triplets** | Root `.triplets` (`VCPKG_OVERLAY_TRIPLETS`) | Built-in triplets plus the Albor overlay |
| **Library Linkage** | Dynamic (Skia dylib, Windows DLLs) | Static (no DLL copying or RPATH manipulation) |
| **Presenter Architecture**| Monolithic (`SlintMainWindowPresenter`) | Modular (one presenter per dialog/window) |
| **Dialog Architecture** | Inline modal overlays (`ModalPanel.slint`) | Native window-modal Albor dialogs |
| **UI Test Suite** | `tests_ui/` (Slint automation) | `tests_studio/` (Albor automation) |
| **GitHub Actions** | Built by existing `default.yaml` & `coverage.yml` | New dedicated workflow added later |
| **Packaging Artifacts**| `artifacts/` via `RuntimeDeps` install component | Separate package output |

> [!IMPORTANT]
> **CI Cache Key Preservation**: `.github/workflows/default.yaml` installs from `slint/vcpkg.json` and keys the vcpkg cache on `hashFiles('slint/vcpkg.json', 'slint/vcpkg-configuration.json', '.triplets/**', '.ports/**')`. Do not move or rename those files while the legacy workflow remains the release path.

---

## 4. Phased Execution Checklist

### Phase 1: Decouple Core Logic from UI & Threading

The only non-UI class in `src/ui/` with a direct Slint dependency is `SimulationRunner`, which directly calls `slint::invoke_from_event_loop`.

- [x] **1.1. Abstract `SimulationRunner` Threading Dispatcher**
  - Define a generic event dispatcher callback in `SimulationRunner`:
    ```cpp
    using MainThreadDispatcher = std::function<void(std::function<void()>)>;
    ```
  - Inject this dispatcher via constructor or `set_dispatcher()`.
  - In Slint's `MainWindowView`, provide `[](auto fn) { slint::invoke_from_event_loop(std::move(fn)); }`.
  - In unit tests / new framework, provide standard synchronous execution or the new framework's main-thread dispatcher.
- [x] **1.2. Audit Core Dependencies**
  - Verify that `src/simulation`, `src/io`, `src/netlist`, `src/charts`, `src/expression`, `src/dsp`, `src/kicad`, and `src/config` have no remaining includes of Slint or `main_window.h`.
- [x] **1.3. Validate Unit Tests**
  - Run existing unit tests (`./.build-debug/tests/xyce-studio-tests`) to ensure zero regressions before moving files.

---

### Phase 2: Repository Reorganization (Creating Roots)

Move files into their respective homes. Git preserves history across renames (`git mv`).

- [x] **2.1. Create Shared Core Directory Structure (`core/`)**
  - Create `core/src/` and move the following directories from `src/`:
    - `src/charts/` → `core/src/charts/`
    - `src/config/` → `core/src/config/`
    - `src/core/` → `core/src/core/`
    - `src/dsp/` → `core/src/dsp/`
    - `src/expression/` → `core/src/expression/`
    - `src/io/` → `core/src/io/`
    - `src/kicad/` → `core/src/kicad/`
    - `src/netlist/` → `core/src/netlist/`
    - `src/simulation/` → `core/src/simulation/`
    - `src/ui/simulation_runner.*` → `core/src/runner/`
- [x] **2.2. Create Legacy Slint Directory Structure (`slint/`)**
  - Create `slint/src/` and move the remaining UI, app, and widget files from `src/`:
    - `src/app/` → `slint/src/app/`
    - `src/main.*` → `slint/src/main.*`
    - `src/ui/` → `slint/src/ui/` (all views, monolithic presenter, Slint chart renderer)
    - `src/plugin.json`, `src/plugin-icon-24x24.png` → `slint/src/`
  - Keep the overlay triplets at the root `.triplets/`, where the legacy presets and CI already reference them.
- [x] **2.3. Setup Per-Root Manifests**
  - `slint/vcpkg.json` carries the legacy manifest (`skia`, `spdlog`, `pocketfft`, `gtest`, `nng`, `protobuf`, `nativefiledialog-extended`); `slint/vcpkg-configuration.json` pins its baseline.
  - `studio/vcpkg.json` carries the Albor manifest (`spdlog`, `pocketfft`, `gtest`, `nng`, `protobuf`, `albor`); `studio/vcpkg-configuration.json` overlays the sibling Albor port and triplets for development.
  - There is no root `vcpkg.json`: each root owns its manifest so the legacy CI cache key stays stable.
- [x] **2.4. Create `studio/` Skeleton**
  - Create `studio/src/app/`, `studio/src/presenters/`, `studio/src/views/`, `studio/src/charts/`.

---

### Phase 3: CMake Configuration & Build Presets

- [x] **3.1. Create `core/CMakeLists.txt`**
  - Define `xyce-core` as a STATIC library.
  - Compile all sources in `core/src/**`.
  - Link core dependencies: `spdlog::spdlog`, `nng::nng`, `protobuf::libprotobuf`, `pocketfft`.
  - Expose `core/src` as PUBLIC include directory.
  - Zero Slint or Skia dependencies on `xyce-core`.
- [x] **3.2. Create `slint/CMakeLists.txt`**
  - Move Slint FetchContent, Slint build options, and widget code generation (`slint_target_sources`) here.
  - Define `xyce-studio` executable linking `xyce-core` + `Slint::Slint` + `unofficial::skia::skia` + `nfd::nfd`.
- [x] **3.3. Create `studio/CMakeLists.txt`**
  - Define `xyce-studio-next` executable linking `xyce-core` + Albor.
- [x] **3.4. Update Root `CMakeLists.txt`**
  - Add feature options:
    ```cmake
    option(BUILD_SLINT_FRONTEND "Build legacy Slint frontend" ON)
    option(BUILD_STUDIO_FRONTEND "Build new framework frontend" ON)
    option(BUILD_STUDIO_TESTS "Build the studio unit tests" OFF)
    ```
  - Un-force `VCPKG_OVERLAY_TRIPLETS` to allow preset override.
  - Include subdirectories:
    ```cmake
    add_subdirectory(core)
    if(BUILD_SLINT_FRONTEND)
        add_subdirectory(slint)
    endif()
    if(BUILD_STUDIO_FRONTEND)
        add_subdirectory(studio)
    endif()
    ```
- [x] **3.5. Configure `CMakePresets.json`**
  - **Preserve Existing CI Presets**: Keep `debug`, `release`, and `profile` presets targeting the Slint frontend with `BUILD_STUDIO_FRONTEND=OFF`, without altering their names or output paths (`.build-debug`, `.build-release`, `.build-profile`). They must continue to build targets `xyce-studio` and `xyce-studio-tests` without requiring Albor.
  - **Add New Framework Presets**:
    - `studio-debug` → `binaryDir`: `${sourceDir}/.build-studio-debug`, `VCPKG_MANIFEST_DIR`: `${sourceDir}/studio`, `BUILD_SLINT_FRONTEND=OFF`, `BUILD_STUDIO_TESTS=ON`, `ALBOR_LOCAL_SOURCE_DIR=${sourceDir}/../albor`.
    - `studio-release` → `binaryDir`: `${sourceDir}/.build-studio-release`, `VCPKG_MANIFEST_DIR`: `${sourceDir}/studio`, `BUILD_SLINT_FRONTEND=OFF`, `BUILD_STUDIO_TESTS=OFF`.
  - The legacy presets set `BUILD_STUDIO_FRONTEND=OFF`, so they build without a local or published Albor package.

---

### Phase 4: Slint Root Verification & CI Parity Gate

- [x] **4.1. Build Slint via Existing Presets**
  - Run: `cmake --preset debug` && `cmake --build --preset debug`.
  - Run: `cmake --preset release` && `cmake --build --preset release`.
- [x] **4.2. Verify Domain Unit Tests**
  - Run: `./.build-debug/tests/xyce-studio-tests`.
  - Confirm all parser, netlist, expression, and simulation unit tests pass. **2970 tests passed, 0 failures.**
- [x] **4.3. Verify Slint UI Integration Tests**
  - Run: `.venv/bin/python -m unittest discover -s tests_ui -p "*_test.py"`.
  - Confirm that moving files and splitting targets caused zero regressions in the existing Slint UI. **75 tests passed, OK.**
- [ ] **4.4. Verify CI Packaging Contract (GitHub Actions)**
  - Run the exact CI install command:
    ```bash
    cmake --install .build-release --component RuntimeDeps --prefix .build-release
    ```
  - Verify that `artifacts/` contains the identical executable, dylibs/DLLs, and directory layout expected by `.github/workflows/default.yaml`.
  - Status: still open; the packaging inputs are untouched by the split, but the install layout has not been re-run since Phase 2.

---

### Phase 5: Bootstrap New Framework Root (`studio/`)

- [x] **5.1. Minimal Application Shell**
  - Implement `studio/src/main.cpp` initializing the new UI framework runtime and event loop.
  - Create `App` lifecycle manager in `studio/src/app/`.
- [x] **5.2. Empty Main Window Shell**
  - Create `studio/src/views/main_window/main_window_view.*` opening the framework's native window.
  - Create a lightweight `studio/src/presenters/main_window_presenter.*` managing only main window actions.
- [x] **5.3. Build & Launch Verification**
  - Run: `cmake --preset studio-debug` && `cmake --build --preset studio-debug`.
  - Launch `./.build-studio-debug/studio/xyce-studio-next` and verify the window appears.

---

### Phase 6: Incremental Feature Migration & Modular Dialogs

Implement features in `studio/` piece by piece. For each feature, create dedicated presenter/view pairs and cover it with studio presenter unit tests (`studio-tests/`) plus Albor UI scenarios (`tests_studio/`).

Status (2026-11-10, through `030f4c5`): 6.1–6.3 and the plugin-config half of 6.6 are complete; 6.4, 6.5, the Add Plot half of 6.6, 6.7 and 6.8 are open.

- [x] **6.1. Window Chrome, Toolbar & Netlist Editor**
  - Implement netlist text editor view (binding to `EditorNetlistSource` from `xyce-core`).
  - Wire File Open / Save actions through Albor's native panels and track the netlist dirty state.
  - Drive the toolbar enablement and swap run/stop while a simulation is live.
- [x] **6.2. Simulation Execution & Output Panel**
  - Wire `SimulationRunner` from `xyce-core` to Albor's main-thread dispatcher.
  - Implement the docked live log panel with severity classification (`XyceLogParser`), auto-scroll, copy, close and reopen.
  - Implement simulation progress and status bar indicators.
  - The declared viewport render was unblocked by Albor #168 (for #167), so the panel is fully rendered.
- [x] **6.3. Native Dialog: Simulation Parameters**
  - Create `SimulationParametersPresenter` (toolkit-free validation of every analysis; seeded from and serialized back to the netlist directives).
  - Create `SimulationParametersDialog` as a window-modal native window with the OP, DC, TRAN, AC, NOISE, HB and LIN pages.
  - Cover the companion print, `.MEASURE`, `.FFT`, `.FOUR`, sensitivity, PCE and `.OPTIONS` sections; the accepted configuration is rewritten into the editor before the run.
  - Wire dialog invocation from `MainWindowPresenter`.
  - The pages share the full pane width through the Albor #206 cross-axis `ScrollView` stretch.
- [ ] **6.4. Native Dialog: FFT Setup**
  - Create `FftDialogPresenter` and `FftDialog` (native window).
  - Connect expression selection and `fft::FftParameters`.
  - Status: the transient page already edits raw `.FFT` directives; the chart-driven setup dialog waits on 6.7.
- [ ] **6.5. Native Dialog: Options Editor**
  - Create `OptionsPresenter` and `OptionsDialog` (native window).
  - Populate options from `OptionCatalog` in `xyce-core`.
  - Status: the analysis-scoped packages (TIMEINT, NONLIN, NONLIN TRAN, LINSOL AC, LOCA, FFT, OUTPUT) are edited in 6.3; the simulation-agnostic packages (DEVICE, LINSOL, PARSER, DIAGNOSTIC, DIST, MEASURE, RESTART) still need the dialog.
- [ ] **6.6. Native Dialogs: Plugin Config & Add Plot**
  - [x] `PluginConfigPresenter` and `PluginConfigDialog` are complete: path validation, persistence, focus and cancel/accept coverage.
  - [ ] `AddPlotPresenter` and `AddPlotDialog` (or dockable signal picker) wait on the chart pipeline in 6.7.
- [ ] **6.7. Chart Rendering Pipeline**
  - Connect `ChartEngine` and `ChartLayout` from `xyce-core` to a native Albor chart view.
  - Implement line and Smith charts, zoom, cursor hover, step selection and the default plot layout.
  - Implementation plan and file map: [`docs/development/chart-engine-plan.md`](docs/development/chart-engine-plan.md).
  - ImGui/ImPlot was evaluated and rejected; the textured-mesh primitive that path would need is tracked upstream as [AlborCore/albor#211](https://github.com/AlborCore/albor/issues/211), but native charts do not depend on it.
  - Status: `studio/src/charts/` is still empty and the shell has no chart view yet; the plan is ready.
- [ ] **6.8. KiCad Integration & Multi-Window Mode**
  - Wire `KiCadSession` IPC into the Albor application lifecycle.
  - Implement native secondary chart windows (`new_window`).
  - Status: the editor exposes the read-only state the KiCad slice will drive, but the IPC session is not connected.

---

### Phase 7: Parity Verification, Binary Cutover & Slint Retirement

- [ ] **7.1. Feature Parity Audit**
  - Run side-by-side manual and automated test sweeps between `xyce-studio` and `xyce-studio-next`.
  - Validate netlist editing, all simulation types, chart interactions, and dialog results.
- [ ] **7.2. Author New Framework CI Workflows**
  - Create separate GitHub Actions workflow(s) (e.g. `.github/workflows/studio.yaml`) dedicated to building and testing `xyce-studio-next`.
  - Keep `.github/workflows/default.yaml` and `coverage.yml` completely untouched and active throughout.
- [ ] **7.3. Swap Default Executables**
  - Make `studio/` produce the primary `xyce-studio` executable.
  - Update packaging scripts when ready to switch default releases.
- [ ] **7.4. Decommission Slint Root**
  - Remove `slint/` directory: `git rm -rf slint`.
  - Retire legacy CI workflows (`default.yaml`) once the new workflows take over primary release duties.
  - Remove Slint FetchContent and options from root `CMakeLists.txt`.
  - Update `README.md` and documentation.

---

## 5. Risk Mitigation & Verification Rules

1. **Slint Preserved at All Times**: No commit in Phases 1–6 may break `xyce-studio` or fail any test in `tests/` or `tests_ui/`.
2. **CI Contract Invariance**: Existing presets (`debug`, `release`, `profile`) and binary outputs must remain 100% binary-compatible with `.github/workflows/default.yaml`.
3. **Self-Contained Commits**: Every phase completion must result in a clean, buildable repository.
4. **Cache & Linkage Isolation**: Always use distinct binary directories (`.build-debug` / `.build-release` vs `.build-studio-debug` / `.build-studio-release`) so dynamic Slint dylibs and static framework libraries never contaminate each other.
5. **Albor Isolation**: The legacy presets must keep building without a local or published Albor package; only the studio presets opt into Albor.

---

*Prepared by Antigravity (AGY) – 2026‑10‑28 · Status updated 2026‑11‑10 through `030f4c5`*

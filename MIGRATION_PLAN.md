# Project Plan: Dual-Root UI Framework Migration

## 1. Overview & Objective

This document outlines the architectural blueprint and step-by-step execution plan to introduce a new custom UI framework into the `xyce-studio` repository while keeping the current Slint-based implementation fully operational side-by-side.

### Primary Drivers
1. **Dialog & Window Architecture**: Slint lacks clean support for secondary OS dialog windows and multi-window workflows, forcing all dialogs into inline modal overlays inside a single monolithic window (`MainWindow.slint`). The new framework provides native dialog windows and multi-window support.
2. **Presenter Modularization**: The current `SlintMainWindowPresenter` is a ~1,500-line monolith that manages both the main window and all 6 inline dialog panels. The new framework will replace this with dedicated, isolated presenters for each dialog and window.
3. **Linkage & Dependency Hygiene**: Slint has an LLVM exception-unwinding / RTTI bug at the Rust/C++ boundary, forcing all dependencies to be compiled as dynamic libraries via custom overlay triplets. The new framework eliminates this requirement and allows clean, static linking.
4. **CI & Packaging Continuity**: The existing GitHub Actions workflows (`.github/workflows/default.yaml`, `coverage.yml`) must continue building and packaging the Slint implementation with **identical binary outputs, preset names, and artifact layouts**. No changes to existing CI workflows are permitted during development of the new framework.
5. **Zero Downtime / Regression Protection**: Both frontends will coexist as two separate roots (`slint/` and `studio/`), sharing a common UI-agnostic domain core (`core/`). The current app (`xyce-studio`) will continue to build and pass all automated tests until the new app (`xyce-studio-next`) achieves complete feature parity.

---

## 2. Target Architecture

```
xyce-studio/
├── CMakeLists.txt                         # Master CMake orchestrating core & frontends
├── CMakePresets.json                      # Presets with isolated build dirs, manifests & triplets
│
├── core/                                  # SHARED DOMAIN ROOT (Pure C++23, zero UI dependencies)
│   ├── CMakeLists.txt                     # Produces static library: xyce-core
│   └── src/
│       ├── charts/                        # ChartEngine, ChartLayout, SmithChart (pure math/geometry)
│       ├── config/                        # PluginConfig persistence
│       ├── dsp/                           # FFT, interpolation (spline, linear, pchip)
│       ├── expression/                    # AST parser, evaluator, probe names, unit utilities
│       ├── io/                            # File parsers: raw, prn, csd, csv, tecplot, touchstone
│       ├── kicad/                         # KiCad IPC connection, session, protobuf commands
│       ├── netlist/                       # Netlist parser, lexer, file/editor sources
│       ├── runner/                        # SimulationRunner with generic event dispatcher
│       └── simulation/                    # Simulation parameters, options catalog, log parser
│
├── slint/                                 # ROOT 1: Current Slint implementation (frozen/maintained)
│   ├── CMakeLists.txt                     # Target: xyce-studio (dynamic Skia & dependencies)
│   ├── triplets/                          # Overlay triplets enforcing dynamic dylibs/DLLs
│   ├── vcpkg.json                         # Slint manifest (Skia, nfd, spdlog, pocketfft, etc.)
│   ├── src/
│   │   ├── app/                           # App singleton & platform entry points (main.*)
│   │   ├── presenters/                    # Monolithic SlintMainWindowPresenter & state
│   │   ├── views/                         # SlintMainWindowView & ModalPanel dialog views
│   │   ├── charts/                        # Slint ChartsRenderer & Skia text measurers
│   │   └── widgets/                       # main_window.slint & components/
│   └── tests_ui/                          # Slint Python integration test suite
│
├── studio/                                # ROOT 2: New UI Framework implementation
│   ├── CMakeLists.txt                     # Target: xyce-studio-next (clean static linkage)
│   ├── triplets/                          # Optional custom triplets (or default static triplets)
│   ├── vcpkg.json                         # New framework manifest (tailored dependencies)
│   └── src/
│       ├── app/                           # New framework App lifecycle & main entry points
│       ├── presenters/                    # Modular presenters (MainWindow, SimulationConfig, FFT...)
│       ├── views/                         # Modular views & native dialog windows
│       └── charts/                        # Canvas/rendering pipeline for charts
│
└── tests/                                 # Fast domain unit tests (links only xyce-core)
    └── CMakeLists.txt
```

---

## 3. Dependency, Triplet & CI Strategy

| Concern | Legacy Root (`slint/` / CI Standard) | New Framework Root (`studio/`) |
| :--- | :--- | :--- |
| **Executable Target** | `xyce-studio` (identical output name) | `xyce-studio-next` |
| **CMake Presets** | `debug`, `release`, `profile` (untouched) | `studio-debug`, `studio-release` |
| **Build Directory** | `.build-debug`, `.build-release`, `.build-profile` | `.build-studio-debug`, `.build-studio-release` |
| **Manifest Path** | Root `vcpkg.json` & `slint/vcpkg.json` | `studio/vcpkg.json` (`VCPKG_MANIFEST_DIR`) |
| **Triplets** | `slint/triplets` (`VCPKG_OVERLAY_TRIPLETS`) | Built-in static triplets (or `studio/triplets`) |
| **Library Linkage** | Dynamic (Skia dylib, Windows DLLs) | Static (no DLL copying or RPATH manipulation) |
| **Presenter Architecture**| Monolithic (`SlintMainWindowPresenter`) | Modular (One presenter per dialog/window) |
| **Dialog Architecture** | Inline modal overlays (`ModalPanel.slint`) | Native OS dialog windows |
| **GitHub Actions** | Built by existing `default.yaml` & `coverage.yml` | New dedicated workflow added later |
| **Packaging Artifacts**| `artifacts/` via `RuntimeDeps` install component | Separate package output |

> [!IMPORTANT]
> **CI Cache Key Preservation**: The existing `.github/workflows/default.yaml` computes cache keys from:
> `hashFiles('vcpkg-configuration.json')`, `hashFiles('vcpkg.json')`, `hashFiles('CMakeLists.txt')`, and `hashFiles('CMakePresets.json')`. Keeping `vcpkg.json` at the repo root identical to `slint/vcpkg.json` ensures GitHub Actions cache keys remain fully valid across runs.

---

## 4. Phased Execution Checklist

### Phase 1: Decouple Core Logic from UI & Threading

The only non-UI class in `src/ui/` with a direct Slint dependency is `SimulationRunner`, which directly calls `slint::invoke_from_event_loop`.

- [ ] **1.1. Abstract `SimulationRunner` Threading Dispatcher**
  - Define a generic event dispatcher callback in `SimulationRunner`:
    ```cpp
    using MainThreadDispatcher = std::function<void(std::function<void()>)>;
    ```
  - Inject this dispatcher via constructor or `set_dispatcher()`.
  - In Slint's `MainWindowView`, provide `[](auto fn) { slint::invoke_from_event_loop(std::move(fn)); }`.
  - In unit tests / new framework, provide standard synchronous execution or the new framework's main-thread dispatcher.
- [ ] **1.2. Audit Core Dependencies**
  - Verify that `src/simulation`, `src/io`, `src/netlist`, `src/charts`, `src/expression`, `src/dsp`, `src/kicad`, and `src/config` have no remaining includes of Slint or `main_window.h`.
- [ ] **1.3. Validate Unit Tests**
  - Run existing unit tests (`./.build-debug/tests/xyce-studio-tests`) to ensure zero regressions before moving files.

---

### Phase 2: Repository Reorganization (Creating Roots)

Move files into their respective homes. Git preserves history across renames (`git mv`).

- [ ] **2.1. Create Shared Core Directory Structure (`core/`)**
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
- [ ] **2.2. Create Legacy Slint Directory Structure (`slint/`)**
  - Create `slint/src/` and move the remaining UI, app, and widget files from `src/`:
    - `src/app/` → `slint/src/app/`
    - `src/main.*` → `slint/src/main.*`
    - `src/ui/` → `slint/src/ui/` (all views, monolithic presenter, Slint chart renderer)
    - `src/plugin.json`, `src/plugin-icon-24x24.png` → `slint/src/`
  - Move `.triplets/` to `slint/triplets/`.
- [ ] **2.3. Setup Per-Root Manifests**
  - Create `slint/vcpkg.json` containing:
    - `skia` (with current platform features)
    - `spdlog`, `pocketfft`, `gtest`, `nng`, `protobuf`, `nativefiledialog-extended`
  - Create `studio/vcpkg.json` containing:
    - `spdlog`, `pocketfft`, `gtest`, `nng`, `protobuf`
    - Plus dependencies specific to the new framework.
- [ ] **2.4. Create `studio/` Skeleton**
  - Create `studio/src/app/`, `studio/src/presenters/`, `studio/src/views/`, `studio/src/charts/`.

---

### Phase 3: CMake Configuration & Build Presets

- [ ] **3.1. Create `core/CMakeLists.txt`**
  - Define `xyce-core` as a STATIC library.
  - Compile all sources in `core/src/**`.
  - Link core dependencies: `spdlog::spdlog`, `nng::nng`, `protobuf::libprotobuf`, `pocketfft`.
  - Expose `core/src` as PUBLIC include directory.
  - Zero Slint or Skia dependencies on `xyce-core`.
- [ ] **3.2. Create `slint/CMakeLists.txt`**
  - Move Slint FetchContent, Slint build options, and widget code generation (`slint_target_sources`) here.
  - Define `xyce-studio` executable linking `xyce-core` + `Slint::Slint` + `unofficial::skia::skia` + `nfd::nfd`.
- [ ] **3.3. Create `studio/CMakeLists.txt`**
  - Define `xyce-studio-next` executable linking `xyce-core` + the new UI framework.
- [ ] **3.4. Update Root `CMakeLists.txt`**
  - Add feature options:
    ```cmake
    option(BUILD_SLINT_FRONTEND "Build legacy Slint frontend" ON)
    option(BUILD_STUDIO_FRONTEND "Build new framework frontend" ON)
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
- [ ] **3.5. Configure `CMakePresets.json`**
  - **Preserve Existing CI Presets**: Keep `debug`, `release`, and `profile` presets targeting the Slint frontend without altering their names, configurations, or output paths (`.build-debug`, `.build-release`, `.build-profile`). They must continue to build targets `xyce-studio` and `xyce-studio-tests`.
  - **Add New Framework Presets**:
    - `studio-debug` → `binaryDir`: `${sourceDir}/.build-studio-debug`, `VCPKG_MANIFEST_DIR`: `${sourceDir}/studio`.
    - `studio-release` → `binaryDir`: `${sourceDir}/.build-studio-release`, `VCPKG_MANIFEST_DIR`: `${sourceDir}/studio`.

---

### Phase 4: Slint Root Verification & CI Parity Gate

- [ ] **4.1. Build Slint via Existing Presets**
  - Run: `cmake --preset debug` && `cmake --build --preset debug`.
  - Run: `cmake --preset release` && `cmake --build --preset release`.
- [ ] **4.2. Verify Domain Unit Tests**
  - Run: `./.build-debug/tests/xyce-studio-tests`.
  - Confirm all parser, netlist, expression, and simulation unit tests pass.
- [ ] **4.3. Verify Slint UI Integration Tests**
  - Run: `.venv/bin/python -m unittest discover -s tests_ui -p "*_test.py"`.
  - Confirm that moving files and splitting targets caused zero regressions in the existing Slint UI.
- [ ] **4.4. Verify CI Packaging Contract (GitHub Actions)**
  - Run the exact CI install command:
    ```bash
    cmake --install .build-release --component RuntimeDeps --prefix .build-release
    ```
  - Verify that `artifacts/` contains the identical executable, dylibs/DLLs, and directory layout expected by `.github/workflows/default.yaml`.

---

### Phase 5: Bootstrap New Framework Root (`studio/`)

- [ ] **5.1. Minimal Application Shell**
  - Implement `studio/src/main.cpp` initializing the new UI framework runtime and event loop.
  - Create `App` lifecycle manager in `studio/src/app/`.
- [ ] **5.2. Empty Main Window Shell**
  - Create `studio/src/views/main_window_view.*` opening the framework's native window.
  - Create a lightweight `studio/src/presenters/main_window_presenter.*` managing only main window actions.
- [ ] **5.3. Build & Launch Verification**
  - Run: `cmake --preset studio-debug` && `cmake --build --preset studio-debug`.
  - Launch `./.build-studio-debug/studio/xyce-studio-next` and verify the window appears.

---

### Phase 6: Incremental Feature Migration & Modular Dialogs

Implement features in `studio/` piece by piece. For each feature, create dedicated presenter/view pairs.

- [ ] **6.1. Window Chrome, Toolbar & Netlist Editor**
  - Implement netlist text editor view (binding to `EditorNetlistSource` from `xyce-core`).
  - Wire File Open / Save actions and netlist dirty tracking.
- [ ] **6.2. Simulation Execution & Output Panel**
  - Wire `SimulationRunner` from `xyce-core` to the new framework's main-thread dispatcher.
  - Implement live log streaming panel with severity classification (`XyceLogParser`).
  - Implement simulation progress and status bar indicators.
- [ ] **6.3. Native Dialog: Simulation Parameters**
  - Create `SimulationParametersPresenter` (manages tab state, `.TRAN`, `.AC`, `.DC` validation).
  - Create `SimulationParametersDialog` (native secondary window).
  - Wire dialog invocation from `MainWindowPresenter`.
- [ ] **6.4. Native Dialog: FFT Setup**
  - Create `FftDialogPresenter` and `FftDialog` (native window).
  - Connect expression selection and `fft::FftParameters`.
- [ ] **6.5. Native Dialog: Options Editor**
  - Create `OptionsPresenter` and `OptionsDialog` (native window).
  - Populate options from `OptionCatalog` in `xyce-core`.
- [ ] **6.6. Native Dialogs: Plugin Config & Add Plot**
  - Create `PluginConfigPresenter` and `PluginConfigDialog`.
  - Create `AddPlotPresenter` and `AddPlotDialog` (or dockable signal picker).
- [ ] **6.7. Chart Rendering Pipeline**
  - Connect `ChartEngine` and `ChartLayout` from `xyce-core` to the new framework's canvas/renderer.
  - Implement zoom, pan, cursor hover, and step selection.
- [ ] **6.8. KiCad Integration & Multi-Window Mode**
  - Wire `KiCadSession` IPC into the new framework application lifecycle.
  - Implement native secondary chart windows (`new_window`).

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

---

*Prepared by Antigravity (AGY) – 2026‑10‑28*
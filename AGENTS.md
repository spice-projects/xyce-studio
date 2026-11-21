# AGENTS.md

## Language

- All code style rules live in `STYLE-GUIDE.md`
- Preserve existing architecture unless explicitly requested

## Build & Unit Tests

- Build system: `cmake`
  - Configure: `cmake --preset debug`
  - Build: `cmake --build --preset debug`
- Unit Tests, avoid running unit tests using `ctest`. Execute unit tests by executing the process: `./.build-debug/tests/xyce-studio-tests`
- The CMake tree registers the remaining checks with `ctest`, which is the entry point for the checks that are not plain unit tests: `ctest -R link-boundary` (the `ui-integration` suite is retired)
- Unit tests must be self-contained: no helper functions, no test utilities, no external fixtures
- Dependencies: `vcpkg`
- Build cache:
  - **vcpkg binary cache** (local): `.vcpkg-cache/` is auto-populated via `VCPKG_BINARY_SOURCES` in CMake presets. Caches Albor/protobuf/nng compilation between clean builds.
  - **ccache** (local): enabled via `CMAKE_{C,CXX}_COMPILER_LAUNCHER` in CMake presets. Inspect with `ccache --show-stats`; clear with `ccache --clear`.
  - **CI**: both caches are persisted via `actions/cache@v5` keyed on runner OS + content hash. First CI run builds from source (~hour); subsequent runs restore in seconds.
- Studio debug builds embed the Albor automation host used by the `tests_studio` scenarios; the binary is `.build-debug/xyce-studio` and exposes its semantic tree to `albor_automation` (release builds do not embed the host).

## Workflow

Before editing:
1. Search for existing implementations
2. Understand ownership and lifetime
3. Follow conventions in `STYLE-GUIDE.md` and the surrounding code
4. Make minimal changes

After editing:
1. Build the project
2. Fix compiler warnings/errors
3. Run the linter check on all changed C++ files: `bash build/check-format.sh`
4. Make every file you touch fully compliant with `STYLE-GUIDE.md`: the check covers whole files, not only the lines you changed, so fix any pre-existing violation in a file the change touches
5. Fix every consecutive comment line in the files you changed: one comment line above a statement, and when a test marker is followed by an explanation only the `// arrange` / `// act` / `// assert` marker stays
6. Keep any changed `.ui` files formatted consistently with the other Albor views under `studio/src/views/`
7. Summarize changed files
8. Do not execute unit tests unless explicitly asked

## UI Integration Tests

- All UI work requires UI integration tests in `tests_studio/`; a UI change without a scenario exercising it is incomplete
- The integration test documentation lives at `tests_studio/AUTOMATION.md` — read it before writing scenarios; every recipe there is reverse engineered from a passing test
- The scenarios run the real debug build through the `albor_automation` framework (session/launch, locators, `expect` polling); run them with the repository virtual environment: `.venv/bin/python -m unittest discover -s tests_studio -p "*_test.py"`
- While developing or fixing one scenario, run only that test (it launches the real app, so keep it to the minimum): `.venv/bin/python -m unittest tests_studio.<module>.<Class>.<test_name>` — run the full suite once when the work is done to make sure nothing else got affected
- Native context menus are not bound by the automation host; views expose semantic actions instead (see the context-menu recipe in `tests_studio/AUTOMATION.md`) — wire any new native-rendered action to such an action when it needs integration coverage
- Test methods must be self-contained: no helper functions, no shared utilities between tests; test doubles are the only shared objects (same rule as the unit tests above)

## UI (Albor studio)

The UI is built with Albor and lives under `studio/src`:

- Views (native windows, dialogs and the chart canvas): `studio/src/views/**` as Albor `.ui` components plus host C++
- Presenters (one per window or dialog): `studio/src/presenters/`
- Application lifecycle, shells and theme: `studio/src/app/`
- Chart pipeline helpers: `studio/src/charts/`

### Build integration (`CMakeLists.txt`)

- Views are Albor `.ui` components compiled with `albor_build_ui_component`; stable semantic ids come from the `id:` attribute and are used by `tests_studio`.
- The studio target and executable are both `xyce-studio`; Albor is consumed as the `albor` vcpkg package, with a sibling checkout available through `ALBOR_LOCAL_SOURCE_DIR` for local development.
- No Slint sources or Slint-specific CMake options remain.

### Presenter wiring

- Presenters are toolkit-free and drive views through a narrow interface; a presenter never constructs UI. Dialogs are native window-modal windows, not inline overlays, and each has its own presenter.
- Keep views alive for the lifetime of their shell; the application owns one shell per window.

### Dialogs

- Dialogs are native Albor windows (simulation parameters, FFT, options, plugin config, Add Plot), each with its own presenter/view pair.
- Accept/cancel results flow back through the presenter to the editor; cancelling leaves the document untouched.

### Charts renderer

- The studio chart view (`studio/src/views/charts/chart_view.*`) is the authoritative renderer: `core::ChartEngine` and `core::ChartLayout` produce `ChartFrame`s and the view paints them through Albor's `RenderContext` using retained picture layers. There is no Slint frame model and no render loop.
- Scene layers (base, series, legend, dataset strip) are cached by keys and rebuilt only when their inputs change; layer draw commands are local, with the slot offset applied by the replay transform.
- Zoom, hover and context-menu commands run through the presenter/controller and are exposed as semantic actions for `tests_studio`.

### Lifecycle

- `StudioApp` owns the native event loop and the set of window shells; `studio/src/main.cpp` handles entry and argument overrides.

## Architecture

- Albor owns the application lifecycle (`StudioApp` + the native event loop)
- Rendering components should remain independent where possible
- Avoid unnecessary dependency coupling

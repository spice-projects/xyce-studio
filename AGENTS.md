# AGENTS.md

## Language

- All code style rules live in `STYLE-GUIDE.md`
- Preserve existing architecture unless explicitly requested

## Build & Unit Tests

- Build system: `cmake`
  - Configure: `cmake --preset debug`
  - Build: `cmake --build --preset debug`
- Unit Tests, avoid running unit tests using `ctest`. Execute unit tests by executing the process: `./.build-debug/tests/xyce-studio-tests`
- Unit tests must be self-contained: no helper functions, no test utilities, no external fixtures
- Dependencies: `vcpkg`
- Build cache:
  - **vcpkg binary cache** (local): `.vcpkg-cache/` is auto-populated via `VCPKG_BINARY_SOURCES` in CMake presets. Caches Skia/protobuf/nng compilation between clean builds.
  - **ccache** (local): enabled via `CMAKE_{C,CXX}_COMPILER_LAUNCHER` in CMake presets. Inspect with `ccache --show-stats`; clear with `ccache --clear`.
  - **CI**: both caches are persisted via `actions/cache@v5` keyed on runner OS + content hash. First CI run builds from source (~hour); subsequent runs restore in seconds.
- Debug builds enable the embedded Slint MCP server. Launch the app with `SLINT_MCP_PORT=8080 ./build/xyce-studio` to expose an HTTP/MCP endpoint for AI-assisted UI inspection (browse the `.slint` component tree, read/write properties, invoke callbacks).

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
4. Format any changed `.slint` files in-place: `slint-lsp format -i <path/to/file.slint>`
5. Summarize changed files
6. Do not execute unit tests unless explicitly asked

## UI Integration Tests

- All UI work requires UI integration tests in `tests_ui/`; a UI change without a scenario exercising it is incomplete
- The integration test documentation lives at `tests_ui/AUTOMATION.md` — read it before writing scenarios; every recipe there is reverse engineered from a passing test
- The scenarios run the real debug build through the `slint_automation` framework (session/launch, locators, `expect` polling); run them with the repository virtual environment: `.venv/bin/python -m unittest discover -s tests_ui -p "*_test.py"`
- While developing or fixing one scenario, run only that test (it launches the real app, so keep it to the minimum): `.venv/bin/python -m unittest tests_ui.<module>.<Class>.<test_name>` — run the full suite once when the work is done to make sure nothing else got affected
- Platform native widgets (e.g. the macOS context menu) are not reachable by the automation; debug builds expose hidden trigger elements instead (see the context-menu recipe in `tests_ui/AUTOMATION.md`) — wire any new native-rendered action to such a trigger when it needs integration coverage
- Test methods must be self-contained: no helper functions, no shared utilities between tests; test doubles are the only shared objects (same rule as the unit tests above)

## UI (Slint)

The UI is built with Slint and lives under `src/ui`:

- Root widgets (windows/dialogs): `src/ui/widgets/*.slint`
- Shared components (imported only by roots): `src/ui/components/*.slint`
- Host C++ (`slint.h`, wiring, platform backends): flat under `src/ui/` (views/presenters, dialog wrappers, `file_dialog.*`, `clipboard.*`, `charts_renderer.*`)

### Build integration (`CMakeLists.txt`)

- Only `src/ui/widgets/*.slint` are compiled via `slint_target_sources(... NAMESPACE <file-stem>)`. Components under `src/ui/components/` are imported by roots only; never register them standalone.
- Each root widget becomes a C++ namespace named after its file stem (e.g. `main_window::MainWindow`); its generated header is included with angle brackets: `#include <main_window.h>`.
- Slint names are kebab-case; generated C++ accessors/callbacks are camelCase: property `charts-visible` → `get_charts_visible()`/`set_charts_visible()`; callback `show-charts` → `on_show_charts()`.
- `SLINT_STYLE=cupertino`, `SLINT_FEATURE_RENDERER_SKIA=ON`, Slint pinned to the immutable `v1.18.1` tag via FetchContent.
- `@image-url()` assets are embedded with `SLINT_EMBED_RESOURCES embed-files`; image paths resolve relative to the `.slint` file (`../kicad-icons/..._48.png`).
- Platform host code uses the suffixes `.osx.mm` / `.win32.c++` / `.linux.c++`; UI colors come from `Palette` in `std-widgets.slint`.

### MVP wiring

- Keep the view adapter pattern: a `*View` implements `MainWindowViewDef` and forwards user interactions through `MainWindowViewDefEvents` (the presenter implements it and never touches the UI). The parent creates both and wires them with `view->set_event_handler(*presenter)`.
- Expose UI actions as `export global ... Actions { callback ...; }` on the root widget; bind them from `set_event_handler` via `m_window->global<...Actions>().on_<callback>(...)`.
- Views/presenters hold `slint::ComponentHandle` members and are non-copyable/non-movable; keep them alive while the event loop runs.
- Prefer `slint::SharedString` for string interop and `slint::VectorModel<T>` for models (`set_row_data` to update a row).

### Dialogs

- All dialogs are rendered as inline modal overlays within `MainWindow` using the `ModalPanel` base component (no separate OS windows). Modality is enforced by the view's `begin_modal_dialog()`/`end_modal_dialog()` and the panel's dimmed backdrop; toolbar callbacks are wrapped in `guard_modal(...)` so they are rejected while a dialog is open.
- Each dialog wrapper (e.g. `PluginConfigDialogView`) lives in its own translation unit — the generated `main_window.h` defines a `SharedGlobals` type that conflicts with other generated headers — and uses a pimpl `Impl`. It exposes `show(...)` and an `on_closed` callback; on accept/cancel, hide the panel, call `on_closed()`, then deliver results through `MainWindowViewDefEvents`.

### Charts renderer

- `ChartsRenderer` is platform-neutral; it composes `ChartFrame` snapshots from the `ChartEngine` state through `ChartLayout` and publishes them into the Slint frame model via an injected publish function. Slint renders the charts natively in its own renderer; no per-platform backends exist.
- There is no render loop. Frames are published synchronously from `publish_frames()` at the state-change points: data update, tab switch, step/scale/series changes, resize-finished, and after every interactive zoom/hover-driven zoom window change.
- Drag-resize must not republish frames: the single resize-finished event triggers the republish; `ChartView` stretches its content to its own element size in between.
- Zoom and hover hit tests use the panel-relative plot rects of the last published frames (`ChartsRenderer::plot_rect(index)`), not the engine state.
- Slint passes chart interaction positions as `float [0..1]`; translate to a chart index with `ChartsRenderer::position_to_index()` before acting.
- Call `reset_viewport()` when the charts panel is hidden so no frames are published while the panel is hidden.

### Lifecycle

- `App::run()` owns the event loop: build the view + presenter, wire them, `show()`, then `slint::run_event_loop()`.

## Architecture

- Slint owns the application lifecycle (`App` singleton + `slint::run_event_loop`)
- Rendering components should remain independent where possible
- Avoid unnecessary dependency coupling

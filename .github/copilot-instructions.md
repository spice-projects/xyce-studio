# Copilot Review Instructions for Xyce Studio

Xyce Studio is a C++20 application (CMake + vcpkg, arm64/x86-64) embedding a Slint UI
that drives Xyce circuit simulations from KiCad. This file customizes pull request
reviews. `AGENTS.md` (root) is the full agent guide and `STYLE-GUIDE.md` holds the
code style rules; these instructions distill what matters for reviewing.

## Review focus

- **Memory safety over style.** Ownership, lifetime and bounds are the highest
  review priority. The app memory-maps RAW output files (`MappedFile`) and shares
  non-owning `View<T>` spans over them; out-of-bounds sweeps (e.g.
  `initialize_expression_data` tile copies) are real SIGSEGV hazards.
- **Preserve the architecture.** Do not suggest restructuring:
  - MVP wiring: views implement `MainWindowViewDef`, forward through
    `MainWindowViewDefEvents`; presenters never touch the UI.
  - Dialogs are inline modal overlays (`ModalPanel`), one translation unit each,
    pimpl `Impl`, results delivered through `MainWindowViewDefEvents`.
  - `ChartsRenderer` is platform-neutral (ImGui/ImPlot + offscreen Skia raster →
    `slint::Image`); always use `ChartsContextScope` around ImGui/ImPlot calls.
- **Slint conventions.** Only `src/ui/widgets/*.slint` are registered as root
  widgets; `src/ui/components/*.slint` are imported by roots only. Slint names
  are kebab-case; generated C++ accessors are camelCase
  (`charts-visible` → `get_charts_visible()`).

## Verification rules

- **Verify Xyce behavior claims against the reference guide.** Any review claim
  about Xyce directive syntax, semantics, defaults or output files must be checked
  in `xyce-docs/Xyce_RG.txt` (reference guide) and `xyce-docs/Xyce_UG.pdf`
  (user guide) before being raised. Do not speculate about Xyce semantics.
- Expect PRs to have been verified with:
  - `cmake --build --preset debug` (no new warnings/errors)
  - `./.build-debug/tests/xyce-studio-tests` (the full suite, not ctest)
  - `bash build/check-format.sh` (linter, must be clean)
- Requests for tests must respect: unit tests are self-contained — no helper
  functions, no shared test utilities, no external fixtures.
- Mapping-lifetime: Xyce output files are memory-mapped; a re-run must never
  rewrite a file the application still maps (Windows blocks the rewrite, macOS
  invalidates the mapping).

## Style context (avoid false positives)

- The codebase comments nearly every block (`// arrange`, `// act`, ...); do not
  flag comments as noise or ask for their removal.
- C++20 features (concepts, ranges, `requires`) and Slint-generated camelCase
  accessors are intentional.
- Platform-specific host files use suffixes `.osx.mm` / `.win32.c++` /
  `.linux.c++`; keep changes mirrored across them when behavior is shared.
- Raw pointers into `View<T>` data are owned elsewhere (mapped file or owned
  vector); do not flag them as leaks without checking the owner chain
  (`View` → `Expression` → `XyceOutputFile` → `unique_ptr<MappedFile>`).

# Copilot Review Instructions for Xyce Studio

Xyce Studio is a C++20 application (CMake + vcpkg, arm64/x86-64) with an Albor-based
studio UI that drives Xyce circuit simulations from KiCad. This file customizes pull request
reviews. `AGENTS.md` (root) is the full agent guide and `STYLE-GUIDE.md` holds the
code style rules; these instructions distill what matters for reviewing.

## Review focus

- **Memory safety over style.** Ownership, lifetime and bounds are the highest
  review priority. The app memory-maps RAW output files (`MappedFile`) and shares
  non-owning `View<T>` spans over them; out-of-bounds sweeps (e.g.
  `initialize_expression_data` tile copies) are real SIGSEGV hazards.
- **Preserve the architecture.** Do not suggest restructuring:
  - Presenter wiring: views drive a narrow interface and forward through
    view-def events; presenters never construct UI.
  - Dialogs are native window-modal Albor windows, one presenter/view pair each,
    with results delivered back through the presenter.
  - The studio chart view (`studio/src/views/charts/chart_view.*`) is the sole
    renderer: `core::ChartEngine`/`ChartLayout` feed retained layers painted
    through Albor's `RenderContext`.
- **Albor conventions.** Views are `.ui` components under `studio/src/views/**`;
  semantic ids come from the `id:` attribute and are consumed by `tests_studio`.

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
- C++20 features (concepts, ranges, `requires`) and Albor `.ui` semantic ids
  are intentional.
- Studio views are Albor `.ui` components (`studio/src/views/**`) with stable
  ids used by `tests_studio`; keep view and presenter changes paired.
- Raw pointers into `View<T>` data are owned elsewhere (mapped file or owned
  vector); do not flag them as leaks without checking the owner chain
  (`View` → `Expression` → `XyceOutputFile` → `unique_ptr<MappedFile>`).

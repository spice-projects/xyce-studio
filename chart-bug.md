# Chart panel: dataset tab strip disappears after a left click

Status: **fixed and validated** — the root cause was in `albor`; the fix in `albor` commit `0648e5c` is confirmed against the original reproduction. See §11.

This document records the defect, the reproduction, every test and measurement performed, the root-cause diagnosis, and the validation of the fix. It separates observed facts from the proven framework defect in `albor`.

- Branch: `refactor/dual-root-structure`
- HEAD: `6e7557f` (nothing from this work is committed)
- Date of measurements: Nov 13–14 2026
- Build: `cmake --preset debug` / `cmake --build --preset debug` (the preset was later renamed from `studio-debug` to `debug`)

---

## 1. The defect

### 1.1 Symptom

After a left click inside the chart area, the dataset tab strip stops being visible. The top 32 px of the chart panel shows bare panel background where the tab strip belongs. Everything below it — plot plate, grid, axes, labels, series, legend — keeps rendering correctly.

The result is stable. It does not flicker back on the next frame, and it survives at least one second after the click. It does not recover until something changes the strip state or the panel is repainted from scratch.

The chart content is **not** displaced. The 32 px band that looks like a shift is exactly the reserved strip height (`m_content_y == 32.0`). When the strip layer is missing, that band falls through to the panel background fill. This is an important distinction: earlier notes in this investigation described the content as "offset by 32 px", which suggested a layout shift. It is not a layout shift; it is a missing layer.

### 1.2 Reproduction

Two datasets, no simulation required to see the strip itself:

```
./.build-debug/xyce-studio \
  --dataset tests_studio/fixtures/tran-simple-01.csv \
  --dataset tests_studio/fixtures/tran-simple-01.csv
```

Window geometry during capture: position `120,168`, size `1200x832`. Left click at screen coordinate `(720,452)` delivered as a real CoreGraphics event (see §4.3). The window must be activated first; without activation the click is not delivered to the app (proven in §4.3).

The same defect was also reproduced with a simulated circuit after a Xyce run:

```
./.build-debug/xyce-studio \
  --netlist netlists/ac-simple-01.cir \
  --xyce ~/.local/bin/Xyce
```

### 1.3 What a correct frame contains

The strip is a retained layer at the top of the chart panel, painted by `paint_strip()`:

- strip plate fill across the panel width
- 1 px hairline along the bottom edge of the strip
- one tab plate per dataset; the active tab takes the panel face colour
- a 2 px active underline
- a 12×12 close cross for closable tabs
- one clipped title per tab
- `tab_bounds` / `close_bounds` recorded as a side effect for hit testing

`tests_studio/charts_test.py` proves the strip is live: `select-tab-0` and `close-tab-0` semantic actions change the active dataset and close the dataset. So the strip exists, is laid out, and is hit-testable both before and after the click.

---

## 2. Current paint architecture (the code under test)

All of the following lives in `studio/src/views/charts/chart_view.cpp` / `.h`.

`ChartView::paint_content()` is the single entry point. Per paint it does, in order:

1. Fill `bounds()` with `m_palette.background` — a direct, non-retained rect.
2. If `m_tabs` is non-empty and `m_content_y > 0`, compute `strip_key()`, record the strip picture if the key changed or the picture is null, then replay it with `context.draw_picture(m_strip_layer.picture, Transform::translation(bounds().x, bounds().y))`.
3. Return early if `m_frames` is empty.
4. Compute per-chart layout: shared plot x/width, axis columns, slot height.
5. For each chart: record/replay `m_base_layers[i]` (plate, grid, border, ticks, labels — or the Smith gamma plane), then `m_series_layers[i]` (clipped polyline), then `m_legend_layers[i]`.
6. Paint the transient zoom selection overlay directly (translucent wash + stroked border) if `m_zoom_selection` is set.

Cache keys:

- `base_key()` — view width, slot height, plot rect, column width, axis counts, frame plot geometry, x ticks/grid, every y axis tick/grid, Smith grid and labels, legend presence, palette.
- `series_key()` — localized plot rect, frame plot geometry, every series name/colour/axis/step/boundary/points, palette.
- `legend_key()` — every legend name/colour, block extent, palette.
- `strip_key()` — width, height, every tab title/active/closable, palette.

Record counters exist for the tests: `base_record_count()`, `series_record_count()`, `legend_record_count()`, `strip_record_count()`.

`ChartView::arrange()` detects a width/height change and arms a 50 ms resize settle, which reports the settled extent once.

`ChartView::dispatch_event()` handles, in order: Escape (only while dragging), `PointerCancel`, strip press (close then select), chart `PointerDown` → `zoom_drag_started`, `PointerMove` → drag move or hover, `PointerUp` → drag end or hover, `PointerLeave` (kept alive during a captured drag) / `PointerEnter` → hover.

---

## 3. Instrumentation added to observe the defect

Temporary diagnostics, **still present in the working tree**, must be removed once the defect is fixed:

- `#include <cstdio>` in `chart_view.cpp`
- `SET TABS %zu` printed at the top of `set_tabs()`
- `SET CONTENT %.1f` printed at the top of `set_content_y()`
- `PAINT bounds=%.1f,%.1f %.1fx%.1f tabs=%zu content=%.1f strip=%p frames=%zu` printed at the top of `paint_content()`, before the strip block

The `PAINT` line is emitted **before** the strip picture is recorded, which is why the first post-click paint prints `strip=0x0` even though that same call records the picture. This ordering matters when reading the log and was initially misread.

---

## 4. Tests performed

### 4.1 Unit tests — `StudioChartViewChecks`

Runner: `./.build-debug/studio-tests/xyce-studio-tests --gtest_filter='StudioChartViewChecks.*'`

Source: `studio-tests/chart_view.test.cpp`

These tests validate the retained-layer model itself. They all pass.

| # | Test | Asserts | Result |
|---|------|---------|--------|
| 1 | `the_background_covers_the_arranged_panel` | background rect covers `bounds()` | pass |
| 2 | `the_semantic_node_names_the_chart_surface` | canvas role, id, name | pass |
| 3 | `one_white_plot_rectangle_is_painted_per_chart` | one plate per frame | pass |
| 4 | `charts_share_the_content_height_equally` | slot origins one slot apart | pass (after fix, see below) |
| 5 | `tick_marks_are_painted_inside_the_centred_plot` | tick marks inside the plot | pass |
| 6 | `grid_lines_are_mapped_into_the_shared_plot` | grid mapping | pass |
| 7 | `a_second_paint_reuses_the_base_layer` | `base_record_count()` stays 1 | pass |
| 8 | `a_series_change_reuses_the_base_layer` | adding a series keeps base count 1, series count 1 | pass |
| 9 | `an_axis_change_rebuilds_the_base_layer` | base count becomes 2 | pass |
| 10 | `a_series_change_rebuilds_only_the_series_layer` | base 1, series 2 | pass |
| 11 | `an_axis_change_rebuilds_the_base_but_reuses_the_series` | base 2, series 1 | pass |
| 12 | `a_series_is_painted_above_the_base` | exactly one polyline | pass |
| 13 | `a_legend_is_painted_above_the_series` | swatch + text after base, translated to `legend_x/legend_y` | pass |
| 14 | `a_series_change_reuses_the_legend_layer` | base 1, series 2, legend 1 | pass |
| 15 | `a_legend_change_rebuilds_only_the_legend_layer` | base 1, series 1, legend 2 | pass |
| 16 | `the_dataset_strip_is_a_retained_picture` | 6 commands, `strip_record_count()` stays 1 on repaint | pass |
| 17 | `a_smith_frame_paints_the_gamma_grid_and_trace` | 3 polylines (boundary, grid, trace), base 1, series 1 | pass |
| 18 | `a_zoom_selection_paints_above_the_chart` | a 100×80 rect command exists | pass |
| 19 | `the_semantic_node_exposes_the_chart_commands` | `chart-0-zoom-to-fit` action reaches the command sink | pass |
| 20 | `a_drag_reports_the_chart_content_coordinates` | press/move/release reach the callbacks with `y` relative to `content_y` | pass |

Notable test change during this work: `charts_share_the_content_height_equally` originally asserted absolute rect `y` values (`10.0`, `160.0`). Once the base became a retained picture replayed with a translation, the recorded rect `y` is chart-local and the slot offset lives in `transform().translate_y`. The assertion was corrected to `rect.y == 10.0` with `translate_y == 0.0` and `150.0` respectively. **This is a real semantic change to be aware of when reading draw commands: layer-local coordinates plus a replay transform, not absolute panel coordinates.**

### 4.2 Unit tests — `StudioChartsControllerChecks`

Source: `studio-tests/charts_controller.test.cpp`

| Test | Asserts | Result |
|------|---------|--------|
| `one_expression_keeps_one_color_across_the_dataset` | `V(out)` has the same RGB in two charts; `I(out)` differs | pass |

### 4.3 Full-suite and integration runs

| Check | Command | Result |
|-------|---------|--------|
| Full unit suite | `./.build-debug/studio-tests/xyce-studio-tests` | 81 → 85 → 86 → 87 → 90 tests, all pass (after each layer step) |
| Full UI suite | `.venv/bin/python -m unittest discover -s tests_studio -p "*_test.py"` | 42 → 44 → 48 tests, OK |
| Format/lint | `bash build/check-format.sh` | see §4.4 |

UI scenarios relevant to the strip and interactions (`tests_studio/charts_test.py`):

| Test | Result |
|------|--------|
| `test_switching_tabs_updates_the_active_dataset_name` | pass |
| `test_closing_the_last_user_dataset_returns_to_the_editor` | pass |
| `test_the_chart_context_menu_commands_change_the_panel` | pass |
| `test_the_plot_selection_dialog_edits_a_chart` | pass |
| `test_a_refused_custom_expression_keeps_the_dialog_open` | pass |
| `test_a_smith_plot_selection_offers_no_custom_expression` | pass |

**Important limitation:** none of these UI scenarios clicks on the chart body. They exercise semantic actions only. There is currently **no automated regression test for the click defect**, and the defect is not covered by the suite that reports green.

### 4.4 Format / lint

`bash build/check-format.sh` initially reported 46 comment-rule violations across `core/src/charts/chart_engine.cpp` and `.h` (pre-existing plus newly touched lines). Fixed by collapsing consecutive comment lines and shortening comments over 124 characters. A subsequent clang-format violation on the include block of `chart_view.cpp` was also fixed by reordering `<cstdint>`, `<functional>`, `<string>`. Final output: `comments: 14 changed files hold no long comment line and no consecutive pair`.

A `-Wreorder-ctor` warning appeared when `m_menu_text` was initialised out of declaration order; fixed by moving it after `m_outline_resolver` in the constructor initialiser list.

### 4.5 Visual captures of each approved layer

Captured with the MCP `capture.screenshot` tool from a live debug build.

| Capture | Setup | Observation |
|---------|-------|-------------|
| `series-step.png` | `ac-simple-01.cir` + Xyce run | series drawn, base/grid/axes correct |
| `legend-step.png` | `ac-simple-01.cir` + Xyce run | legend block, global expression colours |
| `smith-step.png` | `--dataset lin-simple-01.s2p` | gamma grid, boundary, trace, labels; semantic name `1 smith chart` |
| `tabs-step.png` | two `tran-simple-01.csv` datasets | both tabs, active plate/underline; semantic name `2 datasets, 1 line chart, active: tran-simple-01 (2)` |

### 4.6 Click message-injection attempts

This is where most of the debugging time went, and the negative results matter.

| Attempt | Method | Result |
|---------|--------|--------|
| A | `osascript -e 'tell application "System Events" to click at {720,468}'` without activating | screenshot md5 unchanged (`4523c51…`) for 5 frames → **click never delivered**, false negative |
| B | `osascript … set frontmost to true` then `click at {720,468}` | md5 unchanged (`4523c51…`) for 6 frames → **still not delivered** |
| C | Swift `CGEvent` mouse down/up at `{720,468}` with no activation | md5 unchanged → **not delivered** |
| D | Swift `NSWorkspace.activate(.activateAllWindows)` + `CGEvent` down/up at `{720,468}` | md5 changed to `90cba74…` → **delivered**; `click4-after-0…5.png` all identical |
| E | Swift activate + `leftMouseDown` at `(500,430)` + 10 `leftMouseDragged` to `(900,700)` | md5 `00790b1…`; `drag-after-0…3.png` identical; drag state captured |
| F | Swift activate + `leftMouseUp` at `(900,700)` | md5 `4440d9a…`; `up-after-0…5.png` identical |
| G | Swift activate + click at `(720,452)`, 30 screenshots at 30 ms, deduplicated by md5 | exactly **2 distinct hashes**: `2c7ad97…` (frame 0, correct) and `b95760b…` (frame 2 onward, gray band) |
| H | `strip-late.png` ~1 s after the click | identical to `clicktab-frame-02.png` (same byte size, 173753) → **persistent, not transient** |

Conclusion from G/H: the transition from correct to broken happens within the first ~60 ms after the click and then never changes. There is no intermediate flicker sequence to inspect — the paint either includes the strip or does not.

Capture folder: `/private/var/folders/7r/clg5kv714dq2f6fldywnjsww0000gn/T/opencode/charts/`

Helper scripts in the same folder: `click.swift`, `drag.swift`, `dragup.swift`, `clicktab.swift`.

### 4.7 Instrumented run — the key measurement

Build with the §3 diagnostics, launch the two-dataset reproduction, deliver one click at `(720,452)`, then read `strip-debug.log`:

```
SET TABS 1
SET CONTENT 32.0
SET TABS 2
SET CONTENT 32.0
SET TABS 2
SET CONTENT 32.0
PAINT bounds=0.0,52.0 1200.0x727.9 tabs=2 content=32.0 strip=0x0 frames=1
PAINT bounds=0.0,52.0 1200.0x727.9 tabs=2 content=32.0 strip=0x6000006a81d8 frames=1
PAINT bounds=0.0,52.0 1200.0x727.9 tabs=2 content=32.0 strip=0x6000006a81d8 frames=1
```

Reading:

- The panel bounds are `0,52 / 1200×727.9` and do **not** change across the click.
- `tabs=2` and `content=32.0` are stable across the click. The view still knows about both tabs and still reserves the strip.
- The first post-click paint prints `strip=0x0`, but that line precedes the record block, so this is expected.
- Paints 2 and 3 print a **valid strip picture pointer**. Therefore `paint_content()` is recording the strip and calling `draw_picture()` for it on every failing frame.
- `frames=1` — one chart frame, which is correct for a dataset with one auto-created chart.

This is the decisive observation so far: **the strip draw command is emitted, yet the pixels do not appear.**

### 4.8 Semantic tree during the failure

Queried while the gray band was on screen:

| Node | Value |
|------|-------|
| `charts_view` | role `canvas`, name `2 datasets, 1 line chart, active: tran-simple-01 (2)` |
| `main_status` | `TIME=4.89 ms V(1)=4.99 V V(2)=5 V` |
| `main_output` | `Simulated Output` button, `disabled: True` |
| `output_panel` / `output_close` / `output_log` | absent |

Interpretation:

- The presenter state is intact: two datasets, correct active tab name. This is consistent with §4.7 (tabs still known), but it does **not** prove the strip paint reached the screen.
- The status bar shows a hover readout, so `PointerMove`/`PointerEnter` reached `hover_moved()`.
- The output panel nodes are absent while `main_output` is disabled, i.e. the output panel is hidden in this state. This matched a visually observed "output panel disappears" at one point. It is **not yet established** whether that is a separate defect or the same repaint problem in the shell layout. It was observed once and not re-verified in isolation.

### 4.9 Click semantics on the current code path

A single `PointerDown` in the chart body does all of the following:

1. `m_dragging = true`
2. `m_on_zoom_drag_started(x, y)` → presenter → `controller->zoom_drag_started(x, y)`
3. If it returns true (pointer inside a plot rect), `publish_zoom_selection()` runs
4. `zoom_selection()` returns a **zero-area rect** (start == current), not `nullopt`
5. `m_view->set_zoom_selection(rect)` → `invalidate()`
6. On release, `zoom_drag_ended()` applies the window; for a zero-area window it may report "changed"
7. If changed, `publish_scene()` runs the full republish: `set_tabs`, `set_content_y`, `set_frames`, and `set_zoom_selection(std::nullopt)`

Additionally, `set_frames()` calls `cancel_drag()` **before** storing the new frames, and `cancel_drag()` synchronously calls back into the presenter, which calls `set_zoom_selection(std::nullopt)` on the view. So the republish re-enters the view while it is in the middle of updating its own state.

There is no movement threshold: a click is treated as a complete zoom drag from press to release.

This churn is the only new behaviour a click introduces relative to an idle panel. It is the prime suspect for a repaint path that differs from the initial full paint, but it has not been proven to be the cause.

---

## 5. Facts established

- **F1.** The strip picture is recorded and replayed on the failing frames; its pointer is non-null (§4.7).
- **F2.** `tabs`, `content_y`, and the panel bounds are identical before and after the failure (§4.7).
- **F3.** The failure is persistent and stable, not a transient intermediate frame (§4.6 G, H).
- **F4.** The chart layers (base, series, legend) remain visible in the failing frame, and only the strip band is lost.
- **F5.** The failure appears only after a click; the same panel renders the strip correctly on first paint (`tabs-step.png`, `clicktab-frame-00.png`).
- **F6.** A click triggers a zero-area zoom selection and, on release, the full republish path (§4.9).
- **F7.** The retained-layer cache logic is correct in isolation: unit tests prove reuse and rebuild behaviour for base, series, legend, and strip.
- **F8.** A retained layer's draw commands are chart-local; the slot offset is applied through `draw_picture`'s transform (§4.1, item 4).
- **F9.** The UI suite never clicks the chart body, so the defect is outside current automated coverage (§4.3).

## 6. Ruled out

- **Old chart implementation.** `ChartsView`, `ChartScene`, `ChartSceneBuilder` were deleted. They cannot be involved.
- **Loss of tab state.** `set_tabs` / `set_content_y` hold stable values across the failure.
- **Strip never recorded.** The picture exists and is non-null on every failing paint.
- **Layout shift.** The content is not displaced; the 32 px band is the unfilled strip reservation.
- **Cache invalidation storm as the sole cause.** Even if the strip is recorded inside the failing paint, the result is the same; recording is not skipped.
- **A failure of the click to reach the app.** Proven by the md5 transition in §4.6 D and G. The three earlier "no change" attempts were delivery failures, not app behaviour.

## 7. Root Cause Diagnosis (Proven in `albor`)

The defect is caused by bugs in the upstream `albor` UI framework rendering engine (`albor/render/render-albor/` and `albor/render/draw_command.cpp`) affecting `draw_picture` during damage-scoped (partial) repaints on the GPU backend (Metal on macOS, D3D on Windows).

---

### 7.1 Detailed Mechanism & Mathematical Proof

#### Geometry & Setup in the Reproduction
- Window size: `1200 × 832` logical points; Retina scale `device_scale == 2.0` (physical raster: `2400 × 1664` pixels).
- `ChartView` bounds: `Rect{0.0, 52.0, 1200.0, 727.9}` (in scene/window coordinates).
- Tab strip reservation: `m_content_y == 32.0`.
- Strip position in window: `x = 0..1200`, `y = 52..84` logical (`y = 104..168` device pixels).
- Chart content position in window: `x = 0..1200`, `y = 84..779.9` logical (`y = 168..1560` device pixels).

#### What Happens on Frame 0 (Initial Full Paint)
1. Window launches. `AlborRendererBackend::execute_frame` runs for the initial full frame:
   - `scene.is_partial() == false`.
   - `m_clip.rect` is initialized to the whole window: `Rect{0.0, 0.0, 1200.0, 800.0}`.
   - `context.clip_rect(damage)` is **not** called.
2. The tab strip is painted via `context.draw_picture(m_strip_layer.picture, Transform::translation(0, 52))`.
3. Even though bugs exist in `scissor()` and clip handling (see below), they accidentally cancel out on Frame 0 because `m_clip.rect.y == 0`:
   - `device.apply(m_clip.rect.y)` applies `translation(0, 52)` to `0`, resulting in `52` logical (`104` device), which coincides with the top of the strip.
   - `m_clip.rect.intersect(tab_rect)` intersects `[0..800]` with `[0..32]`, leaving `[0..32]` (non-empty).
   - Hence, on Frame 0, the tab strip renders and is visible on screen (`tabs-step.png`, `clicktab-frame-00.png`).

#### What Happens on Frame 1+ (After Left Click)
1. **Trigger:** A left click delivered at `(720, 452)` screen coordinate (`(600, 284)` window-local) lands in the chart body.
   - `ChartView::dispatch_event` receives `EventKind::PointerDown` and calls `m_on_zoom_drag_started(x, y)`.
   - `ChartsPresenter` calls `controller->zoom_drag_started(x, y)`, which returns `true` (starting a drag).
   - `ChartsPresenter::publish_zoom_selection()` calls `m_view->set_zoom_selection(Rect{x, y, 0, 0})`.
   - `ChartView::set_zoom_selection` calls `invalidate()`.
   - `UiNode::invalidate()` sets `m_damage_region = bounds()` (`Rect{0.0, 52.0, 1200.0, 727.9}`).
2. **Partial Frame Setup:**
   In `AlborRendererBackend::execute_frame` (`../albor/albor/render/render-albor/albor_renderer_backend.cpp#L60-L70`):
   ```cpp
   if (partial) {
       const Rect damage = *scene.damage_rect();
       context.save();
       context.clip_rect(damage);
       context.draw_rect(damage, {.fill = scene.background(), .composition = CompositionMode::Copy});
       replay_commands(scene.commands(), context, full, damage);
       context.restore();
   }
   ```
   - `context.clip_rect(damage)` is called:
     - `m_clip.rect` becomes `Rect{0.0, 52.0, 1200.0, 727.9}` (in scene coordinates).
     - `m_clip_device` becomes `Identity.then(scaling(2, 2))`.
   - `context.draw_rect(damage, Copy)` clears the entire damaged area (`y = 52..780`) to the background color (`#EDEDED` / `rgb(237, 237, 237)`).
3. **Execution of `scene.commands()`:**
   - First command: `context.draw_rect(bounds(), m_palette.background)` runs. Fills `y = 52..780` with background.
   - Second command: `DrawCommand::picture` for `m_strip_layer.picture` with `m_transform == Transform::translation(0, 52)`.
   - `command.replay(context)` calls `AlborRenderContext::draw_picture(picture, Transform::translation(0, 52))` (`albor_render_context.cpp#L619-L630`):
     ```cpp
     const Transform outer = m_transform;
     m_transform = transform.then(outer); // m_transform is now translation(0, 52)
     replay_commands(picture->commands(), *this, m_extent);
     m_transform = outer;
     ```
4. **Failure 1 — Double-Translation in Scissor Box:**
   Inside `replay_commands` (`../albor/albor/render/draw_command.cpp#L707-L758`):
   - `Rect applied = unclipped;` where `unclipped == m_extent == Rect{0.0, 0.0, 1200.0, 800.0}`.
   - The strip plate command inside `picture` was recorded with `command.clip() == DrawCommand::unclipped()` (`Rect{-1e6, -1e6, 2e6, 2e6}`).
   - Because `command.clip() != applied` (`-1e6 != 0`), `replay_commands` executes:
     ```cpp
     if (command.clip() != applied || command.clip_radius() != applied_radius) {
         context.save();
         context.clip_rect(command.clip());
         applied = command.clip();
         switched = true;
     }
     ```
   - Inside `AlborRenderContext::clip_rect(command.clip())` (`albor_render_context.cpp#L60-L71`):
     ```cpp
     m_clip_device = device_transform(); // m_transform is currently translation(0, 52)!
     clip_to_rect(m_clip, rect);
     ```
     - `m_clip_device` is updated to `device_transform()`, which includes `Transform::translation(0, 52).then(scaling(2, 2))`!
     - `m_clip.rect` remains `Rect{0.0, 52.0, 1200.0, 727.9}` (from the damage rect, which was already in scene space).
   - Now `command.replay(context)` draws the plate:
     `Contour contour = rect_contour(Rect{0, 0, 1200, 32}); to_device(contour);`
     Vertices are mapped by `device_transform()`:
     - `y = (0 + 52) * 2 = 104` device pixels.
     - `y = (32 + 52) * 2 = 168` device pixels.
   - Geometry emission records the draw scissor via `AlborRenderContext::scissor()` (`albor_render_context.cpp#L173-L193`):
     ```cpp
     const Point origin = device.apply(Point{.x = m_clip.rect.x, .y = m_clip.rect.y});
     const Point corner = device.apply(Point{.x = m_clip.rect.x + m_clip.rect.width, .y = m_clip.rect.y + m_clip.rect.height});
     ```
     `device` is `m_clip_device` (which has `translation(0, 52)` applied):
     - `origin.y = (m_clip.rect.y + 52) * 2 = (52 + 52) * 2 = 208` device pixels!
     - `corner.y = (m_clip.rect.y + height + 52) * 2 = (779.9 + 52) * 2 = 1663.8` device pixels!
     - The scissor box bounds passed to Metal are `y = [208.0, 1663.8]`.
   - In the Metal fragment shader (`albor_shaders.metal#L68-L86`):
     Every fragment of the strip is at `y = 104..168`.
     Since `y = 168 < 208`, the distance to the scissor box is positive (`distance > 0`).
     `scissor_mask` computes `clamp(0.5 - distance * scale, 0.0, 1.0) == 0.0`.
     **100% of the strip plate pixels are masked to 0 alpha by the GPU scissor shader.**
5. **Failure 2 — Picture-Local Clip Collapsing Scene-Space Clip to Empty:**
   For tab labels, `paint_strip` records:
   ```cpp
   context.save();
   context.clip_rect(tab); // tab is Rect{x, 0.0, tab_width, 32.0} in picture-local coordinates
   draw_text(context, label, origin, paint);
   context.restore();
   ```
   When `replay_commands` replays this text command:
   `command.clip()` is `Rect{x, 0.0, tab_width, 32.0}` (untransformed picture-local coordinates).
   `AlborRenderContext::clip_rect(Rect{x, 0.0, tab_width, 32.0})` calls:
   ```cpp
   clip_to_rect(m_clip, rect); // m_clip.rect = m_clip.rect.intersect(rect);
   ```
   Where `m_clip.rect` is `Rect{0.0, 52.0, 1200.0, 727.9}` (scene coordinates).
   `[52.0, 779.9].intersect([0.0, 32.0])` is **empty** (`Rect{0, 0, 0, 0}`).
   Once `m_clip.rect.empty() == true`:
   `AlborRenderContext::drawable()` returns `false`.
   `draw_text` is aborted at line 301 before emitting any vertices.

---

### 7.2 Affected Files & Code Locations in `albor`

The following files in the `albor` repository (`../albor`) contain the defect:

1. **`albor/render/render-albor/albor_render_context.cpp`:**
   - **Line 60–71 (`AlborRenderContext::clip_rect`):**
     ```cpp
     void AlborRenderContext::clip_rect(const Rect& rect) {
         m_clip_device = device_transform();
         ...
         clip_to_rect(m_clip, rect);
     }
     ```
     *Problem 1:* When called during picture replay, `device_transform()` includes the picture's local `m_transform`. But `m_clip.rect` was set outside the picture in scene space. When `scissor()` applies `m_clip_device` to `m_clip.rect`, the translation is applied twice.
     *Problem 2:* `clip_to_rect(m_clip, rect)` intersects `m_clip.rect` (scene space) directly with `rect` (which is picture-local space when replaying a picture's inner clip).
   - **Line 173–193 (`AlborRenderContext::scissor`):**
     Calculates scissor bounds by mapping `m_clip.rect` through `m_clip_device`. If `m_clip.rect` is already in scene space, `m_clip_device` should only apply the window-to-device scaling, not local picture translations.
   - **Line 619–630 (`AlborRenderContext::draw_picture`):**
     ```cpp
     void AlborRenderContext::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
         if (!drawable() || !picture || picture->empty())
             return;
         const Transform outer = m_transform;
         m_transform = transform.then(outer);
         replay_commands(picture->commands(), *this, m_extent);
         m_transform = outer;
     }
     ```
     *Problem:* Does not isolate the clip state. Compare with `Blend2DRenderContext::draw_picture` (`blend2d_render_context.cpp#L990-L1009`) which wraps replay in `m_context->save()` / `m_context->restore()` and passes `m_clip`. Furthermore, picture commands have clips recorded in picture-local space, which are not mapped to scene space when replayed against a scene-space `RenderContext`.

2. **`albor/render/draw_command.cpp`:**
   - **Line 707–750 (`replay_commands`):**
     ```cpp
     void replay_commands(const std::vector<DrawCommand>& commands, RenderContext& context, const Rect& unclipped, const std::optional<Rect>& damage) {
         Rect applied = unclipped;
         ...
         for (const DrawCommand& command : commands) {
             if (command.clip() != applied || command.clip_radius() != applied_radius) {
                 ...
                 context.clip_rect(command.clip());
                 applied = command.clip();
             }
     ```
     *Problem:* `DrawCommand::unclipped()` is a static sentinel `Rect{-1.0e6f, -1.0e6f, 2.0e6f, 2.0e6f}` (`draw_command.cpp#L14-L19`). But callers pass `unclipped == m_extent` (`Rect{0, 0, width, height}`). Therefore, `command.clip() != applied` is **always true** on the very first unclipped command in a picture, forcing a spurious `context.clip_rect(command.clip())` call that corrupts `m_clip_device`.

3. **`albor/render/render-albor/albor_renderer_backend.cpp`:**
   - **Line 59–70 (`AlborRendererBackend::execute_frame`):**
     Sets the damage clip `context.clip_rect(damage)` in scene space before replaying commands. While setting damage clip is conceptually correct, `AlborRenderContext` cannot currently handle nested pictures replaying under transforms while a damage clip is active.

---

### 7.3 Prescribed Fixes for the `albor` Agent

The coding agent fixing this in `albor` should apply the following changes:

#### Fix 1: Disentangle Scene-Space Clips from Picture-Local Transforms in `AlborRenderContext`
In `AlborRenderContext`:
- Track whether a clip is in scene space or local space. When `clip_rect(rect)` is called:
  - If called from user code or `PictureRecorder`, `rect` is local to the current `m_transform`.
  - The scene-space clip rectangle should be `m_transform.map(rect)`.
  - `m_clip.rect` should always be stored in **scene coordinates**.
  - `m_clip_device` should be `Transform::scaling(m_device_scale, m_device_scale)` (device raster transform), so that `m_clip_device.apply(m_clip.rect)` directly maps scene coordinates to physical device pixels, **regardless of what `m_transform` is active when drawing inner commands**.

#### Fix 2: Unclipped Sentinel Handling in `replay_commands`
In `albor/render/draw_command.cpp`:
- In `replay_commands`:
  Check whether `command.clip() == DrawCommand::unclipped()`. If a command is unclipped, do not push a narrower clip or call `context.clip_rect(...)`. Initialize `applied` to `DrawCommand::unclipped()` so that unclipped commands do not trigger clip state transitions.

#### Fix 3: Picture Inner Clip Mapping in `draw_picture`
When `AlborRenderContext::draw_picture(picture, transform)` replays `picture->commands()`:
- Commands inside `picture` that have clips (such as tab label clip `Rect{x, 0, w, 32}`) have clips recorded in picture space.
- When replaying into `AlborRenderContext`, any inner clip must be mapped through `transform` into the parent's coordinate space before being passed to `context.clip_rect(...)`.

#### Fix 4: Unit Test to Add in `albor`
Add a test in `albor/tests/conditional/render_albor_metal.test.macos.mm` and `albor/tests/render_albor.test.cpp`:
```cpp
TEST(AlborParityChecks, DamageScopedPartialFrameRendersTranslatedPicture) {
    // 1. Create a surface with metrics 800x600, device scale 2.0.
    // 2. Build a FrameScene with is_partial = true and damage_rect = Rect{0, 50, 800, 550}.
    // 3. The scene contains a DrawCommand::picture replayed with Transform::translation(0, 50).
    // 4. The picture contains a filled rectangle at picture-local Rect{0, 0, 800, 30}.
    // 5. Execute the frame via backend.execute_frame(surface, scene).
    // 6. Assert that pixels at (100, 60) match the picture fill color and are not masked to 0 by scissor.
}
```

---

## 8. Summary of Findings

- The bug is located in the **`albor` UI framework**, specifically in `render-albor` (Metal / GPU rendering backend) picture replay and clip/scissor calculation during partial repaints.
- The `xyce-studio` retained-layer model in `ChartView` is functioning as intended:
  - Cache keys, record counts, and picture pointers are all intact and stable across clicks.
  - The strip draw command is correctly emitted to the render context.
- When any click occurs in the chart, `invalidate()` correctly requests a damage frame for the changed region.
- `albor`'s partial-frame execution clears the damage area and attempts to replay the commands, but `albor`'s scissor calculation double-translates picture layers, clipping the top 32 px strip entirely out of the GPU render pass.

---

## 9. Next Steps

The diagnosis above led to `albor` commit `0648e5c` — "fix: rebase a picture's clip into its own space". The fix is present in the current `albor` source and has been validated against the original reproduction; see §11 for the evidence and the mapping between the prescribed fixes and the change that landed.

## 10. Working-tree state

Uncommitted changes relevant to this document:

| File | Role |
|------|------|
| `studio/src/views/charts/chart_view.h` | view: retained layers, strip, input, context menu, overlay |
| `studio/src/views/charts/chart_view.cpp` | painting, cache keys, strip, zoom/hover/context menu |
| `studio/src/presenters/charts_presenter.h` / `.cpp` | dataset/tab publishing, zoom/hover/context-menu callbacks, `publish_zoom_selection` |
| `studio/src/charts/charts_controller.h` / `.cpp` | frames, zoom, chart state, `ChartColorRegistry` member |
| `core/src/charts/chart_engine.h` / `.cpp` | `ChartColorRegistry`, color assignment |
| `studio-tests/chart_view.test.cpp` | retained-layer, strip, zoom/overlay, Smith, cache tests |
| `studio-tests/charts_controller.test.cpp` | dataset-wide color test |
| `tests_studio/charts_test.py` | tab, context-menu, plot-dialog scenarios |

Known interaction gaps that are unrelated to this defect but still open: resize-settle deferral (geometry refreshes on every `arrange`), chart reorder gestures, and the step tool.

---

## 11. Fix Validation

### 11.1 Fix under test

- Repository: `../albor`
- Commit: `0648e5c` — "fix: rebase a picture's clip into its own space (§6.2, §6.4, AD-05, AD-16)"
- Parent: `19eaf79` (Skia backend retired)

What the commit changes, in the terms of §7:

| Prescribed fix (§7.3) | What landed |
|---|---|
| Fix 1 — store clips in one space, use the device scale for the scissor | `AlborRenderContext` gains `m_clip_scene`. A picture replayed with an axis-aligned placement rebases the container clip into picture space; a rotated or sheared placement converts it to scene space and scissor-maps it with the device scale alone. |
| Fix 2 — do not treat `DrawCommand::unclipped()` as a narrower clip | The picture list now replays against `DrawCommand::unclipped()` (previously `m_extent`), so an unclipped command no longer forces a spurious `clip_rect`. |
| Fix 3 — map picture-inner clips into the parent space | Inner clips are rebased through the picture placement (`clip_to_picture`), including the radius, and the frozen device transform follows the rebased clip. |
| Fix 4 — regression tests | Tests added in `albor/tests/render_albor.test.cpp`, `tests/blend2d.test.cpp`, and the mirrored Metal/D3D conditional suites: translated, scaled, nested, rotated pictures, inner clips, rounded clips, singular placements. |

The fix also refuses a collapsing picture placement before computing its inverse, and restores the clip, clip transform, clip space and placement after the replay.

### 11.2 Build integration

`CMakePresets.json` points `ALBOR_LOCAL_SOURCE_DIR` at `${sourceDir}/../albor`, so the debug build compiles the local checkout. A rebuild after the fix recompiled and relinked the changed albor sources:

```
[1/6] Building CXX object albor/albor/render/render-albor/CMakeFiles/albor-render-albor.dir/albor_render_context.cpp.o
[2/6] Building OBJCXX object albor/albor/render/render-albor/CMakeFiles/albor-render-albor.dir/albor_metal_backend.macos.mm.o
[3/6] Linking CXX static library albor/albor/render/render-albor/libalbor-render-albor.a
[4/6] Linking CXX executable studio-tests/xyce-studio-tests
[5/6] Linking CXX executable xyce-studio-next
```

The framework change is therefore compiled into both the app and the test binary used below.

### 11.3 Albor test suite (independent validation)

Rebuilt with `cmake --build --preset test` and ran `.build-test/tests/albor-tests` in `../albor`.

| Run | Result |
|---|---|
| `--gtest_filter='*picture*:*Picture*:*clip*:*Clip*'` | 138 tests, all pass |
| Full suite | **4283 passed**, 1 skipped (`MacosWindowMetrics.TheWindowServerFrameMatchesTheAppKitFrame`, environment-dependent) |

Directly relevant tests added by the fix, all passing:

- `ATranslatedPictureInsideANarrowClipKeepsItsInk` — the exact shape of the strip failure: a translated picture inside a narrower container clip.
- `ATranslatedPictureKeepsTheSceneClipOnTheRaster` — the damage-scoped frame equivalent.
- `APictureInnerClipNarrowsInsideThePictureSpace` — the shape of the series layer: a clip recorded *inside* the picture.
- `ANestedPictureMapsBothClips` — nested retained layers.
- `APictureScaledInsideAClipMapsTheSceneClipOnce` — the clip is mapped once, not twice.
- `ARotatedPictureMapsItsInnerClipIntoSceneSpace` — the rotated-placement fallback.
- `ASingularPlacementUnderAScaledSceneReplaysNothing` — the collapsing placement the fix now refuses before inverting.

The conditional Metal and Direct3D parity suites were extended with the matching rotated/scaled/clipped picture cases. The Metal mirror ran and passed above; the Direct3D mirror was not executed because this machine is macOS.

### 11.4 Live reproduction — two datasets, left click

Same setup as §1.2: two `tran-simple-01.csv` datasets, window `120,168 / 1200x832`, real CGEvent click at `(720,452)`, 30 captures at 30 ms, deduplicated by md5, plus one capture 0.5 s later.

| Measurement | Before the fix | After the fix |
|---|---|---|
| Distinct frames after the click | 2 (`2c7ad97…` correct, `b95760b…` broken) | **1** (`9de445b62b8b2b34d860ec6ff435f87d`) |
| Frame 0.5 s after the click | broken (`strip-late.png`) | **identical to frame 0** (`fix-late.png`) |
| Dark pixels in the strip band (device rows 104–168) | 0 (the whole band is background: 153 600 gray pixels) | **1738** — identical to the pre-click count, with 130 338 gray pixels |
| Tab strip visible | no | **yes**, both tabs, active plate and underline |
| Chart layers | correct | correct |

Interpretation: after the click the panel now presents one stable frame, the strip occupies its full 1200×32 reservation, and nothing outside the strip changed. The former failure sequence — good frame, then a permanently broken frame — no longer exists.

Capture names in `/private/var/folders/7r/clg5kv714dq2f6fldywnjsww0000gn/T/opencode/charts/`: `fix-before.png`, `fix-frame-00.png`, `fix-late.png`. The old failing captures (`clicktab-frame-00.png`, `clicktab-frame-02.png`, `strip-late.png`) are kept for comparison.

### 11.5 Live reproduction — simulation run, left click

The user's original report: run a simulation, then left click in the chart.

```
./.build-debug/xyce-studio \
  --netlist netlists/ac-simple-01.cir \
  --xyce ~/.local/bin/Xyce
```

Simulation completed, then the same click at `(720,452)`.

| Measurement | Result |
|---|---|
| Distinct frames after the click | 1 (`09e52e4072e52a04b1ca2057b9737d76`), stable 0.5 s later |
| Difference against the pre-click frame | bounding box **only** `(33,1570)–(1398,1592)` — the status bar hover readout |
| Chart, axes, labels, series, legend, tab area | unchanged and fully rendered |
| Output panel | visible, unchanged |
| Click delivery | proven by the hover readout `FREQUENCY=49.4 kHz …` |

This is the scenario that originally produced the gray band. The only pixel change caused by the click is the readout text; the panel no longer loses a layer or shifts.

### 11.6 Additional coverage

- **Two charts in one panel.** Invoking `chart-0-add-chart` produced the semantic name `2 datasets, 2 line charts, active: tran-simple-01 (2)` and one frame with both slots painted, the strip intact, and the translated picture layers of both charts rendered (`fix-two-charts.png`).
- **Diagnostics removed.** The `SET TABS` / `SET CONTENT` / `PAINT` logging and `<cstdio>` were removed from `studio/src/views/charts/chart_view.cpp` before the final runs; the file matches the architecture described in §2.

### 11.7 Suites after the fix

| Check | Command | Result |
|---|---|---|
| Studio unit | `./.build-debug/studio-tests/xyce-studio-tests` | **90 passed** |
| Studio UI | `.venv/bin/python -m unittest discover -s tests_studio -p "*_test.py"` | **48 passed** |
| Format / comments | `bash build/check-format.sh` | clean |
| Albor | `.build-test/tests/albor-tests` | **4283 passed, 1 skipped** |

### 11.8 Tooling note

While capturing the drag sequence, `capture.screenshot` timed out twice. The app was not hung: `sample` showed the main thread idle in `MacosEventLoop::run` waiting for the next event, and `semantic.find` answered normally. The capture call appears to wait for a presented frame; once a pointer move dirtied the view, the screenshot returned immediately. This is a capture-tool quirk under no-op input, not application behaviour, and it does not affect the click validation above (the click always dirties the view through the hover readout and the zoom selection).

### 11.9 Residual gap

The defect is now fixed at the framework level, but the repository still has **no automated UI scenario that clicks the chart body**; the `tests_studio` suite exercises tabs, context-menu commands, and the plot dialog through semantic actions, so it would not have caught this regression. A scenario that clicks the chart body and asserts the strip is still painted would be worthwhile, but it needs a pixel assertion (the semantic tree does not expose whether the strip picture reached the screen), which the current automation layer does not provide out of the box.

Nothing has been committed. The `albor` fix is a separate repository commit on branch `fix/picture-clip-damage-frame` (commit `0648e5c`, not yet on `master`); the `xyce-studio` working tree remains uncommitted on `refactor/dual-root-structure`.

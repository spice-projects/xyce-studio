# Native Albor Chart Engine Plan

## 1. Decision

The `studio` root gets a native Albor chart component. It renders **Line** and **Smith** charts directly through `RenderContext`; it does not use SVG, ImGui/ImPlot or Skia.

The data and geometry already exist in `core`:

- `ChartEngine` owns series data, step selection, axis assignment, zoom windows, decimation, hover mapping and Smith data.
- `ChartLayout` turns engine state into a `ChartFrame`: plot rect, ticks, grid lines, series polylines, legend geometry and Smith grid/labels.
- `ChartPalette` and `chart_style.h` own the colours and metrics.

The studio side is therefore a **translator and a painter**, not a second chart engine.

### Non-goals for this plan

- ImGui/ImPlot integration. AlborCore/albor#211 (`draw_mesh`) is independent of this plan and is not a dependency.
- Skia. The Skia backend is being removed and must not be used.
- SVG command strings. Series are built as retained `PathResource` contours.
- 3D, general-purpose plotting, or a second chart layout engine.

---

## 2. Architecture

```text
XyceOutputFile / ExpressionManager / StepInformation
                      │
                      ▼
              core::ChartEngine          (data, zoom, steps, axes, hover)
                      │
                      ▼
              core::ChartLayout          (ChartFrame geometry)
                      │
                      ▼
        studio::ChartSceneBuilder        (ChartFrame → retained commands)
                      │
                      ▼
        studio::ChartScene               (PictureResource base + overlay)
                      │
                      ▼
        studio::ChartsView               (native Albor Widget)
                      │
                      ▼
              Albor RenderContext
```

### Ownership

| Object | Owner | Lifetime |
| --- | --- | --- |
| `XyceOutputFile` / `ExpressionManager` / `StepInformation` | `ChartsPresenter` dataset list | Until the plot tab closes |
| `ChartEngine` per chart | `ChartsPresenter` dataset state | Dataset lifetime |
| `ChartLayout` + text measurer | `ChartsPresenter` | Process lifetime |
| `ChartSceneBuilder` + `ResourceFactory` | `ChartsPresenter` | Process lifetime |
| `ChartScene` (retained pictures) | `ChartsView` | Replaced on state change |
| `ChartsView` native component | `MainWindowView` tree | Window lifetime |

Everything stays on the UI thread initially. `ChartLayout` is pure and can move to a worker later, but the first implementation avoids cross-thread data races.

---

## 3. Native component

### 3.1 `studio::ChartsView`

A C++ `Widget` subclass, not a `.ui` draw block:

```cpp
class ChartsView final : public albor::Widget
{
public:
    ChartsView(albor::BindingEngine* engine, albor::TextEngine* text);

    void set_scene(std::shared_ptr<const ChartScene> scene);
    void set_palette(const ChartPalette& palette);
    void set_overlay(std::shared_ptr<const OverlayScene> overlay);

    void paint_content(albor::RenderContext& context) override;
    void dispatch_event(albor::Event& event) override;
    void measure(const albor::SizeConstraints& constraints, albor::Size& out) override;
    void arrange(const albor::Rect& bounds) override;

    [[nodiscard]] albor::SemanticNode semantic_node() const override;
};
```

- `paint_content` only draws retained commands: `draw_picture(scene->base)`, then `draw_picture(scene->overlay)`.
- `dispatch_event` handles pointer down/move/up and wheel; it raises chart signals through `std::function` callbacks installed by the presenter.
- `measure` fills the offered bounds when they are bounded, so a host panel gives the view the whole page. A default size is used when unbounded.
- `arrange` records the viewport size and schedules a rebuild when the size settles.
- `semantic_node` reports `SemanticRole::Canvas`, the explicit id `charts_view`, and the chart count/name in the accessible label.

### 3.2 `.ui` integration constraint

Albor's `.ui` codegen currently rejects native elements (`E0501`, "not a generated element type"); the type checker accepts them but the code generator has no construction path. The first slice therefore constructs `ChartsView` in C++ and inserts it into a host container by id:

```cpp
// main_window.ui
Panel { id: charts_host; padding: 0px; }

// MainWindowView::create()
auto chart = std::make_unique<ChartsView>(&stage->binding_engine(), &stage->text_engine());
chart->set_explicit_id("charts_view");
host->add_child(std::move(chart));
host->invalidate_layout();
```

The view adapter keeps a borrowed pointer to the component for scene updates. If Albor adds native-element codegen, switch to a declarative `ChartsView { id: charts_view; }` and keep the same C++ implementation.

The host must give the view the full page. `ChartsView::measure` is written specifically for that: it fills the bounded constraints, so a `Panel`/`SizedBox` host with `vertical_align: Align.stretch` and `horizontal_align: Align.stretch` works.

---

## 4. Scene model

### 4.1 `ChartScene`

```cpp
struct ChartSlot
{
    size_t dataset_index;
    ChartKind kind;                 // XY or SMITH
    albor::Rect frame;              // chart area inside the panel
    albor::Rect plot;               // hit-test rect
    float chart_height;
};

struct ChartScene
{
    std::shared_ptr<const albor::PictureResource> base;
    std::shared_ptr<const albor::PictureResource> overlay;
    std::vector<ChartSlot> slots;
    float width;
    float height;
    ChartPalette palette;
};
```

The base picture holds everything that changes only with data, zoom, steps, size or theme. The overlay holds the zoom-selection rectangle and any transient crosshair; a pointer move rebuilds only the overlay.

### 4.2 `ChartSceneBuilder`

Input: a `std::vector<ChartFrame>` (one per chart), panel size and palette.
Output: one `PictureResource` per layer.

For each `ChartFrame`:

1. **Plot background** — `draw_rect` with `palette.panel` or the Smith padded rect.
2. **Plot border** — `draw_rect` with a stroke, or a `PathResource` for the Smith unit circle; Smith has no square border.
3. **Grid and ticks** — build `LineBatch` resources grouped by style:
   - major grid (1 px, `palette.grid`);
   - minor grid (0.5 px, `palette.grid` with `alpha * 0.35`);
   - tick marks (1 px, `palette.border`);
   - axis datums (1 px, `palette.border`).
   `LineBatch` is the low-overhead primitive for independent segments. A `draw_line_batch` call carries the stroke and a translation transform.
4. **Series** — for each `ChartSeriesFrame`, build a `PathResource` with `MoveTo`/`LineTo` per contiguous run. One `draw_path` per series run, stroked with the series colour and `CHART_LINE_WEIGHT` (or 1.7 px if visual parity with the Slint view is preferred). `PathResource` strokes with joins/caps and stays retained.
5. **Text** — for every visible label, reuse a cached `TextLayout` and emit `draw_text`:
   - x tick labels centred under `x_datum`;
   - y1 labels right-aligned at `plot_x - 6`;
   - y2/y3 labels left-aligned at their datum;
   - legend labels and Smith labels.
   No text shaping happens in `paint_content`.
6. **Legend** — `draw_rect` swatches and cached label layouts; geometry comes from `ChartFrame`.
7. **Smith extras** — `smith_grid` runs are `PathResource` contours; boundary runs use `palette.border`, grid runs use `palette.grid`; `smith_labels` are centred text. No axes or ticks.

Every `PathResource` and `LineBatch` is created through `ResourceFactory`; the `PictureResource` holds the shared pointers, so the resources live as long as the scene.

### 4.3 Text measurement cache

`ChartLayout` takes a `TextMeasurer`. The studio adapter:

```cpp
class AlborTextMeasurer
{
public:
    explicit AlborTextMeasurer(albor::TextEngine& engine);
    float operator()(const std::string& text);          // for ChartLayout
    const albor::TextLayout& layout(const std::string& text); // for drawing
private:
    std::unordered_map<std::string, albor::TextLayout> m_cache;
};
```

Key: text + font size (chart labels use `CHART_LABEL_FONT_SIZE`; legend labels the same). The cache is cleared on font/theme/DPI change. Both `ChartLayout::build` and the scene builder use the same cache, so a label is shaped once, not once for measurement and again for drawing.

---

## 5. Performance design

The target is a chart panel that stays interactive with several charts and thousands of points.

### 5.1 Retained base scene

- `paint_content` does **no** path construction, text shaping, string formatting or vector growth.
- It replays two `PictureResource`s. The backend replays the retained command list; the GPU caches path/line/text resources by id.
- A data change, zoom, step change, series change, theme change or settle-resize rebuilds the base. A hover or a drag rebuilds only the overlay.

### 5.2 Scene rebuild rules

| Event | Rebuild |
| --- | --- |
| Dataset activation / tab switch | base (all charts of the dataset) |
| Series add/remove, step selection | base |
| Zoom release | base |
| Drag move | overlay only |
| Hover move | no chart rebuild; status text only, debounced |
| Theme change | base |
| Resize drag | draw the old base picture scaled; no rebuild |
| Resize settle (50 ms) | base at the new size |

### 5.3 Damage

- `ChartsView::set_scene` calls `invalidate()`.
- `ChartsView::set_overlay` computes the union of the old and new overlay bounds and calls `invalidate(union)`.
- The base picture is one command; a damage region that intersects it replays the retained list, which is cheap. If profiling shows otherwise, split each chart into its own `PictureResource` and invalidate only the changed chart.
- Hover text goes to the status bar, so it never invalidates the chart.

### 5.4 Resize

- `arrange` stores the new bounds and starts a one-shot `TimerScheduler` rebuild (50 ms).
- Until the timer fires, `paint_content` draws the old base picture under a scale transform that maps the old panel size to the new bounds. Text may scale during the drag; the settle rebuild restores crisp text.
- This mirrors the established rule: no frame republish during drag-resize; one republish on resize-finished.

### 5.5 Data volume

- `ChartEngine` already decimates to `k_decimate_target = 4000`.
- A `PathResource` per contiguous run keeps geometry off the per-frame path.
- Smith grid sampling is fixed (`k_smith_grid_samples = 256`); the Smith grid is built once per theme/size, not per frame.
- Optional later optimisation: per-pixel decimation in `ChartLayout` if 4000 points still exceed the pixel budget for narrow charts. Do not add it until a benchmark shows a need.

### 5.6 Benchmarks

Add a `studio` benchmark or a unit test that times:

- `ChartLayout::build` + scene build for 1/4/10 charts, 1/4 series each, 4000 points per series;
- base picture replay (static);
- overlay rebuild (drag);
- a hover update (status text only).

Budgets (initial, measured on the development machine):

- base build: ≤ 2 ms per chart with 4 series × 4000 points;
- static frame: no allocation in `paint_content`;
- drag overlay: ≤ 0.5 ms;
- hover: ≤ 0.2 ms plus text interpolation.

If a budget is missed, profile before adding complexity.

---

## 6. Interactions

### 6.1 Hit testing

`ChartSlot` carries the panel-relative plot rect from the last built frame. `ChartsView` maps pointer coordinates to the slot and plot ratios. The presenter uses the same rects, so hit testing and layout cannot diverge.

### 6.2 Zoom drag

- Pointer down inside a plot: record the chart index and anchor; build the first overlay rect.
- Pointer move: clamp to the active plot; update the overlay picture; `invalidate` the union of old/new rect.
- Pointer up: if the rect is at least 10 px in both axes:
  - selected chart: `ChartEngine::update_zoom_window(x1, x2, y1, y2)`;
  - every other chart in the dataset: horizontal-only `update_zoom_window(x1, x2, -1, -1)`.
  - rebuild the base scene once.
- Escape/cancel: drop the overlay, no engine change.

Smith charts do not zoom; the gamma plane is fixed at ±1.

### 6.3 Hover

- Pointer move inside a plot: for XY, map the x ratio through `ChartEngine::plot_ratio_to_abscissa_value`; for Smith, map to gamma-r/gamma-i.
- Debounce 20 ms through `TimerScheduler`.
- XY: `ChartEngine::hovered_series_text(abscissa)`.
- Smith: `ChartEngine::hovered_smith_text(gamma_r, gamma_i)`.
- Publish the text to the shell status bar. The chart itself is not repainted.
- Leave: clear the status text.

### 6.4 Context menu

`ChartsView` raises a signal with the chart index and plot position. The presenter opens an Albor native menu with the legacy actions:

- Zoom to Fit, Autorange, Zoom Abscissa Extent;
- Add/Remove Plots, Delete All Plots;
- Calculate FFT, Step Tool;
- Add Chart, Delete Chart, New Window;
- chart reorder (drag handle later).

Smith datasets hide the cartesian-only actions. Debug builds expose hidden automation triggers, following the existing `tests_studio` context-menu pattern.

---

## 7. Datasets and plot tabs

The chart component is only useful with a dataset model. This is the `6.7a` shell:

- `PlotDataset` owns an `XyceOutputFile` (and its `ExpressionManager`/`StepInformation`), a title, a closable flag, and the per-dataset chart list.
- Opening an output file (`.raw`, `.prn`, `.csd`, `.csv`, `.dat`, Touchstone) routes to the dataset loader instead of the netlist editor.
- A finished simulation auto-loads its analysis output and companion files (`.FFT`, `.FOUR`, `.MEASURE`, `.PCE`) using the existing core parsers.
- The active dataset activates its chart engines and publishes a scene.
- Plot tabs: a tab strip at the top of the charts panel; selecting a tab activates its dataset; closing releases the dataset and its retained scenes.
- Suggested plots from `XyceOutputFile::suggested_plots()` create the initial charts; otherwise one empty chart is created.
- Smith datasets create `ChartEngine` instances with `ChartKind::SMITH`; the diagonal entries are the default plots.
- Fourier datasets have no plottable spectra; they show the Fourier results table instead of the chart canvas. This can be a follow-up if line/Smith is the first milestone.

The legacy `slint/src/ui/charts_renderer.cpp` contains the dataset/chart state machine and is the reference; port the logic, not the Slint types.

---

## 8. Main window integration

- Add a `Charts` toolbar action next to the netlist/output actions, enabled when a dataset is available.
- Add a body switch: netlist editor vs charts panel. The legacy app toggles `charts-visible`; the studio can use the same model or a `TabView`.
- The charts panel contains the plot-tab strip and the `ChartsView` host.
- Keep the simulation output panel and status bar unchanged.
- `ChartsPresenter` is created by `StudioApp` and wired to `MainWindowPresenter` for dataset loading and run completion.

---

## 9. Testing

### 9.1 Unit tests (`studio-tests/`)

- `ChartSceneBuilder` emits the expected command counts for a small XY frame (background, border, grid batches, one path per series run, labels, legend).
- A Smith frame emits the padded plot rect, grid paths, boundary paths, series paths and labels; no axes.
- Text layouts are cached: two labels with the same text produce one `TextLayout`.
- Zoom normalization maps drag rects to the correct ratios; other charts receive horizontal-only windows.
- Chart-slot hit testing maps points to the right chart and plot.
- Hover mapping delegates to `ChartEngine` and debounces.
- Resize scheduling: one rebuild after settle, no rebuild per drag event.

### 9.2 UI integration (`tests_studio/`)

- Open a netlist, run a simulation, switch to Charts, verify the panel and one chart.
- Open a fixture output file directly, verify the chart title/tab and the suggested series.
- Drag-zoom a chart and verify the other charts keep their vertical range while sharing the abscissa.
- Switch datasets and verify each keeps its own zoom/selection.
- Smith dataset: verify the chart is in Smith mode and the cartesian context actions are hidden.
- Debug-only triggers for context-menu actions; the automation cannot reach a native menu directly.

Because chart text is painted as pixels, expose a small debug-only automation surface on `ChartsView`/`ChartsPresenter`:

- chart count and per-chart kind;
- active dataset title;
- selected series names;
- last hover text;
- plot rects and current zoom window.

These are read through the existing automation property/element mechanism, not by scraping pixels.

### 9.3 Performance tests

The benchmark from §5.6 is a test or a separate executable; assert the allocation-free `paint_content` path with a `TestRenderContext` that counts draw calls and allocations where practical.

---

## 10. Phased delivery

### 6.7a — Dataset and shell

- `ChartsPresenter`, `PlotDataset`, output-file loading, run auto-load.
- Charts toolbar action, body switch, empty charts panel.
- Plot-tab strip and dataset activation.

### 6.7b — Static Line charts

- `AlborTextMeasurer`.
- `ChartSceneBuilder` for XY frames.
- `ChartsView` native component, C++ insertion, base picture.
- Static charts with suggested plots; resize settle.
- Unit tests and first UI scenario.

### 6.7c — Interactions

- Zoom drag + overlay picture.
- Hover readout.
- Context menu actions.
- Step selection, add/remove charts, chart reorder.
- Resize stretch + settle.

### 6.7d — Smith charts

- Smith `ChartFrame` painting (grid, boundary, labels, series).
- Smith hover readout.
- Hide cartesian-only actions.
- UI scenario.

### 6.7e — Performance hardening

- Benchmarks, allocation checks, damage tuning.
- Per-chart picture split only if the single base picture is too coarse.
- Optional background scene build if profiling justifies it.

### Follow-ups

- 6.4 FFT setup dialog, using the chart expressions.
- 6.6 Add Plot dialog.
- 6.8 native chart windows (`new_window`).
- Fourier results table parity.

---

## 11. Risks and open questions

| Risk | Mitigation |
| --- | --- |
| `.ui` codegen cannot instantiate native components (`E0501`) | Construct `ChartsView` in C++ and insert into a host `Panel`; keep the implementation independent of the host. Revisit if Albor adds codegen support. |
| `ChartsView::measure` does not fill a host panel | Prototype the host/measure pair first; use a bounded fill measure and `Align.stretch`. Add a layout test. |
| Chart text is not reachable by automation | Expose debug-only chart metadata properties and hidden triggers; do not rely on pixel scraping. |
| Retained picture replays too much on a small hover damage | Start with one base picture; split per chart if profiling shows the replay dominates. |
| Theme/DPI change invalidates text layouts | Clear the text cache and rebuild the base scene on theme/scheme/scale change. |
| Dataset loading is a larger slice than the painter | Keep 6.7a separate; the chart component can be tested against fixture datasets before the run path is wired. |
| Smith chart parity | `ChartLayout::build_smith` already provides geometry; validate labels and padding against the current Slint view. |
| Per-frame `PictureResource` rebuild from ImGui-style immediate data | Not applicable: the native path builds only on state changes. |

---

## 12. File map

```text
core/src/charts/                     existing ChartEngine, ChartLayout, SmithChart
studio/src/charts/
  chart_scene.h/.cpp                 ChartScene, ChartSlot, overlay
  chart_scene_builder.h/.cpp         ChartFrame → PictureResource
  chart_text_measurer.h/.cpp         Albor TextEngine → ChartLayout::TextMeasurer + cache
  charts_controller.h/.cpp           ChartEngine list, zoom, hover, steps
  plot_dataset.h/.cpp                dataset lifecycle
studio/src/presenters/
  charts_presenter.h/.cpp            datasets, tabs, scene publication
studio/src/views/charts/
  charts_view.h/.cpp                 native Albor component
  charts_panel.ui                    tab strip + host
studio/src/views/main_window/
  main_window.ui                     Charts action + body switch
  main_window_view.cpp               host insertion + scene wiring
studio-tests/
  chart_scene_builder.test.cpp
  charts_controller.test.cpp
tests_studio/
  charts_test.py
  charts_smith_test.py
```

## 13. Acceptance criteria

- Line charts render from every supported output-file type with the same geometry as `ChartLayout` produces today.
- Smith charts render with the gamma-plane grid, boundary, labels, series and hover readout.
- Zoom drag, hover, context actions and dataset switching match the legacy behaviour.
- `paint_content` performs no allocation, text shaping or path construction.
- A ten-chart panel with 4000-point series stays interactive (budgets in §5.6).
- Unit tests, UI scenarios and the existing formatter pass.
- No new runtime dependency, no SVG and no Skia.

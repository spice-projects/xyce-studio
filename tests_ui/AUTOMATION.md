# UI Automation Guide

How to drive Xyce Studio from the python UI integration tests in this folder.
Every recipe here is reverse engineered from a passing test in `tests_ui/`; the
referenced tests are the executable documentation.

## Overview

- The tests use the `slint_automation` package (repository root). See
  `slint_automation/design.md` for the framework design and
  `slint_automation/slint-mcp-api.md` for the raw MCP endpoint reference.
- Direct MCP use is not allowed in the scenarios: tests must drive the
  application exclusively through the public `slint_automation` API
  (`launch`/`TestSession`, locators, `expect` assertions and the `app` facade
  methods). Never build JSON-RPC requests, touch the MCP endpoint or reach the
  transport layer (`McpClient`, private client members) from a test body — if
  a needed interaction is missing from the framework, extend
  `slint_automation` first (with unit tests in `tests/slint_automation`), then
  use it from the scenario.
- The application under test is the debug build `.build-debug/xyce-studio`.
  Debug builds embed the automation MCP server and the hidden context-menu
  triggers; release builds expose neither.
- Run one module: `python3 -m unittest tests_ui.<module>` from the repository
  root, or run everything with
  `python3 -m unittest discover -s tests_ui -p "*_test.py"`.
- Scenarios that need simulation results require the `Xyce` executable on
  `PATH`; they call `shutil.which("Xyce")` and `self.skipTest(...)` when absent.

## Building blocks

### Sessions and launch

Every test wraps its application in a `TestSession`. The session closes the
application at the end and collects failure artifacts (screenshot, ui tree,
stdout/stderr) into `artifacts/<test id>/` when the test fails.

```python
with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
    ...
```

Launch variants (from `slint_automation.slint_application.launch`):

| Goal | Call |
| --- | --- |
| Plain start | `launch()` |
| Load a netlist | `launch(args=["--netlist", str(netlist)])` |
| With a configured Xyce executable | `launch(args=["--netlist", str(netlist), "--xyce", xyce])` |
| Extra application variables | `launch(env={"KICAD_API_TOKEN": "..."})` |
| Persistent configuration shared across launches | `launch(env={CONFIG_VARIABLE: config_root})` with `CONFIG_VARIABLE = "APPDATA" if sys.platform == "win32" else "XDG_CONFIG_HOME"` |

Launches are isolated by default: each run gets an ephemeral MCP port, a clean
environment and its own temporary configuration directory. Never mutate the
test fixtures: copy a netlist to a scratch directory before editing it.

### Locators

Locators are lazy: creating one does not touch the application; every action
and read re-resolves against the current element tree.

- `app.get_by_id("MainWindow::charts")` — qualified element id
  `ComponentName::element-id`. The element id is the Slint `element-id` (or the
  instance name) qualified with the declaring root component.
- `app.get_by_type("ToolbarButton")` — all elements whose primary Slint type
  matches, in document order. Combine with `.nth(index)` for positional
  access; declaration order in the `.slint` files is the contract (e.g. the
  nine toolbar tools from left to right).
- `app.get_by_role("Button")` — first element with the accessible role.
- `locator.child("Text")` — restricts the search to a subtree.

### Assertions and waiting

`expect(locator)` polls until the assertion holds or raises
`SlintAssertionError` with the last observed state. Always pass explicit
timeouts for operations that take time:

```python
expect(editor).to_have_text(netlist.read_text())
expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=15.0)
expect(save_icon).to_have_opacity(1.0)
app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
output.wait_for_gone()
```

For compound conditions use the application level poller:

```python
app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2,
                       timeout=5.0, message="expected a second chart")
```

Text of an element comes from `accessibleLabel` (or `accessibleValue`);
`locator.text()` returns `accessibleValue` falling back to `accessibleLabel`.
The MCP server omits empty fields, so an empty label reads back as `None`.

## Recipes

### Run a simulation (from `charts_test.py`, `simulation_run_test.py`)

1. Resolve `xyce = shutil.which("Xyce")` and skip the test when missing.
2. Launch with the netlist and the executable.
3. Click the run tool: `app.get_by_type("ToolbarButton").nth(5).click()`
   (toolbar index 5 is the run tool).
4. Wait for the status bar to report success:
   `expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=15.0)`
   with `status = app.get_by_id("MainWindow::statusbar").child("Text")`.
5. Wait for the charts view to open:
   `app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)`.

The transient tab then shows its charts (`app.get_by_type("ChartView")`).

### Switch plot tabs (from `charts_test.py`)

Plot tabs are `PlotTabButton` elements in tab order; after the simple
transient simulation there are three tabs (transient plus two fft tabs).

```python
tabs = app.get_by_type("PlotTabButton")
self.assertEqual(tabs.count(), 3)
tabs.nth(1).click()
app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, ...)
```

### Read chart axis labels (from `charts_test.py`)

Charts are `ChartView` elements stacked top to bottom; `nth(index)` addresses
them in vertical order. Every chart carries an accessible identity: plotted
charts report their first series name as `accessibleLabel` (for example
`FFT(I(C1))`), empty charts report `None`.

Axis tick labels are `Text` children of the `ChartView`; abscissa labels sit
in the band below the plot (relative y in the last 60 px of the view) and
ordinate labels above it. See `_chart_axis_labels` and `_abscissa_tick_labels`
in `charts_test.py` for the grouping and the si-prefix value parser
(`_label_value`) used to assert numeric ranges.

### Zoom a chart by drag (from `charts_test.py`)

Dragging inside a chart view performs the zoom selection:

```python
app.get_by_type("ChartView").nth(0).drag(660.0, 310.0)
```

A zoom applied to one chart applies the horizontal range to all charts and the
vertical range only to the dragged chart; assert it by comparing axis labels
before and after.

### Context menu actions via the debug triggers (from `charts_context_test.py`)

The charts context menu is rendered by the platform (NSMenu on macOS) and is
therefore invisible to the automation. Debug builds expose hidden zero-size
trigger elements that call exactly what the matching menu item calls; each is
addressable by id `ChartsPanel::test-<action>` and takes the target chart as a
relative position `[0..1]` of the vertical stack (the renderer translates it
to an index the same way the right-click position would):

```python
app.invoke_chart_action("add-chart", chart_position=0.25)     # insert after the first chart
app.invoke_chart_action("delete-chart", chart_position=0.25)  # remove the first chart
app.invoke_chart_action("autorange", chart_position=0.25)
app.invoke_chart_action("zoom-to-fit", chart_position=0.75)
```

Available actions mirror the context menu one to one: `zoom-to-fit`,
`autorange`, `zoom-abscissa-extent`, `add-remove-plots`, `delete-all-plots`,
`calculate-fft`, `step-tool`, `add-chart`, `delete-chart`, `new-window`
(declaration in `src/ui/components/charts_panel.slint`). The triggers only
exist while the charts panel is shown and only in debug builds.

To assert stack order, read the per-chart identity:

```python
labels = [app.client().get_element_properties(handle).get("accessibleLabel")
          for handle in app.client().find_by_type("ChartView")]
self.assertEqual(labels, ["FFT(I(C1))", None, "FFT(I(L1))"])
```

An inserted empty chart lands as `None` between the two plotted charts, which
is how insertion order is verified.

### Assert toolbar states (from `toolbar_test.py`)

The toolbar exposes nine `ToolbarButton` tools in declaration order (left to
right: open, save, netlist, charts, output, run, configure simulation,
configure plugin, exit). Disabled tools render their icon dimmed, so state is
asserted through the icon opacity of the tool:

```python
tools = app.get_by_type("ToolbarButton")
icon = tools.nth(1).child("Image")
expect(icon).to_have_opacity(1.0)  # enabled
expect(icon).to_have_opacity(0.4)  # disabled
```

### Edit the netlist and save (from `netlist_editor_test.py`)

```python
editor = app.get_by_id("NetlistEditor::input")
editor.click()
editor.press("x")                      # single key edit marks the file dirty
expect(tools.nth(1).child("Image")).to_have_opacity(1.0)
tools.nth(1).click()                   # save from the toolbar
```

### Drive dialogs (from `simulation_parameters_test.py`, `plugin_config_test.py`)

Dialogs are inline modal panels inside the main window. Text fields are
`LineEdit` elements; footer buttons are found by their accessible label within
the window root because the generic dialog layout places them in document
order:

```python
root = app.client().get_window_properties()["rootElementHandle"]
ok = [handle for handle in app.client().find_by_type_in(root, "Button")
      if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
app.client().click_element(ok)
fields.nth(0).wait_for_gone()          # dialog closed
```

- Fill text fields with `field.fill("25m")`, verify with `expect(field).to_have_text("25m")`.
- Dialog tabs are `TabButton` elements inside the window root, clicked by index.
- `Escape` dismissal: `field.press("\x1b")` — key text must be the control
  character, not the name (`"\x1b"` for Escape, `"\n"` for Return, `"\t"` for Tab).
- A rejected accept keeps the dialog open; wait for the footer button to still
  exist and for the validation error text to appear.

### Configuration persistence across launches (from `plugin_config_test.py`)

Point `XDG_CONFIG_HOME` (or `APPDATA` on Windows) at a temporary directory and
launch twice with the same root; the second launch must see the first run's
persisted state. Without the override, each launch is fully isolated.

### Show and close the simulation output (from `simulation_run_test.py`)

Toolbar index 4 toggles the output panel (`MainWindow::output`); the close
button is `SimulationOutputPanel::close-button`. Log lines are `Text`
elements inside the panel; look for the `Welcome to the Xyce` banner. The
panel retains its content after close/reopen.

### Exit the application (from `simulation_run_test.py`)

```python
app.get_by_type("ToolbarButton").nth(8).click()
app.wait_for_condition(lambda: not app.is_running(), timeout=10.0, message="expected the application process to exit")
```

## Conventions

- Every scenario starts with `# arrange`, uses `# step` for each interaction
  and `# assert` for each verification.
- Test methods must be self-contained: no helper functions and no shared
  utilities between tests; test doubles (fake processes, fake MCP clients) are
  the only shared objects, mirroring the framework unit tests in
  `tests/slint_automation`.
- Skip fixtures instead of failing: `self.skipTest("Xyce executable not found")`.
- Never cache element handles across steps; resolve, act, then resolve again.
- Wait with explicit timeouts instead of sleeping; `expect(...)`/`wait_for_*`
  poll automatically.
- Run the tests with the repository virtual environment: `.venv/bin/python -m
  unittest discover -s tests_ui -p "*_test.py"`.
- Tests must not perform any real network I/O; the MCP transport against the
  embedded local server is the only allowed endpoint, and the python unit
  tests in `tests/slint_automation` use in-process fakes exclusively.

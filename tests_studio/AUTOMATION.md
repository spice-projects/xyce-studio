# UI Automation Guide (Albor studio frontend)

How to drive the Albor-based studio frontend from the python UI integration
tests in this folder. Every recipe here is reverse engineered from a passing
test in `tests_studio/`; the referenced tests are the executable documentation.

## Overview

- The tests use the `albor_automation` package (repository root). The scenarios
  mirror the test-facing API of `slint_automation` (the Slint frontend's
  framework) but do not abstract over both toolkits: the transport, handshake,
  framing and tool surface differ fundamentally between the two.
- Direct protocol use is not allowed in the scenarios: tests must drive the
  application exclusively through the public `albor_automation` API
  (`launch`/`TestSession`, `get_by_id` locators, `expect` assertions and the
  `app` facade methods). Never build JSON-RPC requests, touch the socket
  endpoint or reach the transport layer (`McpClient`, private client members)
  from a test body — if a needed interaction is missing from the framework,
  extend `albor_automation` first (with unit tests in
  `tests/albor_automation`), then use it from the scenario.
- The application under test is the debug build
  `.build-studio-debug/xyce-studio-next` (the `studio-debug` CMake preset,
  built with `ALBOR_DEV=ON` and `ALBOR_AUTOMATION=ON` against the local Albor
  checkout). Release builds do not embed the automation host.
- Run one module: `.venv/bin/python -m unittest tests_studio.<module>` from
  the repository root, or run everything with
  `.venv/bin/python -m unittest discover -s tests_studio -p "*_test.py"`.

## Building blocks

### Sessions and launch

Every test is an `AlborTestCase` and starts its application through
`start_session`. The session finishes when the test ends — through the
unittest cleanup hook, which runs even when the test method failed — closing
the application and collecting failure artifacts (screenshot, tree dump,
descriptor, stdout/stderr) into `artifacts/<test id>/` when the test failed.

```python
class PluginConfigDialogChecks(AlborTestCase):
    def setUp(self) -> None:
        self.start_session(launch(args=["--xyce", "/bin/echo"]))
        self.app = self._app
```

Launch variants (from `albor_automation.albor_application.launch`):

| Goal | Call |
| --- | --- |
| Plain start | `launch()` |
| With a configured Xyce executable | `launch(args=["--xyce", "/bin/echo"])` |
| Extra application variables | `launch(env={"XDG_CONFIG_HOME": config_root})` |

- Launches are isolated by default: each run gets its own ephemeral socket,
  token and a clean temporary configuration directory. `config_dir=False`
  keeps the developer configuration in play.
- The executable resolves from the `ALBOR_TEST_APPLICATION` environment
  variable when set, otherwise from the studio debug build.
- `ALBOR_TEST_ARTIFACTS` redirects the failure artifacts root (the session
  default is `artifacts/`).
- Set `ALBOR_AUTOMATION_DEBUG=1` to mirror the debug protocol log to stderr.

### Locators

Locators are lazy: creating one does not touch the application; every action
and read re-resolves against the live tree.

- `app.get_by_id("plugin_config_path")` — semantic id, the only selector the
  albor tree offers. Ids come from the `id:` attribute in the studio `.ui`
  files (`studio/src/views/**.ui`), declared once and stable across rebuilds
  — unlike the Slint frontend there is no ordinal/type/role selection.
- `locator.exists()` / `wait_for_exists(timeout=...)` / `wait_for_gone(...)`
  — presence polling, which is how tests sync with the event loop.
- `locator.text()` reads the node name (the announced label),
  `locator.value()` reads the editable value, `locator.writable()` reports
  whether the node accepts a `fill`.

A locator that resolves through a transport hiccup reads as absent rather
than raising, so polling assertions survive a slow frame.

### Assertions and waiting

`expect(locator)` polls until the assertion holds or raises
`AlborAssertionError` (an `AssertionError`, so the runner reports a failure)
naming the node and the last observed state:

```python
expect(field).to_have_value("/bin/echo")
expect(error).to_have_name("Selected path is not an executable file")
expect(app.get_by_id("plugin_config_error")).not_().to_exist()
```

Available assertions: `to_exist`, `to_have_name`, `to_have_value`,
`to_be_writable`, `to_have_state(flag)` (tree flags such as `disabled`,
`focused`, `selected`, `read_only`, `multiline`, `expanded`), each accepting
`timeout=` and `poll_interval=` keywords. `not_()` flips the polarity.

For compound conditions use the application-level poller:

```python
app.wait_for_condition(lambda: app.is_running() is False, timeout=10.0, message="expected the application process to exit")
```

## Recipes

### Load and edit a netlist (from `netlist_editor_test.py`)

The shell always shows the netlist editor; `--netlist` opens a file through
the same dispatch the toolbar Open action uses. Work on a scratch copy so the
scenario never mutates the fixture, and assert the state through the save
tool's `disabled` flag, which the presenter refreshes with the dirty state:

```python
netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
with tempfile.TemporaryDirectory() as scratch:
    working_copy = Path(scratch) / "tran-simple-01.cir"
    shutil.copy(netlist, working_copy)
    self.start_session(launch(args=["--netlist", str(working_copy)]))
    app = self._app
    editor = app.get_by_id("netlist_editor")
    editor.wait_for_exists()
    save = app.get_by_id("main_save")
    expect(editor).to_have_value(netlist.read_text())
    expect(save).to_have_state("disabled")
    edited = netlist.read_text() + "* typed by the scenario\n"
    editor.fill(edited)
    expect(save).not_().to_have_state("disabled")
    save.click()
    app.wait_for_condition(lambda: working_copy.read_text() == edited, timeout=5.0, message="expected the save to write the edited netlist")
    expect(save).to_have_state("disabled")
```

- `editor.fill(...)` replaces the whole value through the edit path, so it
  raises the shell's edit signal exactly like a keystroke — the dirty marker
  follows.
- The save lands inside the click handler, so poll the file on disk with
  `app.wait_for_condition` instead of reading it once.
- An untitled editor (launched without `--netlist`) starts empty and clean;
  its save flow runs the native save panel, which automation cannot drive —
  that decision logic is covered by the presenter unit tests in
  `studio-tests/main_window_presenter.test.cpp` instead.
- The open panel is native too; scenarios reach the open dispatch through
  `--netlist`, which the presenter treats identically.
- Semantic ids of the shell (from `studio/src/views/main_window/main_window.ui`):
  `main_open`, `main_save`, `main_plugin_settings` (toolbar), `netlist_editor`
  (multiline text field), `main_status` (status row).

### Open the plugin configuration dialog (from `plugin_config_test.py`)

The dialog opens from the main window toolbar button; the field resolves
against the dialog's semantic ids once the dialog is up:

```python
app.get_by_id("main_plugin_settings").click()
field = app.get_by_id("plugin_config_path")
field.wait_for_exists()
expect(field).to_have_value("/bin/echo")
```

Semantic ids of the dialog (from `studio/src/views/plugin_config/plugin_config_dialog.ui`):
`plugin_config_path` (text box), `plugin_config_browse`, `plugin_config_error`
(validation message, absent until validation fails), `plugin_config_cancel`,
`plugin_config_ok`.

### Edit a text field and persist the value across launches (from `plugin_config_test.py`)

Configuration persistence is a two-launch scenario over one configuration
root: point `XDG_CONFIG_HOME` at a temporary directory and pass it to both
launches; the second launch must see the first run's persisted state.

```python
with tempfile.TemporaryDirectory() as config_root:
    self.start_session(launch(env={"XDG_CONFIG_HOME": config_root}))
    app = self._app
    app.get_by_id("main_plugin_settings").click()
    field = app.get_by_id("plugin_config_path")
    field.wait_for_exists()
    field.fill("/bin/echo")
    app.get_by_id("plugin_config_ok").click()
    field.wait_for_gone()                  # dialog closed after acceptance
    app.close()
    # relaunch sharing the same configuration root
    self.start_session(launch(env={"XDG_CONFIG_HOME": config_root}))
    app = self._app
    app.get_by_id("main_plugin_settings").click()
    field = app.get_by_id("plugin_config_path")
    field.wait_for_exists()
    expect(field).to_have_value("/bin/echo")
```

Without the override each launch is fully isolated, so the dialog shows the
path from the launch arguments (`--xyce`) instead of persisted state.

### Assert a validation error without losing the dialog (from `plugin_config_test.py`)

A rejected accept keeps the dialog open; the error label appears only after
the refusal, so wait for it and assert its name:

```python
field.fill("/nonexistent/xyce")
app.get_by_id("plugin_config_ok").click()
error = app.get_by_id("plugin_config_error")
error.wait_for_exists()
expect(error).to_have_name("Selected path is not an executable file")
self.assertTrue(app.get_by_id("plugin_config_ok").exists())  # dialog stays open
```

### Cancel reverts the edited value (from `plugin_config_test.py`)

Drive to the failing state first, then cancel, then reopen and compare
against the configured value:

```python
field.fill("/nonexistent/xyce")
app.get_by_id("plugin_config_ok").click()
app.get_by_id("plugin_config_error").wait_for_exists()
app.get_by_id("plugin_config_cancel").click()
field.wait_for_gone()                      # cancel closes the dialog
app.get_by_id("main_plugin_settings").click()
field = app.get_by_id("plugin_config_path")
field.wait_for_exists()
expect(field).to_have_value("/bin/echo")   # the rejected value was discarded
expect(app.get_by_id("plugin_config_error")).not_().to_exist()
```

## Conventions

- Every scenario starts with `# arrange`, uses `# step` for each interaction
  and `# assert` for each verification.
- Test methods must be self-contained: no helper functions and no shared
  utilities between tests; test doubles (fake processes, fake transports) are
  the only shared objects, mirroring the framework unit tests in
  `tests/albor_automation`.
- Wait with explicit timeouts instead of sleeping; `expect(...)`,
  `wait_for_exists`/`wait_for_gone` and `wait_for_condition` poll
  automatically.
- Never cache element handles across steps; resolve, act, then resolve again.
- Framework unit tests live in `tests/albor_automation` and run in-process
  with fakes exclusively; run them with
  `.venv/bin/python -m unittest discover -s tests/albor_automation -p "*_test.py"`.
- Tests must not perform any real network I/O; the unix-socket transport
  against the embedded local automation host is the only allowed endpoint.

## Failure artifacts

When a scenario fails, the session captures before closing the application:

- `screenshot.png` — the window image (best effort, never masks the failure)
- `tree.txt` — the raw semantic tree dump
- `descriptor.json` — the automation host descriptor (socket/token paths)
- `stdout.txt` / `stderr.txt` — the captured process output, which includes
  the `albor automation: serving <path>/host.json` line

The artifacts land in `$ALBOR_TEST_ARTIFACTS/<test id>` (default
`artifacts/`), one directory per test method.

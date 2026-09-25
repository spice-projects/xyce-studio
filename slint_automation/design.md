# Slint MCP Integration Testing Framework

## 1. Objective

Build a lightweight, independent integration-testing framework for a C++ desktop application built with Slint.

The framework must use the **MCP server embedded in the Slint application** as the UI automation interface.

The goal is to allow integration tests to launch the real application, interact with its real Slint UI, inspect UI state, and make assertions.

The framework should provide a simple, Playwright-inspired API without depending on Playwright or Slint's commercial `slint_testing` package.

Example target usage:

```python
class OpenProjectChecks(SlintTestCase):

    def setUp(self) -> None:
        # arrange: initialize the base test case hooks
        super().setUp()
        # arrange: launch the real application with a dynamic mcp port
        self._app = launch(executable)

    def test_open_project(self) -> None:
        # act
        self._app.get_by_id("open-project").click()
        self._app.get_by_id("filename").fill("test-project.kicad_pro")
        self._app.get_by_id("open").click()
        # assert
        expect(self._app.get_by_id("project-name")).to_have_text("test-project")
```

The first implementation should prioritize correctness, simplicity, and a clean abstraction boundary over feature completeness.

---

# 2. Important constraints

## 2.1 Do not use Slint `slint_testing`

Do NOT depend on:

* `slint_testing`
* Slint's commercial GUI Test Framework
* any commercial Slint testing package

The framework must communicate with the application's embedded MCP server directly.

## 2.2 Do not use Selenium or Playwright

Do not make Playwright or Selenium dependencies of the framework.

The API may be inspired by Playwright's Locator/Expect model, but the implementation must be independent.

A future Playwright adapter may be considered, but it is explicitly out of scope for the initial implementation.

## 2.3 Do not implement an MCP server

The application already hosts the MCP server.

The framework is an **MCP client**.

Architecture:

```
Python test
    |
    v
Test Framework
    |
    v
MCP client
    |
    | Streamable HTTP / JSON-RPC
    v
Slint MCP server
    |
    v
Real C++/Slint application
```

## 2.4 Do not guess the Slint MCP API

Before implementing the MCP client:

1. Inspect the version of Slint used by the target application.
2. Inspect the actual Slint MCP server implementation/source.
3. Determine:

   * MCP endpoint behavior
   * initialization sequence
   * protocol version
   * available tools
   * tool names
   * tool arguments
   * tool return values
   * element identification
   * element handles
   * actions
   * property inspection
   * screenshots
   * error behavior
4. Implement against the actual API.

Do not invent MCP tool names or schemas based on examples from other versions.

The framework should isolate all Slint/MCP-specific assumptions in one layer.

**Status: complete.** The installed Slint version (`v1.18.1`, verified live) is documented in
`slint_automation/slint-mcp-api.md`. Key findings that adjust this document:

* transport is plain JSON-RPC 2.0 over HTTP POST `/mcp` — no sessions, no SSE, no batch;
* `tools/call` failures come back as `isError: true` results, never as JSON-RPC errors;
* element identification uses Slint **element IDs** (`ComponentName::element-id`), not the
  `accessible-id` property (see §10);
* handles are ephemeral per query and must be re-resolved before every action (see §12).

---

# 3. Current Slint assumptions

The current Slint documentation states:

* the embedded MCP server is enabled using the `mcp` feature;
* when `SLINT_MCP_PORT` is set at runtime, the application starts an HTTP server implementing MCP Streamable HTTP;
* MCP clients can inspect and interact with the running UI;
* the MCP functionality is intended as a developer/debug feature and should not be enabled in production builds.

The implementation must verify these details against the exact Slint version used by the application rather than assuming the current documentation exactly matches the installed version.

The application is C++ and uses Slint.

The test framework should therefore treat the application as an external executable.

---

# 4. Repository structure

Create a dedicated testing framework package, preferably under a structure similar to:

```text
tests/
    framework/
        __init__.py
        mcp_client.py
        slint_app.py
        locator.py
        assertions.py
        errors.py
        process.py
        artifacts.py

    conftest.py

    integration/
        test_smoke.py
        ...
```

The exact location may be adapted to the existing repository structure.

Do not reorganize unrelated application code.

---

# 5. Architecture

The framework consists of five primary layers.

```text
                    unittest
                      |
                      v
                  SlintApp
                      |
              +-------+-------+
              |               |
              v               v
        Locator/Element   Artifacts
              |
              v
          MCPClient
              |
              v
        Slint MCP Server
              |
              v
        C++ Application
```

## Layer 1: MCPClient

Responsible exclusively for communication with MCP.

It must not know anything about Slint UI concepts.

Responsibilities:

* establish HTTP connection;
* perform MCP initialization;
* maintain JSON-RPC request IDs;
* send requests;
* invoke MCP tools;
* parse responses;
* detect MCP errors;
* expose raw tool results to higher layers.

It should NOT contain methods such as:

```text
click_button()
find_element()
get_text()
```

Those belong to higher layers.

Suggested API:

```python
class McpClient:
    def __init__(self, endpoint: str):
        ...

    def connect(self) -> None:
        ...

    def initialize(self) -> dict:
        ...

    def call(self, method: str, params: dict | None = None) -> dict:
        ...

    def call_tool(
        self,
        name: str,
        arguments: dict | None = None,
    ) -> dict:
        ...

    def close(self) -> None:
        ...
```

The exact implementation must match the actual MCP transport used by the Slint version under test.

---

# 6. SlintApp

**Implemented as `SlintApplication`** (`framework/slint_application.py`) with a
module-level `launch(executable_path, *, startup_timeout=...)` factory; the
name below was the planning name.

`SlintApp` is the main public object used by tests.

It represents a running instance of the application.

Suggested API:

```python
class SlintApp:
    @classmethod
    def launch(
        cls,
        executable: str,
        *,
        args: list[str] | None = None,
        env: dict[str, str] | None = None,
        cwd: str | None = None,
        startup_timeout: float = 10.0,
    ) -> "SlintApp":
        ...

    def get_by_id(self, element_id: str) -> "Locator":
        ...

    def get_by_role(
        self,
        role: str,
        *,
        name: str | None = None,
    ) -> "Locator":
        ...

    def screenshot(self, path: str) -> None:
        ...

    def close(self) -> None:
        ...
```

The exact API can evolve.

The important principle is that tests interact with `SlintApp`, not `McpClient`.

Example:

```python
app.get_by_id("run-button").click()
```

not:

```python
app.mcp.call_tool(...)
```

---

# 7. Application process management

`launch()` (in `framework/slint_application.py`) must:

1. Start the application as a subprocess.
2. Allocate an available TCP port.
3. Set `SLINT_MCP_PORT` for the child process.
4. Start the application in a **controlled environment**: the child process
   never inherits the test process environment wholesale (editors and shells
   inject `.env` variables that can change application behavior); only
   essential system variables (`PATH`, `HOME`, `TMPDIR`, ...) pass through,
   and application specific variables (e.g. simulating the KiCad plugin with
   `KICAD_API_*`) must be injected explicitly through
   `launch(env={...})`.
5. Start the process.
6. Wait until the MCP server is available.
7. Initialize the MCP connection.
8. Return a ready `SlintApplication`.

Do not use a fixed port such as `8080`.

Parallel tests must be possible eventually.

The framework should therefore allocate a free port dynamically.

Pseudo-flow:

```text
allocate_port()
       |
       v
build environment
       |
       +-- SLINT_MCP_PORT=<port>
       |
       v
spawn application
       |
       v
wait for MCP endpoint
       |
       v
MCP initialize
        |
        v
SlintApp ready
```

## 7.1 Port allocation and parallel safety

Allocating a free port by binding a socket to port `0`, closing it, and
reusing the port has a small race window: another process may claim the port
between allocation and application startup. To keep this from causing
flakiness:

1. Bind a listening socket to `127.0.0.1:0` and record the port.
2. Close the socket immediately before spawning the application.
3. Start the application with `SLINT_MCP_PORT=<port>`.
4. If the MCP endpoint does not become reachable within a short grace period,
   treat "port stolen by another process" as a retryable startup failure,
   allocate a new port, and retry with a bounded number of attempts.

Parallel test execution requirements:

* every test launches its own application process with its own port;
* no state may be shared between application instances;
* artifact and temporary directories must be unique per test;
* tests must not depend on execution order.

The initial implementation does not have to enable parallel execution, but no
part of the design may preclude it (see §48).

---

# 8. Process lifecycle

The application process must be owned by `SlintApplication`.

Use a context manager:

```python
with launch("./xyce-studio") as app:
    ...
```

On normal exit:

```text
terminate application
wait
```

On failure:

```text
capture artifacts
terminate application
wait
```

If graceful termination fails, use a stronger termination mechanism.

The implementation must not leave orphan application processes.

This is especially important on:

* macOS
* Windows
* Linux

because the application is a GUI executable rather than a normal command-line process.

---

# 9. Locator

Implement a Playwright-inspired but framework-specific `Locator`.

A locator represents a way to identify a UI element.

Example:

```python
button = app.get_by_id("run-button")
```

The locator should preferably resolve the element lazily.

Suggested API:

```python
class Locator:
    def click(self) -> None:
        ...

    def fill(self, text: str) -> None:
        ...

    def type(self, text: str) -> None:
        ...

    def press(self, key: str) -> None:
        ...

    def text(self) -> str:
        ...

    def property(self, name: str):
        ...

    def exists(self) -> bool:
        ...

    def screenshot(self, path: str) -> None:
        ...
```

Do not implement all methods if the Slint MCP server does not support the corresponding operation.

First determine the available MCP tools.

---

# 10. Element identification

Locators resolve elements through two mechanisms, in order of preference:

1. `app.get_by_role("Button")` — accessible-role queries through the MCP
   `query_element_descendants` tool (first-match semantics); requires no UI
   changes and must be the default choice for tests;
2. `app.get_by_id("some-element-id")` — Slint element IDs (`match_id` query,
   qualified as `ComponentName::element-id`); the **last resort**, used only
   when the id already exists for application requirements — never add an id
   purely for testing, since unused ids can trigger compilation warnings and
   the build must stay 100% clean.

The `accessible-label` / `accessible-value` properties expose an element's *text content*
(read back via `get_element_properties`), which is what text assertions check.

Use the element-ID mechanism; see `slint_automation/slint-mcp-api.md` §6 for the full mapping
between framework concepts and MCP tools.

Do not require tests to locate elements based on screen coordinates.

Do not use screenshots or OCR for normal UI element identification.

Coordinates should only be considered later for cases where semantic element interaction is impossible.

---

# 11. Locator resolution

A locator should not necessarily resolve when it is created.

This:

```python
button = app.get_by_id("run-button")
```

should create a locator.

This:

```python
button.click()
```

should resolve the locator and perform the action.

This allows the application UI to change between creation and use.

It also allows future retry/wait behavior.

---

# 12. Element handles

The Slint MCP server may return internal element handles.

Those handles must be hidden from test code.

Bad:

```python
handle = app.find(...)
app.click(handle)
```

Good:

```python
app.get_by_id("run-button").click()
```

The framework owns the mapping:

```text
Locator
   |
   v
resolve element
   |
   v
Slint element handle
   |
   v
MCP operation
```

If handles expire or become invalid because the UI changes, the framework should re-resolve the locator.

---

# 13. Assertions

Create an `expect()` API.

Example:

```python
expect(app.get_by_id("status")).to_have_text("Simulation complete")
```

Suggested initial API:

```python
def expect(locator: Locator) -> Expect:
    ...


class Expect:
    def to_have_text(self, expected: str) -> None:
        ...

    def to_have_property(self, name: str, expected) -> None:
        ...

    def to_be_visible(self) -> None:
        ...

    def to_be_enabled(self) -> None:
        ...

    def to_exist(self) -> None:
        ...
```

Also support negated assertions:

```python
expect(locator).not_to_be_visible()
```

or:

```python
expect(locator).not_to_have_text("Error")
```

Use whichever design results in the cleanest API.

---

# 14. Assertions must support waiting

Integration tests frequently fail because the test checks state before asynchronous UI/application work has completed.

Therefore assertions should eventually support polling.

Example:

```python
expect(app.get_by_id("status")).to_have_text("Simulation complete", timeout=10.0)
```

Initial implementation can use a framework-wide default timeout:

```python
DEFAULT_ASSERTION_TIMEOUT = 5.0
```

The assertion should:

1. Resolve locator.
2. Query current state.
3. Check condition.
4. If false, sleep briefly.
5. Retry.
6. Fail only after timeout.

Do not use excessively aggressive polling.

Make polling interval configurable.

## 14.1 Explicit waits

Assertion polling covers the most common case, but some flows need to wait for
a condition that is not a simple element state check. Provide a small set of
explicit wait utilities on `SlintApp` and `Locator`:

```python
app.wait_for_condition(
    lambda: ...,
    timeout=5.0,
    poll_interval=0.1,
    message="charts panel became visible",
)

locator.wait_for_exists(timeout=5.0)
locator.wait_for_gone(timeout=5.0)
```

Rules:

* waits must always have a timeout; no unbounded waits;
* wait failures must raise the same error types used by assertions, with the
  last observed state included in the message;
* do not add implicit global sleeps or waits between actions; waiting belongs
  in assertions and explicit wait calls only.

---

# 15. Screenshots

Screenshots are important for debugging integration failures.

Provide:

```python
app.screenshot("artifacts/failure.png")
```

and, if supported:

```python
app.get_by_id("main-window").screenshot(
    "artifacts/window.png"
)
```

The Slint MCP server already provides screenshot functionality in supported configurations; determine the exact tool and response format before implementation.

Do not implement screenshot capture independently using OS-level screenshot tools unless the MCP interface cannot provide it.

---

# 16. Failure artifacts

When an integration test fails, collect as much useful diagnostic information as practical.

At minimum:

```text
artifacts/
    <test-name>/
        screenshot.png
        stdout.txt
        stderr.txt
```

If the MCP API makes UI-tree information available, also capture:

```text
ui-tree.json
```

If possible, capture:

```text
mcp-response.json
```

for the failing operation.

The framework should make artifact collection automatic through unittest integration where practical.

---

# 17. Test runner integration

The framework core is **runner agnostic**: no module under `framework/` may
import `unittest`, `pytest`, or any other test runner. An application under
test chooses its own runner (unittest, pytest, plain scripts) and must get
identical behavior from the framework.

The runner-neutral building block is `TestSession`
(`framework/session.py`): it owns the application instance, names the
failure-artifact scope, and closes the application on `finish(failed)`. It
also works as a context manager that collects artifacts and closes whenever
the wrapped block raises. **Integration test classes must not inherit
framework classes** — they derive from the runner's own test class
(`unittest.TestCase`) and compose with `TestSession` per test:

```python
class ToolbarInitialChecks(unittest.TestCase):

    def test_toolbar_state(self) -> None:
        # arrange: launch the application in a session scoped to this test
        with TestSession(launch(), self.id()) as app:
            # assert
            expect(app.get_by_type("ToolbarButton")).nth(0).exists()
```

`launch()` resolves the executable from the `SLINT_TEST_APPLICATION`
environment variable and falls back to the repository debug build.

`SlintTestCase` (`framework/test_case.py`) remains an **optional unittest
adapter** over `TestSession` for suites that prefer `setUp`-based launching;
it maps unittest failure detection onto `session.finish(failed)`. It must
stay thin; all behavior lives in the neutral core.

In this repository, the framework's own suite and the integration tests use
unittest (per `STYLE-GUIDE.md`). Applications embedding the framework with
pytest wire `TestSession` through their own fixtures/hooks.

Test discovery and execution must run with the repository root as the
working directory (or the root on `PYTHONPATH`) so the top-level packages
resolve: `slint_automation` (the framework), `tests` (framework unit tests),
and `tests_ui` (integration tests). The canonical command is
`python3 -m unittest discover -v -s . -p "*_test.py"` from the repository
root; VS Code discovery is configured to the same root.

The reference template is `tests_ui/smoke_test.py`;
new integration suites must follow it.

Do not put application-specific behavior into the generic base class.

Application-specific base classes can be added later.

Each test must launch a fresh application instance and isolate any
persistent application state; see §46.

---

# 18. Test configuration

Do not hardcode the application executable.

Support configuration through:

1. environment variables;
2. a configuration file.

For example:

```text
SLINT_TEST_APPLICATION=/path/to/MyApplication
```

Tests are discovered with the standard unittest runner:

```text
python -m unittest discover -s tests -p "*_test.py"
```

The exact mechanism should follow the repository's existing testing conventions.

---

# 19. Configuration object

Avoid scattering configuration throughout the code.

Create a configuration object such as:

```python
@dataclass
class TestConfig:
    executable: str
    startup_timeout: float = 10.0
    assertion_timeout: float = 5.0
    poll_interval: float = 0.1
    artifact_directory: Path = ...
```

Additional options can be added later.

---

# 20. Error handling

Define framework-specific exceptions.

At minimum:

```python
class SlintTestError(Exception):
    pass


class ApplicationStartupError(SlintTestError):
    pass


class McpError(SlintTestError):
    pass


class LocatorError(SlintTestError):
    pass


class SlintAssertionError(AssertionError):
    pass
```

Do not shadow Python's built-in `AssertionError` unless there is a strong reason.

`SlintAssertionError` must derive from the built-in `AssertionError` so test
runners report expectation failures as test failures, not as errors with a
stack trace.

Prefer names such as:

```python
SlintAssertionError
```

if a custom assertion exception is required.

Error messages must contain useful context.

Assertion failure messages must lead with the failed expectation itself
(what was expected), followed by the last observed state and the waited
time, never with the polling mechanics ("timed out waiting ...").

Reported tracebacks for expectation failures must stop at the test frame:
every public entry point that polls (`expect` methods, locator waits,
`wait_for_condition`) trims the framework frames from the raised
`SlintAssertionError` so the reported traceback points at the failing test
line only.

Example:

```text
Timed out waiting for element 'simulation-status'
to have text:

Expected:
    "Simulation complete"

Last observed:
    "Running simulation..."

Timeout:
    5.0 seconds
```

## 20.1 MCP error mapping

`McpClient` must translate transport- and protocol-level failures into the
exceptions above. Map at least:

| Condition | Exception |
| --- | --- |
| HTTP connection refused / timeout during startup | `ApplicationStartupError` |
| HTTP failure on an established connection | `McpError` |
| JSON-RPC error response from a tool call | `McpError` with code and message |
| Malformed / non-JSON response | `McpError` with raw payload attached |
| Element not found during resolution | `LocatorError` |
| Ambiguous element match | `LocatorError` |
| Assertion condition not met within timeout | `SlintAssertionError` |

`McpError` should carry structured context as attributes and in its string
representation: the method/tool name, the request arguments, the response
body, and the HTTP status if applicable.

## 20.2 Transient failures and retries

Some failures are transient (element not yet present, UI in transition). The
retry policy must be explicit and layered:

* locator resolution inside actions and assertions: retried until the
  operation's timeout budget is exhausted (this is the §14/§14.1 behavior);
* MCP transport errors: retried only for idempotent read operations and only
  within the caller's timeout budget;
* startup: bounded retries only for the port-in-use race described in §7.1;
* actions (`click`, `fill`, `press`, ...): never retried blindly, because an
  action may already have taken effect. If an element handle becomes stale,
  re-resolve the locator and retry once; if re-resolution also fails, raise
  `LocatorError`.

Never repeat an action without knowing whether the first attempt was applied.

---

# 21. Logging

The framework should support debug logging.

Example:

```text
[slint-test] launching application
[slint-test] MCP port: 53142
[slint-test] waiting for MCP server
[slint-test] MCP initialized
[slint-test] resolving element: run-button
[slint-test] invoking click
```

Do not print excessive protocol-level details by default.

Provide a debug mode that can expose MCP requests/responses.

## 21.1 Structured logging and tracing

Use the standard `logging` module with a namespaced logger (e.g.
`slint_test`) instead of `print`:

* each `SlintApp` instance gets a correlation ID (e.g. MCP port plus child
  PID) included in every log line, so parallel or sequential multi-test runs
  stay readable;
* `DEBUG` level exposes full MCP requests/responses (JSON-RPC frames);
* the default level logs lifecycle events only (launch, ready, element
  resolution, actions, close, artifact collection);
* the unittest integration must attach the captured per-test application log to
  the failure artifacts (§16).

---

# 22. Security

The embedded MCP server is intended for local development/testing.

The test framework must therefore:

* connect only to localhost;
* not expose the MCP endpoint externally;
* not require authentication;
* not add network exposure;
* never enable the MCP server in release builds.

Do not modify the application to bind the MCP server to a public interface.

---

# 23. Production build separation

The application must have separate testing/development and production configurations.

Conceptually:

```text
Production:
    MCP disabled

Integration-test build:
    MCP enabled
```

Do not make the application depend on the testing framework at runtime in production.

Do not add testing-specific code paths to normal application behavior unless required by Slint itself.

---

# 24. First milestone: MCP connectivity

Do NOT implement the complete framework immediately.

First implement a minimal MCP client capable of:

1. Launching the application.
2. Waiting for the MCP endpoint.
3. Initializing MCP.
4. Listing available tools.
5. Calling one read-only tool.
6. Printing the result.

Create a smoke test:

```python
class McpConnectivityChecks(unittest.TestCase):

    def test_mcp_connection(self):
        ...
```

Acceptance criteria:

* application starts;
* MCP endpoint becomes available;
* MCP initialization succeeds;
* at least one Slint UI inspection operation succeeds;
* application shuts down cleanly.

Do not proceed to locator abstractions until this works.

Per §39, the framework suite validates this milestone with mocked servers and
a patched process; launching the real application for this smoke check
belongs to the plugin integration suite (second deliverable).

---

# 25. Second milestone: UI inspection

Implement:

```python
app.get_by_id(...)
```

and whatever internal operations are necessary to resolve an element.

Create a test that:

1. launches the real application;
2. finds a known UI element;
3. reads a property/text value;
4. asserts the expected value.

Example:

```python
class MainWindowChecks(unittest.TestCase):

    def test_main_window(self):
        # assert
        expect(self._app.get_by_id("status")).to_have_text("Ready")
```

---

# 26. Third milestone: interaction

Implement:

```python
click()
fill()
type()
press()
```

only for operations actually supported by the Slint MCP interface.

Create a test such as:

```python
class ButtonChecks(unittest.TestCase):

    def test_button(self):
        # act
        self._app.get_by_id("run-button").click()

        # assert
        expect(self._app.get_by_id("status")).to_have_text("Running")
```

---

# 27. Fourth milestone: assertions and waiting

Implement:

```python
expect(locator).to_have_text(...)
expect(locator).to_have_property(...)
expect(locator).to_exist()
expect(locator).to_be_visible()
```

Add polling and timeout support.

Create tests demonstrating asynchronous state transitions.

---

# 28. Fifth milestone: diagnostics

Implement:

* screenshots;
* stdout capture;
* stderr capture;
* automatic failure artifacts;
* useful exception messages;
* debug logging.

At this point the framework should be usable for real integration testing.

---

# 29. Sixth milestone: first real application tests

Only after the framework itself is stable should application-specific tests be added.

Do not start by testing complicated application workflows.

Begin with smoke tests:

```text
application starts
main window appears
expected initial state
basic navigation
basic dialog
basic input
basic save/open workflow
```

Then move to complex workflows.

---

# 30. Test design principles

Tests should interact with the application through the same mechanisms a user would use.

Prefer:

```python
app.get_by_id("save-button").click()
```

over:

```python
application_internal_state.save()
```

**Tests must never reference MCP or Slint protocol shapes.** No test code may
touch payload internals such as `typeNamesAndIds`, `elementHandles`,
`computedOpacity`, raw handles, or raw element dictionaries; whenever a test
needs such information, the missing capability belongs to the framework
(locators, assertions, collections). The only knowledge a test author needs
is the UI structure (the project's `.slint` files) and the scenario under
test.

Do not directly manipulate C++ business logic from integration tests.

The purpose of these tests is to verify integration between:

```text
UI
+
C++ application logic
+
application state
+
external processes/services
```

Unit tests remain responsible for testing individual C++ components.

## 30.1 Scenario authoring

An integration scenario is a user workflow that may span several actions and
application state transitions. Scenarios follow these rules:

* one scenario is one test method; scenarios are split by behavior, not by
  line count — a long method covering one coherent workflow is preferred over
  several methods sharing setup;
* each scenario launches a fresh application process (§46); a sequential
  workflow cannot be split across test methods because test methods never
  share state;
* workflow scenarios are structured with `# step N:` comment markers instead
  of the `# arrange` / `# act` / `# assert` markers, which remain for simple
  single-state assertions;
* every step is followed by its own assertions so a failure pinpoints the
  broken stage; assertion timeouts (5s default) absorb UI refresh races
  between the action and the state update;
* long-running steps (e.g. a real simulation) use an explicit, generous wait
  timeout scoped to that step only;
* scenario setup is expressed through data, never through shared test code:
  the starting state comes from `launch()` parameters (command line
  arguments, environment) and read-only fixture files; the framework is the
  only shared code between scenarios;
* duplication across scenarios is accepted: self-contained scenarios stay
  independent when one of them fails;
* external executables required by a scenario (e.g. the real Xyce binary)
  are resolved from the environment; when unavailable the scenario skips
  with a clear message instead of failing.

---

# 31. Stable UI identifiers

Element IDs (`id :=`) must exist only for application requirements; tests
prefer role-based lookup (§10) and use ids as the last resort. Do NOT add
ids purely for testing: unused ids can add compilation warnings and the
build must stay 100% clean. Existing declared IDs must remain stable and
semantic:

Good:

```text
MainWindow::toolbar
ToolbarButton::ta
save-button
simulation-status
```

Bad:

```text
button1
rectangle7
foo
element123
```

IDs should describe the semantic purpose of the element rather than its implementation.
Accessible properties (`accessible-label`, `accessible-value`) complement IDs for reading text
content and asserting state.

---

# 32. Selector strategy

The preferred selector is role based:

```python
get_by_role()
```

backed by `query_element_descendants` with `matchElementAccessibleRole`
(first-match semantics, verified live). Type based locators select elements
by their Slint type in declaration order for collections:

```python
app.get_by_type("ToolbarButton")    # collection: count(), nth(i), all()
locator.child("Image")              # strict child locator inside the subtree
```

`get_by_id()` remains available as the last resort (§10). `get_by_text()` /
`get_by_label()` stay unimplemented until a test needs them; do not emulate
browser DOM semantics unnecessarily.

---

# 33. Do not use pixel-based testing initially

Do not make screenshot comparison the primary test mechanism.

The first framework should validate:

```text
element exists
element state
element properties
element actions
application state
```

Pixel comparison can be added as a separate feature later.

---

# 34. Future extension: Playwright-style API

The API should be designed so that the following is possible later:

```python
app.get_by_id("foo")
app.get_by_role("button", name="Run")
expect(locator).to_have_text("Done")
```

But do not attempt to become Playwright.

The framework should remain a native Slint testing abstraction.

---

# 35. Future extension: TypeScript client

If the Python implementation proves useful, a TypeScript implementation may eventually be added.

It should communicate with the same MCP endpoint:

```text
Python framework ─┐
                  |
TypeScript client ├──> Slint MCP
                  |
future clients ───┘
```

Therefore the MCP protocol layer must remain independent from the test-language API.

---

# 36. Future extension: Playwright adapter

A Playwright adapter is explicitly out of scope for the initial implementation.

If later desired, it could expose a Playwright-like API or integrate with Playwright's test runner.

Do not compromise the initial framework architecture to support this prematurely.

---

# 37. Dependencies

Keep dependencies minimal.

Preferred initial dependencies:

```text
HTTP/MCP client implementation (standard library only)
```

The framework core has **no test-runner dependency**: `unittest` is used only
by this repository's own suites and the optional `SlintTestCase` adapter
(§17). A future spin-off package ships the neutral core plus the optional
unittest adapter; pytest integration belongs to the embedding application.

Use an existing MCP client library only if:

1. it supports the exact Streamable HTTP MCP protocol required by the Slint version;
2. it is stable;
3. it does not introduce unnecessary complexity.

Otherwise implement the minimal MCP transport required by Slint.

Do not add Selenium.

Do not add Playwright.

Do not add another GUI automation library.

Do not add OCR.

---

# 38. Coding standards

Follow `STYLE-GUIDE.md` at the repository root — it is authoritative for both
C++ and Python.

For Python:

* type annotations;
* dataclasses where appropriate;
* clear exception hierarchy;
* no global mutable state;
* context managers for process ownership;
* small classes with clear responsibilities;
* unit tests for framework internals.

Mandatory comment style (from `STYLE-GUIDE.md`, applies to all framework and
test code):

* comments are placed **above** the code they describe, never inline;
* format `# comment text` — starts with a lowercase letter, no trailing
  period, single line;
* **every non-trivial statement gets its own comment line above it**,
  including statements inside `if` blocks, loops, and other control
  structures;
* no docstrings of any kind (`"""..."""` is forbidden).

Additional Python rules from `STYLE-GUIDE.md`:

* member variables use a `_` prefix with snake_case (`self._process`);
* constants use `UPPER_SNAKE_CASE`;
* imports in three blank-line-separated sections: standard library,
  third-party, project files (each alphabetical);
* tests use the standard library `unittest` (see §17), files named
  `<module>_test.py` under `tests/`, PascalCase suite names, snake_case test
  names, and every test structured with explicit `# arrange` / `# act` /
  `# assert` markers (`# arrange / act` allowed when setup and execution are
  one step); no blank lines between the sections; tests are self-contained;
* **no multiline function definitions or calls** — keep them on a single
  line, even if long (see `STYLE-GUIDE.md`, Function Definitions and Calls).

Do not introduce unnecessary abstraction layers.

---

# 39. Framework unit tests

The framework test suite must be **fully (100%) mock based**: no framework
test may launch the real application or any GUI process. Use mocked HTTP/MCP
servers, fake process objects, and fake clients. Real-application coverage
belongs to the xyce-studio integration test suite (the second
deliverable), which lives under `tests_ui/` and uses
real UI arrange/act/assert flows.

At minimum test:

```text
MCP request construction
MCP response parsing
MCP errors
request IDs
timeouts
port allocation
process startup
process shutdown
locator behavior
assertion polling
timeout handling
retry policy for transient failures
explicit wait utilities
feature detection / missing-tool errors
port allocation race handling
```

Do not require a real Slint application for tests of the MCP transport layer.

Use mocked HTTP/MCP responses where appropriate.

Then use a small real Slint application for end-to-end framework tests.

---

# 40. Reference test application

Superseded by the mock-only policy in §39: the framework test suite is fully
mock based and does not need a reference Slint application. The real
application is exercised directly by the plugin integration test suite (the
second deliverable under `tests_ui/`).

This avoids confusing framework failures with application failures.

---

# 41. Acceptance criteria for version 0.1

Version 0.1 is complete when all of the following are true:

### Process

* [ ] Test framework can launch the real C++ application.
* [ ] MCP port is dynamically allocated.
* [ ] Application receives `SLINT_MCP_PORT`.
* [ ] Framework waits for MCP readiness.
* [ ] Application is terminated correctly.
* [ ] No orphan application processes remain.

### MCP

* [ ] MCP initialization works.
* [ ] MCP tool invocation works.
* [ ] MCP errors are converted into useful Python exceptions.
* [ ] MCP implementation is isolated in `McpClient`.

### UI

* [ ] UI element can be located by stable ID.
* [ ] UI text/property can be read.
* [ ] At least one UI action can be performed.
* [ ] Element handles are hidden from tests.

### Assertions

* [ ] `expect(locator)` exists.
* [ ] text assertion works.
* [ ] existence assertion works.
* [ ] at least one property assertion works.
* [ ] assertions support timeout/polling.

### Diagnostics

* [ ] screenshots can be captured.
* [ ] stdout/stderr are captured.
* [ ] failed tests generate useful artifacts.

### Stability

* [ ] explicit wait utilities exist (`wait_for_condition`, `wait_for_exists`,
      `wait_for_gone`);
* [ ] tests run sequentially without state leakage between tests;
* [ ] a missing/unknown MCP tool produces a clear error naming the tool and
      the detected Slint version;
* [ ] port allocation handles the port-in-use race without flakiness;
* [ ] actions are never retried blindly (§20.2).

### Testing

* [ ] framework unit tests exist;
* [ ] framework end-to-end tests exist;
* [ ] at least one real application integration test exists.

---

# 42. Agent instructions

The coding agent must work incrementally.

Do not implement the entire framework in one change.

Use this sequence:

```text
Phase 1  — DONE
  Inspect repository
  Inspect Slint version
  Inspect actual MCP implementation
  Determine exact protocol/tools
  → documented in slint_automation/slint-mcp-api.md (verified live)

Phase 2  — DONE
  Implement MCP client
  Add MCP connectivity smoke test
  → framework/mcp_client.py, framework/errors.py,
    tests/mcp_client_test.py (mock transport), tests/mcp_connectivity_test.py

Phase 3  — DONE
  Implement SlintApp
  Implement process management
  → framework/slint_application.py (dynamic port, readiness wait, graceful
    shutdown with kill fallback, bounded launch retries per §7.1)

Phase 4  — DONE
  Implement Locator
  Implement get_by_id()
  → framework/locator.py, tests/locator_test.py (fake client unit tests),
    tests/element_inspection_test.py (real app, §25 milestone)
  → src/ui/widgets/main_window.slint gained the stable id `status` on the
    status bar text element per §31

Phase 5  — DONE
  Implement basic UI actions
  → framework/locator.py gained click() / fill() / press();
    framework/slint_client.py gained click_element / set_element_value /
    dispatch_key_event
  → type() is intentionally deferred: the MCP server has no element-focused
    character typing (dispatch_key_event is window-level); its own guidance
    is to set text via set_element_value (fill)

Phase 6  — DONE
  Implement expect/assertions
  Implement explicit wait utilities (§14.1)
  → framework/assertions.py: expect(locator) with to_exist / to_have_text /
    to_have_property / to_be_visible / to_be_enabled and negation via
    expect(locator).not_.<assertion>
  → framework/waiting.py: shared poll helper (deadline, poll interval,
    failure message with last observed state, §14/§20)
  → locator.wait_for_exists / wait_for_gone and
    app.wait_for_condition per §14.1
  → SlintAssertionError added to framework/errors.py
  → visibility maps to computedOpacity and enabled to accessibleEnabled;
    protobuf JSON omits zero/false values so absence means 0.0 / False

Phase 7  — DONE
  Implement diagnostics/artifacts
  Implement structured logging with correlation IDs (§21.1)
  → framework/slint_application.py: stdout/stderr capture via spooled temp
    files, screenshot(), collect_artifacts() (best effort screenshot.png /
    ui-tree.json / stdout.txt / stderr.txt per §16)
  → framework/test_case.py: SlintTestCase collects failure artifacts
    automatically on unittest failures (§16); artifacts root configurable
    through SLINT_TEST_ARTIFACTS
  → framework/log.py: namespaced slint_test logger, SLINT_TEST_DEBUG env
    toggle for protocol-level debug logging; McpClient logs full
    requests/responses at debug level

Phase 8  — DONE
  Add real application integration tests
  → tests_ui/smoke_test.py: application starts, main
    window appears, toolbar id located, initial status text asserted via
    get_by_role("Text") per §10 (no test-only ids added to the UI)
```

Test isolation (§46) and test data management (§47) are part of Phase 3
(process management) and must be in place before Phase 8. Parallel execution
(§48) is not part of the initial plan; only its preconditions are.

After each phase:

1. Build.
2. Run unit tests.
3. Run integration tests where applicable.
4. Fix failures.
5. Only then continue.

Do not make assumptions about Slint MCP APIs without inspecting the installed/current source.

---

# 43. Important architectural rule

The following dependency direction must be maintained:

```text
Tests
  |
  v
SlintApp / Locator / Expect
  |
  v
McpClient
  |
  v
MCP protocol
```

Never reverse this relationship.

`McpClient` must not depend on:

```text
Locator
SlintApp
unittest
application-specific concepts
```

`Locator` must not know how HTTP or JSON-RPC works.

`SlintApp` must not construct raw MCP requests directly.

This separation is the primary architectural requirement.

---

# 44. Desired final test experience

The final framework should make tests look approximately like:

```python
class RunSimulationChecks(unittest.TestCase):

    def test_run_simulation(self) -> None:
        # arrange: launch the application in a session scoped to this test
        with TestSession(launch(), self.id()) as app:
            # assert
            expect(app.get_by_role("Text")).to_have_text("Ready")

            # act
            app.get_by_role("Button").click()

            # assert
            expect(app.get_by_role("Text")).to_have_text("Simulation complete", timeout=30)
```

The session closes the application and collects failure artifacts
automatically; tests never call `close()` themselves and never inherit
framework classes.

The test author should not need to know:

* MCP;
* JSON-RPC;
* HTTP;
* TCP ports;
* element handles;
* Slint internal implementation;
* process management.

Those are framework implementation details.

The framework's purpose is to make **real Slint application integration tests simple and maintainable**.

---

# 45. Explicit non-goals

Do NOT implement initially:

* Playwright compatibility;
* Selenium compatibility;
* browser automation;
* OCR;
* computer-vision element detection;
* pixel-perfect screenshot testing;
* distributed test execution;
* remote application testing;
* authentication;
* production MCP support;
* a replacement for unittest;
* a replacement for Slint's commercial GUI Test Framework.

The objective is a small, reliable test framework built on the MCP server that already exists in the Slint application.

---

# 46. Test isolation and state management

Each test must start from a known application state.

Rules:

* the TestCase `setUp` launches a fresh application process per test by
  default; a single application instance must never be shared between tests;
* application state that persists on disk (recent projects, settings, caches)
  must be redirected to a per-test temporary directory through the child
  process environment (`HOME`, `XDG_*`, `APPDATA`, or application-specific
  flags); the framework implements this by injecting a per-launch
  `XDG_CONFIG_HOME` (`APPDATA` on Windows) pointing at a temporary directory
  that is removed when the application closes, so scenarios never read or
  write the real user configuration;
* `tearDown` owns removing that temporary directory;
* if the application cannot isolate its state via environment or flags,
  prefer adding a test-only flag over having tests clean global state;
* tests must not depend on state left behind by earlier tests and must not
  assume any particular execution order.

A shared long-lived instance may later be added for cheap read-only smoke
tests, but state-mutating tests always get a fresh instance.

---

# 47. Test data management

* fixture files and sample projects live in a dedicated test-data directory
  inside the test package (e.g. `tests/integration/data/`);
* tests never modify shared fixture data in place: each test copies the data
  it needs into its per-test temporary directory first;
* files created by a test are written only inside its temporary directory or
  its artifact directory;
* no fixed file paths may be shared between tests, so parallel execution
  remains possible (§48).

---

# 48. Parallel execution

Parallel execution (e.g. a parallel unittest runner) is not an initial
requirement, but the design must not preclude it:

* port allocation per instance (§7.1);
* per-test application process and temporary directories (§46, §47);
* per-test artifact directories;
* no global mutable state in the framework; configuration objects are passed
  explicitly, never read from module-level globals;
* when parallelism is added, the runner integration must key artifacts and
  logs by test name plus worker ID to avoid collisions.

---

# 49. Slint / MCP version compatibility

The framework couples to whatever MCP surface the pinned Slint version
exposes. To keep Slint upgrades manageable:

* at connect time, `McpClient` records the detected Slint version and the
  list of tools the server advertises;
* the framework performs feature detection against the advertised tool list
  instead of assuming every tool exists;
* if a required tool is missing, raise a clear `McpError` naming the missing
  tool and the detected Slint version, instead of failing deep inside a
  locator;
* all knowledge of tool names and arguments lives in the MCP adapter layer
  (`mcp_client.py` plus a thin Slint-specific adapter), so a Slint upgrade is
  ideally a single-module change;
* maintain a small compatibility matrix in this document when the Slint
  version is upgraded (version → observed tool/behavior changes).

---

# 50. Contributor guide

* development setup: create a virtual environment, install the test package
  in editable mode, and point `SLINT_TEST_APPLICATION` at a debug build of
  the application (the debug build enables the embedded MCP server);
* framework changes require: framework unit tests, a run of the framework
  end-to-end tests against the reference test application (§40), and at
  least the real-application smoke tests;
* Python style follows §38 and `STYLE-GUIDE.md`; type annotations are
  mandatory; comments follow the mandatory comment rules (a `#` comment above
  every non-trivial statement, lowercase, no period) and docstrings are
  forbidden;
* changes to the MCP adapter layer must be verified against the actual Slint
  version in use, per §2.4;
* CI should run framework unit tests on every change and integration tests
  against the debug build wherever a GUI/display is available.

---

# 51. Documentation requirements

* the framework package ships a README covering: installation, configuration
  (§18, §19), writing tests (§44), and debugging failures (§16, §21);
* the application documentation must describe which builds enable the MCP
  server and how stable IDs are assigned in Slint code (§31);
* every locator action and assertion added to the framework must document its
  MCP-backed behavior and any limitations.

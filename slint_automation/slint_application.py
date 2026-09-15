import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from types import TracebackType
from typing import BinaryIO, Callable

from .errors import ApplicationStartupError, McpError
from .locator import Locator, LocatorCollection
from .log import logger
from .mcp_client import McpClient
from .slint_client import SlintClient
from .waiting import DEFAULT_POLL_INTERVAL, DEFAULT_WAIT_TIMEOUT, reports_test_frames, wait_for

DEFAULT_STARTUP_TIMEOUT = 10.0
DEFAULT_READY_POLL_INTERVAL = 0.1
DEFAULT_TERMINATE_TIMEOUT = 5.0
MAX_LAUNCH_ATTEMPTS = 3
DEFAULT_EXECUTABLE_ENVIRONMENT_VARIABLE = "SLINT_TEST_APPLICATION"
CLEAN_ENVIRONMENT_VARIABLES = [
    "PATH", "HOME", "TMPDIR", "TEMP", "TMP", "LANG", "LC_ALL", "USER",
    "LOGNAME", "SHELL", "DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY",
    "XDG_RUNTIME_DIR", "XDG_DATA_DIRS", "XDG_CONFIG_DIRS", "SYSTEMROOT",
    "SYSTEMDRIVE", "COMSPEC", "PATHEXT", "APPDATA", "LOCALAPPDATA",
    "PROGRAMFILES", "WINDIR",
]
_LOGGER = logger()


class SlintApplication:

    def __init__(self, process: subprocess.Popen, client: SlintClient, port: int, stdout_file: BinaryIO | None = None, stderr_file: BinaryIO | None = None, config_dir: tempfile.TemporaryDirectory | None = None) -> None:
        # process owns the OS process for the Slint application
        self._process = process
        # client provides UI interaction capabilities
        self._client = client
        # port is the localhost port of the embedded mcp server
        self._port = port
        # stdout_file captures the child standard output when enabled
        self._stdout_file = stdout_file
        # stderr_file captures the child standard error when enabled
        self._stderr_file = stderr_file
        # config_dir isolates the application persistent configuration for this launch
        self._config_dir = config_dir

    def client(self) -> SlintClient:
        # return the SlintClient instance for UI interaction
        return self._client

    def port(self) -> int:
        # return the localhost port used by the embedded mcp server
        return self._port

    def is_running(self) -> bool:
        # report whether the application process is still alive
        return self._process.poll() is None

    def get_by_id(self, elements_id: str) -> Locator:
        # create a lazy locator for the qualified element id
        return Locator(self._client, elements_id=elements_id)

    def get_by_role(self, role: str) -> Locator:
        # create a lazy locator for the accessible role
        return Locator(self._client, role=role)

    def get_by_type(self, type_name: str) -> LocatorCollection:
        # create a collection locator for the slint type name
        return LocatorCollection(self._client, type_name)

    def invoke_chart_action(self, action: str, chart_position: float = 0.5) -> None:
        # invoke a charts panel context menu action through the hidden debug
        # triggers; the native context menu cannot be reached by the automation
        # because the platform renders it, so debug builds expose invisible
        # trigger elements calling exactly what the matching menu item calls.
        # the action is the kebab-case trigger id (e.g. "add-chart") and the
        # chart position is a float [0..1] locating the target chart in the
        # vertical stack, translated to an index by the charts renderer
        trigger = self.get_by_id(f"ChartsPanel::test-{action}")
        # filling the trigger fires accessible-action-set-value, which invokes
        # the wrapped action with the position argument
        trigger.fill(str(chart_position))

    @reports_test_frames
    def wait_for_condition(self, condition: Callable[[], bool], *, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL, message: str) -> None:
        # wait until the caller supplied condition holds
        wait_for(condition, timeout=timeout, poll_interval=poll_interval, timeout_message=message)

    def screenshot(self, path: str) -> None:
        # capture a png screenshot of the current window
        data = self._client.take_screenshot()
        # write the png bytes to the target file
        Path(path).write_bytes(data)

    def stdout_text(self) -> str:
        # report empty output when the stream was not captured
        if self._stdout_file is None:
            return ""
        # rewind and decode the captured standard output
        self._stdout_file.seek(0)
        return self._stdout_file.read().decode(errors="replace")

    def stderr_text(self) -> str:
        # report empty output when the stream was not captured
        if self._stderr_file is None:
            return ""
        # rewind and decode the captured standard error
        self._stderr_file.seek(0)
        return self._stderr_file.read().decode(errors="replace")

    def collect_artifacts(self, directory: str) -> str:
        # create the artifact directory for the failing test
        path = Path(directory)
        path.mkdir(parents=True, exist_ok=True)
        # capture the best effort screenshot artifact
        self._write_screenshot_artifact(path)
        # capture the ui tree artifact
        self._write_ui_tree_artifact(path)
        # capture the process output artifacts
        self._write_text_artifact(path / "stdout.txt", self.stdout_text())
        self._write_text_artifact(path / "stderr.txt", self.stderr_text())
        # log the artifact collection for debugging
        _LOGGER.info("[%s] collected failure artifacts in %s", self._label(), path)
        # return the artifact directory for the caller
        return str(path)

    def close(self) -> None:
        # ignore repeated close calls on an already terminated process
        if self._process.poll() is not None:
            self._close_output_files()
            self._close_config_dir()
            return
        # terminate the application process gracefully
        self._process.terminate()
        try:
            # wait for the process to fully exit
            self._process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
        except subprocess.TimeoutExpired:
            # force kill when graceful termination fails
            self._process.kill()
            # wait for the killed process to be reaped
            self._process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
        # release the captured output files after the process exited
        self._close_output_files()
        # remove the isolated configuration directory after the process exited
        self._close_config_dir()
        # log the shutdown for the correlation label
        _LOGGER.info("[%s] application closed", self._label())

    def __enter__(self) -> "SlintApplication":
        # support for 'with' statement context manager
        return self

    def __exit__(self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: TracebackType | None) -> bool:
        # ensure cleanup on context exit
        self.close()
        # return False to propagate any exceptions
        return False

    def _label(self) -> str:
        # build the correlation label from the port and child pid
        return f"port {self._port}/pid {getattr(self._process, 'pid', '?')}"

    def _close_output_files(self) -> None:
        # close the captured output files when present
        for file in (self._stdout_file, self._stderr_file):
            # closed files are set to none after release
            if file is not None:
                file.close()
        # drop the references to the closed files
        self._stdout_file = None
        self._stderr_file = None

    def _close_config_dir(self) -> None:
        # nothing to clean when the launch used no isolated configuration
        if self._config_dir is None:
            return
        # remove the per-launch configuration directory, tolerating cleanup failures
        try:
            self._config_dir.cleanup()
        except OSError:
            # log the leftover directory at debug level for diagnosis
            _LOGGER.debug("[%s] configuration directory cleanup failed", self._label())
        # drop the reference so repeated close calls stay idempotent
        self._config_dir = None

    def _write_screenshot_artifact(self, path: Path) -> None:
        # capture the screenshot best effort without masking the test failure
        try:
            # write the png bytes into the artifact directory
            self.screenshot(str(path / "screenshot.png"))
        except Exception:
            # log the skipped artifact at debug level
            _LOGGER.debug("[%s] screenshot artifact unavailable", self._label())

    def _write_ui_tree_artifact(self, path: Path) -> None:
        # capture the ui tree best effort without masking the test failure
        try:
            # read the element tree from the first window
            tree = self._client.get_element_tree()
            # write the tree as formatted json into the artifact directory
            (path / "ui-tree.json").write_text(json.dumps(tree, indent=2) + "\n")
        except Exception:
            # log the skipped artifact at debug level
            _LOGGER.debug("[%s] ui tree artifact unavailable", self._label())

    def _write_text_artifact(self, path: Path, text: str) -> None:
        # write the captured stream text into the artifact directory
        path.write_text(text)


def launch(executable_path: str | None = None, *, startup_timeout: float = DEFAULT_STARTUP_TIMEOUT, env: dict[str, str] | None = None, args: list[str] | None = None) -> "SlintApplication":
    # resolve the executable from the argument or the framework defaults
    path = executable_path or default_executable()
    # log the launch request for the caller
    _LOGGER.info("launching application %s", path)
    # retry the launch to absorb the free-port allocation race
    last_error: ApplicationStartupError | None = None
    for _attempt in range(MAX_LAUNCH_ATTEMPTS):
        try:
            return _launch_attempt(path, startup_timeout, env or {}, args or [])
        except ApplicationStartupError as error:
            last_error = error
    # all attempts failed so surface the last startup error
    raise ApplicationStartupError(f"failed to launch application after {MAX_LAUNCH_ATTEMPTS} attempts: {last_error}")


def default_executable() -> str:
    # use the override executable when provided through the environment
    executable = os.environ.get(DEFAULT_EXECUTABLE_ENVIRONMENT_VARIABLE)
    # fall back to the debug build at the repository root otherwise
    if not executable:
        executable = str(Path(__file__).resolve().parents[1] / ".build-debug" / "xyce-studio")
    # return the resolved executable path
    return executable


def _child_environment(overrides: dict[str, str]) -> dict[str, str]:
    # start from a minimal copy of the essential system variables so the
    # application under test never inherits variables injected by external
    # tooling (editor environments, .env files, shells, ...)
    env = {name: value for name, value in os.environ.items() if name in CLEAN_ENVIRONMENT_VARIABLES}
    # apply the explicitly injected application variables on top
    env.update(overrides)
    # return the controlled child environment
    return env


def _launch_attempt(executable_path: str, startup_timeout: float, env_overrides: dict[str, str], command_arguments: list[str]) -> "SlintApplication":
    # allocate a free localhost port for the embedded mcp server
    port = _allocate_port()
    # build the controlled child environment with the mcp server port enabled
    env = _child_environment(env_overrides)
    env["SLINT_MCP_PORT"] = str(port)
    # redirect the persistent configuration into a per-launch temporary directory so
    # scenarios never read or write the real user configuration (see design §46);
    # a configuration root supplied by the caller wins so scenarios can share
    # persistent configuration state across launches
    config_variable = "APPDATA" if sys.platform == "win32" else "XDG_CONFIG_HOME"
    if config_variable in env:
        # keep the caller provided configuration root and skip the isolation
        config_dir = None
    else:
        # create the per-launch isolated configuration directory
        config_dir = tempfile.TemporaryDirectory(prefix="xyce-studio-config-")
        # point the configuration root at the isolated directory
        env[config_variable] = config_dir.name
    # open spooled temp files capturing the child output streams
    stdout_file = tempfile.TemporaryFile()
    stderr_file = tempfile.TemporaryFile()
    # launch the application as a subprocess with the command line arguments appended after the executable
    process = subprocess.Popen([executable_path, *command_arguments], env=env, stdout=stdout_file, stderr=stderr_file)
    # build the mcp transport and the ui facade on top of it
    mcp = McpClient(port)
    client = SlintClient(mcp)
    app = SlintApplication(process, client, port, stdout_file, stderr_file, config_dir)
    try:
        # wait until the mcp server answers the initialize handshake
        _wait_until_ready(mcp, startup_timeout, process)
    except ApplicationStartupError:
        # terminate the failed application before retrying
        app.close()
        raise
    # log the readiness with the correlation label
    _LOGGER.info("[%s] application ready", app._label())
    # return the ready application instance
    return app


def _wait_until_ready(mcp: McpClient, startup_timeout: float, process: subprocess.Popen) -> None:
    # poll the mcp handshake until the startup deadline expires
    deadline = time.monotonic() + startup_timeout
    while time.monotonic() < deadline:
        # fail fast when the application process already exited
        if process.poll() is not None:
            raise ApplicationStartupError(f"application exited during startup with code {process.returncode}")
        try:
            # the initialize handshake doubles as the readiness probe
            mcp.connect()
            return
        except McpError:
            # server not ready yet so keep polling until the deadline
            time.sleep(DEFAULT_READY_POLL_INTERVAL)
    # the server never became reachable within the timeout
    raise ApplicationStartupError(f"mcp server did not become ready within {startup_timeout} seconds")


def _allocate_port() -> int:
    # bind an ephemeral localhost socket to discover a free port
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        # bind to port 0 to let the os choose a free port
        sock.bind(("127.0.0.1", 0))
        # report the allocated port
        return sock.getsockname()[1]

import json
import os
import re
import subprocess
import tempfile
import time
from pathlib import Path
from types import TracebackType
from typing import BinaryIO, Callable

from .albor_client import AlborClient
from .errors import ApplicationStartupError
from .locator import Locator
from .log import logger
from .mcp_client import McpClient
from .waiting import DEFAULT_POLL_INTERVAL, DEFAULT_WAIT_TIMEOUT, reports_test_frames, wait_for

# the application needs roughly five seconds to reach the handshake on an idle
# machine, and a busy machine may take longer before the automation host binds
# its socket; the generous budget still fails fast on a broken build
DEFAULT_STARTUP_TIMEOUT = 30.0
DEFAULT_TERMINATE_TIMEOUT = 5.0
DEFAULT_EXECUTABLE_ENVIRONMENT_VARIABLE = "ALBOR_TEST_APPLICATION"

# the descriptor line the automation host prints once the endpoint is bound
DESCRIPTOR_PATTERN = re.compile(r"^albor automation: serving (?P<descriptor>.+)$")

_LOGGER = logger()


class AlborApplication:
    # one launched application process and the connected automation client

    def __init__(self, process: subprocess.Popen, client: AlborClient, descriptor_path: Path, stdout_file: BinaryIO | None = None, stderr_file: BinaryIO | None = None, config_dir: tempfile.TemporaryDirectory | None = None) -> None:
        # process owns the OS process for the albor application
        self._process = process
        # client provides ui interaction capabilities over the socket transport
        self._client = client
        # descriptor_path is the host.json the automation host published
        self._descriptor_path = descriptor_path
        # stdout_file captures the child standard output when enabled
        self._stdout_file = stdout_file
        # stderr_file captures the child standard error when enabled
        self._stderr_file = stderr_file
        # config_dir isolates the application persistent configuration for this launch
        self._config_dir = config_dir

    def client(self) -> AlborClient:
        # return the ui interaction client for direct protocol level access
        return self._client

    def descriptor_path(self) -> Path:
        # return the automation descriptor path the host published
        return self._descriptor_path

    def is_running(self) -> bool:
        # report whether the application process is still alive
        return self._process.poll() is None

    def get_by_id(self, node_id: str) -> Locator:
        # create a lazy locator for the author assigned node id
        return Locator(self, node_id)

    @reports_test_frames
    def wait_for_condition(self, condition: Callable[[], bool], *, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL, message: str) -> None:
        # wait until the caller supplied condition holds
        wait_for(condition, timeout=timeout, poll_interval=poll_interval, timeout_message=message, observe=self._tree_text)

    def screenshot(self, path: str) -> None:
        # capture the window image and write it to the target path
        png = self._client.screenshot()
        Path(path).write_bytes(png)

    def tree_text(self) -> str:
        # return the raw tree dump for debugging and artifact capture
        return self._tree_text()

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
        try:
            self.screenshot(str(path / "screenshot.png"))
        except Exception:
            # a screenshot that cannot be captured must not mask the test failure
            pass
        # capture the tree dump artifact
        try:
            (path / "tree.txt").write_text(self.tree_text())
        except Exception:
            # a tree dump that cannot be read must not mask the test failure
            pass
        # write the descriptor content for connection diagnostics
        try:
            (path / "descriptor.json").write_text(self._descriptor_path.read_text())
        except Exception:
            pass
        # capture the process output artifacts
        (path / "stdout.txt").write_text(self.stdout_text())
        (path / "stderr.txt").write_text(self.stderr_text())
        # log the artifact collection for debugging
        _LOGGER.info("[%s] collected failure artifacts in %s", self._label(), path)
        # return the artifact directory for the caller
        return str(path)

    def close(self) -> None:
        # terminate the application process gracefully when it is still alive
        if self._process.poll() is None:
            self._process.terminate()
            try:
                # wait for the process to fully exit
                self._process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
            except subprocess.TimeoutExpired:
                # a process that ignores termination is killed outright
                self._process.kill()
                self._process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
        # release the transport even when the process died on its own, which
        # is what keeps the client socket from leaking past a crashed launch
        self._client._mcp.close()
        # release the captured output files and the isolated configuration
        self._close_output_files()
        self._close_config_dir()

    def __enter__(self) -> "AlborApplication":
        # support for 'with' statement context manager
        return self

    def __exit__(self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: TracebackType | None) -> bool:
        # ensure cleanup on context exit
        self.close()
        # return False to propagate any exceptions
        return False

    def _label(self) -> str:
        # build the correlation label from the descriptor path and child pid
        return f"pid {self._process.pid} {self._descriptor_path}"

    def _tree_text(self) -> str:
        # observe the live tree for wait failure messages
        return self._client._mcp.call_tool("semantic.tree")

    def _close_output_files(self) -> None:
        # close the captured output files when present
        for file in (self._stdout_file, self._stderr_file):
            if file is not None:
                file.close()
        # drop the references so repeated close calls stay idempotent
        self._stdout_file = None
        self._stderr_file = None

    def _close_config_dir(self) -> None:
        # nothing to clean when the launch used no isolated configuration
        if self._config_dir is None:
            return
        # remove the per launch configuration directory
        self._config_dir.cleanup()
        self._config_dir = None


def launch(executable_path: str | None = None, *, startup_timeout: float = DEFAULT_STARTUP_TIMEOUT, env: dict[str, str] | None = None, args: list[str] | None = None, config_dir: bool = True) -> "AlborApplication":
    # resolve the executable from the argument or the framework defaults
    path = executable_path or default_executable()
    # launch the application and wait for the automation host to bind its endpoint
    return _launch_attempt(path, startup_timeout, env or {}, args or [], config_dir)


def default_executable() -> str:
    # use the override executable when provided through the environment
    override = os.environ.get(DEFAULT_EXECUTABLE_ENVIRONMENT_VARIABLE)
    if override:
        return override
    # fall back to the studio debug build at the repository root
    return str(Path(__file__).resolve().parents[1] / ".build-studio-debug" / "xyce-studio-next")


def _launch_attempt(executable_path: str, startup_timeout: float, env_overrides: dict[str, str], command_arguments: list[str], config_dir: bool) -> "AlborApplication":
    # prepare the isolated configuration directory unless the caller seeded one
    temp_config: tempfile.TemporaryDirectory | None = None
    child_env = _child_environment(env_overrides)
    if config_dir and "XDG_CONFIG_HOME" not in child_env:
        # a per launch directory keeps scenarios from reading developer configuration
        temp_config = tempfile.TemporaryDirectory(prefix="xyce-studio-config-")
        child_env["XDG_CONFIG_HOME"] = temp_config.name
    # open spooled files that capture the child output streams
    stdout_file = tempfile.TemporaryFile()
    stderr_file = tempfile.TemporaryFile()
    process: subprocess.Popen | None = None
    try:
        # start the application process with the descriptor discovery from stdout
        process = subprocess.Popen(
            [executable_path] + command_arguments,
            env=child_env,
            stdout=stdout_file,
            stderr=stderr_file,
        )
        # wait until the host prints the descriptor and the endpoint is reachable
        descriptor_path = _wait_until_ready(process, stdout_file, stderr_file, startup_timeout)
        # read the descriptor the host published for the socket and token paths
        descriptor = json.loads(Path(descriptor_path).read_text())
        # read the token file the descriptor points at
        token = Path(descriptor["token"]).read_text().strip()
        # build the transport over the unix socket and run the handshake
        mcp = McpClient(descriptor["socket"], token)
        mcp.connect()
    except Exception:
        # every failure from spawn through handshake releases the resources,
        # which is what keeps a failed launch from leaking a process or files
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
        stdout_file.close()
        stderr_file.close()
        if temp_config is not None:
            temp_config.cleanup()
        raise
    # the application is ready when the handshake succeeded
    return AlborApplication(process, AlborClient(mcp), Path(descriptor_path), stdout_file, stderr_file, temp_config)


def _wait_until_ready(process: subprocess.Popen, stdout_file: BinaryIO, stderr_file: BinaryIO, startup_timeout: float) -> Path:
    # poll the child stdout for the descriptor line until the deadline expires
    deadline = time.monotonic() + startup_timeout
    descriptor_path: Path | None = None
    while time.monotonic() < deadline:
        # fail fast when the application exited before the host bound its endpoint
        if process.poll() is not None:
            # capture both streams for the startup error message
            stdout_file.seek(0)
            output = stdout_file.read().decode(errors="replace")
            stderr_file.seek(0)
            errors = stderr_file.read().decode(errors="replace")
            raise ApplicationStartupError(
                f"application exited with code {process.returncode} before the automation host served"
                f"\ndescriptor output:\n{output}\nstandard error:\n{errors}"
            )
        # the host writes one line when the endpoint binds, which is what we read
        output = _read_stream(stdout_file)
        errors = _read_stream(stderr_file)
        # the descriptor line may arrive on either stream, so both are scanned
        descriptor_path = _descriptor_in(output) or _descriptor_in(errors)
        if descriptor_path is not None:
            break
        # the host needs a frame to bind the endpoint, so wait between polls
        time.sleep(0.1)
    # a timeout without the descriptor line is a startup failure
    if descriptor_path is None:
        # the process may still be alive but the host never served
        output = _read_stream(stdout_file)
        errors = _read_stream(stderr_file)
        raise ApplicationStartupError(
            f"automation host did not serve a descriptor within {startup_timeout}s"
            f"\noutput:\n{output}\nstandard error:\n{errors}"
        )
    # the socket only becomes reachable after the descriptor exists, so poll it
    _wait_until_socket(descriptor_path, startup_timeout)
    return descriptor_path


def _read_stream(file: BinaryIO) -> str:
    # rewind and decode one captured output stream
    file.seek(0)
    return file.read().decode(errors="replace")


def _descriptor_in(output: str) -> Path | None:
    # find the descriptor path the serving line carries, or None
    for line in output.splitlines():
        matched = DESCRIPTOR_PATTERN.match(line)
        if matched is not None:
            return Path(matched.group("descriptor"))
    return None


def _wait_until_socket(descriptor_path: Path, startup_timeout: float) -> None:
    # read the socket path from the descriptor and wait until it accepts
    deadline = time.monotonic() + startup_timeout
    while time.monotonic() < deadline:
        # the descriptor may still be written when the socket is not yet bound
        try:
            socket_path = json.loads(descriptor_path.read_text())["socket"]
        except (OSError, json.JSONDecodeError, KeyError):
            time.sleep(0.1)
            continue
        # the socket exists when the path resolves and accepts a probe connection
        import socket as socket_module

        probe = socket_module.socket(socket_module.AF_UNIX, socket_module.SOCK_STREAM)
        try:
            probe.connect(socket_path)
            probe.close()
            return
        except OSError:
            # a socket that does not accept yet needs another frame to bind
            probe.close()
            time.sleep(0.1)
    raise ApplicationStartupError(f"the automation socket did not become reachable within {startup_timeout}s")


def _child_environment(overrides: dict[str, str]) -> dict[str, str]:
    # start from the process environment and apply the caller overrides
    env = os.environ.copy()
    env.update(overrides)
    return env

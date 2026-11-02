import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import albor_automation.albor_application
from albor_automation.albor_application import AlborApplication, default_executable, launch
from albor_automation.errors import ApplicationStartupError, AlborTestError, LocatorError
from albor_automation.locator import Locator


class FakeProcess:
    # fake subprocess handle recording the termination calls close makes

    def __init__(self, hang_on_wait: bool = False, exit_code: int | None = None) -> None:
        # hang_on_wait models a process that ignores termination until killed
        self._hang_on_wait = hang_on_wait
        # _exit_code is the value poll returns once the process finished
        self._exit_code = exit_code
        # returncode mirrors the final status the launcher error messages read
        self.returncode = exit_code
        # pid is read by the correlation label
        self.pid = 4242
        # terminated counts the terminate calls, killed the kill calls
        self.terminated = 0
        self.killed = 0

    def poll(self) -> int | None:
        return self._exit_code

    def terminate(self) -> None:
        self.terminated += 1
        if not self._hang_on_wait:
            self._exit_code = 0
            self.returncode = 0

    def kill(self) -> None:
        self.killed += 1
        self._hang_on_wait = False
        self._exit_code = -9
        self.returncode = -9

    def wait(self, timeout: float | None = None) -> int:
        # a hanging process models terminate being ignored, which drives the kill path
        if self._hang_on_wait:
            raise subprocess.TimeoutExpired(cmd="fake-app", timeout=timeout)
        return self.returncode if self.returncode is not None else 0


class FakeMcp:
    # fake transport recording the close call and serving canned tool results

    def __init__(self) -> None:
        self.closed = False
        self.tool_results = {"semantic.tree": "group - \nbutton ok OK\n"}

    def call_tool(self, name: str, arguments: dict | None = None):
        return self.tool_results.get(name, "")

    def close(self) -> None:
        self.closed = True


class FakeClient:
    # fake ui facade serving the canned screenshot the artifacts collect

    def __init__(self) -> None:
        self._mcp = FakeMcp()

    def screenshot(self) -> bytes:
        return b"png-bytes"


class ApplicationUnderTestChecks(unittest.TestCase):
    # shared construction of the application over the fake process and client

    def _make_app(self) -> tuple[AlborApplication, FakeProcess, FakeClient]:
        # build the application over the fake process and client
        process = FakeProcess()
        client = FakeClient()
        app = AlborApplication(process, client, Path("/tmp/descriptor.json"))
        return app, process, client


class AlborApplicationInstanceChecks(ApplicationUnderTestChecks):
    # the instance lifecycle runs against fakes, which keeps the checks free
    # of any running application

    def test_client_and_descriptor_are_exposed(self) -> None:
        # arrange
        app, _, client = self._make_app()
        # act / assert
        self.assertIs(app.client(), client)
        self.assertEqual(app.descriptor_path(), Path("/tmp/descriptor.json"))

    def test_is_running_follows_the_process_poll(self) -> None:
        # arrange: a live process reports running
        app, process, _ = self._make_app()
        self.assertTrue(app.is_running())
        # arrange: the process finishing flips the report
        process._exit_code = 0
        self.assertFalse(app.is_running())

    def test_get_by_id_returns_a_locator(self) -> None:
        # arrange
        app, _, _ = self._make_app()
        # act
        locator = app.get_by_id("ok")
        # assert
        self.assertIsInstance(locator, Locator)
        self.assertEqual(locator.node_id(), "ok")

    def test_stdout_and_stderr_text_are_read_back(self) -> None:
        # arrange: the captured streams carry canned content
        stdout = tempfile.TemporaryFile()
        stderr = tempfile.TemporaryFile()
        self.addCleanup(stdout.close)
        self.addCleanup(stderr.close)
        stdout.write(b"launched\n")
        stdout.seek(0)
        stderr.write(b"serving descriptor\n")
        stderr.seek(0)
        process = FakeProcess()
        client = FakeClient()
        app = AlborApplication(process, client, Path("/tmp/d.json"), stdout, stderr)
        # act / assert
        self.assertEqual(app.stdout_text(), "launched\n")
        self.assertEqual(app.stderr_text(), "serving descriptor\n")

    def test_stdout_text_is_empty_without_capture(self) -> None:
        # arrange: no files captured the streams
        app, _, _ = self._make_app()
        # act / assert
        self.assertEqual(app.stdout_text(), "")
        self.assertEqual(app.stderr_text(), "")


class AlborApplicationArtifactChecks(unittest.TestCase):

    def test_collect_artifacts_writes_every_file(self) -> None:
        # arrange: the app runs over fakes with a real descriptor file
        process = FakeProcess()
        client = FakeClient()
        stdout = tempfile.TemporaryFile()
        self.addCleanup(stdout.close)
        stdout.write(b"app output\n")
        stdout.seek(0)
        with tempfile.TemporaryDirectory() as tmp:
            descriptor = Path(tmp) / "descriptor.json"
            descriptor.write_text('{"socket": "/tmp/mcp.sock"}')
            app = AlborApplication(process, client, descriptor, stdout, None)
            # act
            directory = app.collect_artifacts(str(Path(tmp) / "test-case"))
            # assert: the screenshot, tree, descriptor and streams are captured
            root = Path(directory)
            self.assertEqual((root / "screenshot.png").read_bytes(), b"png-bytes")
            self.assertIn("button ok OK", (root / "tree.txt").read_text())
            self.assertIn("mcp.sock", (root / "descriptor.json").read_text())
            self.assertEqual((root / "stdout.txt").read_text(), "app output\n")
            self.assertEqual((root / "stderr.txt").read_text(), "")

    def test_collect_artifacts_survives_a_failing_screenshot(self) -> None:
        # arrange: the capture raises, which must not mask the other artifacts
        process = FakeProcess()

        class FailingClient(FakeClient):

            def screenshot(self) -> bytes:
                raise RuntimeError("no renderer")

        client = FailingClient()
        with tempfile.TemporaryDirectory() as tmp:
            descriptor = Path(tmp) / "descriptor.json"
            descriptor.write_text("{}")
            app = AlborApplication(process, client, descriptor)
            # act
            directory = app.collect_artifacts(str(Path(tmp) / "failing"))
            # assert: the tree artifact still landed
            self.assertIn("button ok OK", (Path(directory) / "tree.txt").read_text())


class AlborApplicationCloseChecks(ApplicationUnderTestChecks):

    def test_close_terminates_and_releases_every_resource(self) -> None:
        # arrange
        app, process, client = self._make_app()
        # act
        app.close()
        # assert: the process was terminated and every resource released
        self.assertEqual(process.terminated, 1)
        self.assertEqual(process.killed, 0)
        self.assertTrue(client._mcp.closed)
        self.assertFalse(app.is_running())

    def test_close_closes_the_transport_even_when_the_process_already_exited(self) -> None:
        # arrange: the process finished on its own, which the leak fix must still clean up
        process = FakeProcess(exit_code=0)
        client = FakeClient()
        app = AlborApplication(process, client, Path("/tmp/d.json"))
        # act
        app.close()
        # assert: the transport was closed despite the early return path
        self.assertTrue(client._mcp.closed)

    def test_close_kills_a_process_that_ignores_termination(self) -> None:
        # arrange: the process hangs on wait, which drives the kill path
        process = FakeProcess(hang_on_wait=True)
        client = FakeClient()
        app = AlborApplication(process, client, Path("/tmp/d.json"))
        # act
        app.close()
        # assert: terminate ran first, then the kill finished the job
        self.assertEqual(process.terminated, 1)
        self.assertEqual(process.killed, 1)
        self.assertTrue(client._mcp.closed)

    def test_context_manager_closes_on_exit(self) -> None:
        # arrange
        app, process, client = self._make_app()
        # act
        with app:
            self.assertTrue(app.is_running())
        # assert
        self.assertTrue(client._mcp.closed)
        self.assertEqual(process.terminated, 1)


class FakePopen:
    # fake spawn that records the argv and environment and writes the
    # descriptor serving line into the stream the launcher polls

    def __init__(self, argv, env=None, stdout=None, stderr=None, exit_before_serving: bool = False) -> None:
        # argv and env are recorded for the launch assertions
        self.argv = list(argv)
        self.env = dict(env or {})
        self.returncode = 3 if exit_before_serving else None
        self.pid = 9090

    def poll(self) -> int | None:
        return self.returncode

    def terminate(self) -> None:
        self.returncode = 0

    def wait(self, timeout: float | None = None) -> int:
        return self.returncode or 0


class FakeProbeSocket:
    # fake socket the readiness probe connects through, which succeeds
    # immediately like a bound endpoint would

    def __init__(self, family=None, kind=None) -> None:
        self.closed = False

    def connect(self, path: str) -> None:
        # a successful connect proves the endpoint accepts
        pass

    def close(self) -> None:
        self.closed = True


class FakeRefusingSocket(FakeProbeSocket):
    # fake socket whose connect fails, modeling an endpoint that never binds

    def connect(self, path: str) -> None:
        raise OSError("connection refused")


class FakeLaunchMcp:
    # fake transport the launcher constructs and the application later closes

    def __init__(self, socket_path, token) -> None:
        self.socket_path = socket_path
        self.token = token
        self.closed = False

    def connect(self) -> None:
        pass

    def close(self) -> None:
        self.closed = True


class LaunchChecks(unittest.TestCase):
    # the launcher is patched at the process, transport and probe boundaries,
    # which keeps the checks free of real sockets and threads

    def setUp(self) -> None:
        # arrange: a real descriptor the fake serving line points at
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self._descriptor = Path(self._tmp.name) / "host.json"
        token_path = Path(self._tmp.name) / "token"
        token_path.write_text("token-value")
        self._descriptor.write_text(json.dumps({"socket": str(Path(self._tmp.name) / "mcp.sock"), "token": str(token_path)}))
        # arrange: the fake process writes the serving line into stderr
        self._popen = None
        # arrange: the transport succeeds immediately
        self._mcp_connected = []

        self._mcp_patcher = mock.patch("albor_automation.albor_application.McpClient", self._make_mcp_factory())
        self._mcp_patcher.start()
        self.addCleanup(self._mcp_patcher.stop)
        # arrange: the readiness probe connects immediately
        self._probe_patcher = mock.patch("socket.socket", FakeProbeSocket)
        self._probe_patcher.start()
        self.addCleanup(self._probe_patcher.stop)

    def _make_mcp_factory(self):
        # a factory recording every constructed transport for the assertions
        def factory(socket_path, token):
            client = FakeLaunchMcp(socket_path, token)
            self._mcp_connected.append(client)
            return client

        return factory

    def _install_popen(self, exit_before_serving: bool = False, serving_stream: str = "stderr", serve: bool = True) -> None:
        # build the fake spawn bound to the descriptor and patch it in
        def factory(argv, env=None, stdout=None, stderr=None):
            process = FakePopen(argv, env, stdout, stderr, exit_before_serving)
            if serve and not exit_before_serving and serving_stream == "stderr" and stderr is not None:
                stderr.write(f"albor automation: serving {self._descriptor}\n".encode("utf-8"))
                stderr.seek(0)
            if serve and not exit_before_serving and serving_stream == "stdout" and stdout is not None:
                stdout.write(f"albor automation: serving {self._descriptor}\n".encode("utf-8"))
                stdout.seek(0)
            self._popen = process
            return process

        patcher = mock.patch("albor_automation.albor_application.subprocess.Popen", factory)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_launch_returns_a_connected_application(self) -> None:
        # arrange
        self._install_popen()
        # act
        app = launch("/fake/app", args=["--xyce", "/bin/echo"])
        # assert: the app is built over the spawned process
        self.assertIsInstance(app, AlborApplication)
        self.assertEqual(self._popen.argv, ["/fake/app", "--xyce", "/bin/echo"])
        self.assertTrue(self._mcp_connected)
        app.close()

    def test_launch_finds_the_descriptor_on_stdout(self) -> None:
        # arrange: the serving line lands on stdout instead of stderr
        self._install_popen(serving_stream="stdout")
        # act / assert: the launcher still connects
        app = launch("/fake/app")
        self.assertTrue(self._mcp_connected)
        app.close()

    def test_launch_adds_an_isolated_config_directory(self) -> None:
        # arrange: no override env, so the launcher isolates the configuration
        os.environ.pop("XDG_CONFIG_HOME", None)
        self.addCleanup(os.environ.pop, "XDG_CONFIG_HOME", None)
        self._install_popen()
        # act
        app = launch("/fake/app")
        # assert: the child env carries the isolated directory
        config_home = self._popen.env["XDG_CONFIG_HOME"]
        self.assertIn("xyce-studio-config", config_home)
        app.close()
        # the cleanup removed the isolated directory
        self.assertFalse(Path(config_home).exists())

    def test_launch_respects_the_seeded_config_override(self) -> None:
        # arrange: the caller seeds the configuration root, which the launcher must keep
        self._install_popen()
        # act
        app = launch("/fake/app", env={"XDG_CONFIG_HOME": "/seeded/root"})
        # assert: the override reached the child environment untouched
        self.assertEqual(self._popen.env["XDG_CONFIG_HOME"], "/seeded/root")
        app.close()

    def test_launch_forwards_environment_overrides(self) -> None:
        # arrange
        self._install_popen()
        # act
        app = launch("/fake/app", env={"ALBOR_TOKEN": "secret"})
        # assert
        self.assertEqual(self._popen.env["ALBOR_TOKEN"], "secret")
        app.close()

    def test_launch_without_isolation_keeps_the_environment(self) -> None:
        # arrange: config_dir=False skips the temporary configuration directory
        os.environ.pop("XDG_CONFIG_HOME", None)
        self.addCleanup(os.environ.pop, "XDG_CONFIG_HOME", None)
        self._install_popen()
        # act
        app = launch("/fake/app", config_dir=False)
        # assert: the child environment was not extended
        self.assertNotIn("XDG_CONFIG_HOME", self._popen.env)
        app.close()

    def test_launch_failure_cleans_up_the_spawned_process(self) -> None:
        # arrange: the process exits before the host served
        self._install_popen(exit_before_serving=True)
        # act / assert
        with self.assertRaises(ApplicationStartupError) as context:
            launch("/fake/app")
        self.assertIn("exited with code 3", str(context.exception))
        # the spawn was cleaned up through the launcher release path
        self.assertEqual(self._popen.returncode, 3)

    def test_launch_timeout_names_the_deadline(self) -> None:
        # arrange: the process stays alive but never serves
        self._install_popen(serve=False)
        # act / assert: the short timeout keeps the check fast
        with self.assertRaises(ApplicationStartupError) as context:
            launch("/fake/app", startup_timeout=0.3)
        self.assertIn("did not serve a descriptor within 0.3s", str(context.exception))


class DefaultExecutableChecks(unittest.TestCase):

    def test_environment_override_wins(self) -> None:
        # arrange
        os.environ["ALBOR_TEST_APPLICATION"] = "/override/app"
        self.addCleanup(os.environ.pop, "ALBOR_TEST_APPLICATION", None)
        # act / assert
        self.assertEqual(default_executable(), "/override/app")

    def test_default_falls_back_to_the_build_directory(self) -> None:
        # arrange: no override present
        os.environ.pop("ALBOR_TEST_APPLICATION", None)
        # act
        path = default_executable()
        # assert: the fallback points at the studio debug build
        self.assertIn("xyce-studio-next", path)


if __name__ == "__main__":
    unittest.main()

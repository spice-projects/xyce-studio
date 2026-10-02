import os
import shutil
import socket
import stat
import tempfile
import threading
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


def _encode_varint(value: int) -> bytes:
    # encode an integer as a protobuf base-128 varint
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def _encode_tag(field_number: int, wire_type: int) -> bytes:
    # encode a protobuf field tag
    return _encode_varint((field_number << 3) | wire_type)


def _encode_length_delimited(field_number: int, payload: bytes) -> bytes:
    # encode a length delimited protobuf field (strings, bytes, sub messages)
    return _encode_tag(field_number, 2) + _encode_varint(len(payload)) + payload


def _encode_string_field(field_number: int, value: str) -> bytes:
    # encode a protobuf string field
    return _encode_length_delimited(field_number, value.encode())


def _encode_varint_field(field_number: int, value: int) -> bytes:
    # encode a protobuf varint field
    return _encode_tag(field_number, 0) + _encode_varint(value)


def _decode_fields(payload: bytes):
    # iterate protobuf wire fields yielding (field_number, wire_type, value) tuples
    index = 0
    while index < len(payload):
        key = 0
        shift = 0
        while True:
            byte = payload[index]
            index += 1
            key |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                break
        field_number = key >> 3
        wire_type = key & 0x07
        if wire_type == 0:
            value = 0
            shift = 0
            while True:
                byte = payload[index]
                index += 1
                value |= (byte & 0x7F) << shift
                shift += 7
                if not byte & 0x80:
                    break
            yield field_number, wire_type, value
        elif wire_type == 2:
            length = 0
            shift = 0
            while True:
                byte = payload[index]
                index += 1
                length |= (byte & 0x7F) << shift
                shift += 7
                if not byte & 0x80:
                    break
            value = payload[index:index + length]
            index += length
            yield field_number, wire_type, value
        else:
            raise ValueError(f"unsupported wire type {wire_type}")


def _extract_any_type_url(request_payload: bytes) -> str:
    # resolve the type url of the any payload carried by an ApiRequest envelope
    for field_number, _, value in _decode_fields(request_payload):
        if field_number == 2:
            for any_field, _, any_value in _decode_fields(value):
                if any_field == 1:
                    return any_value.decode()
    return ""


def _extract_request_token(request_payload: bytes) -> str:
    # resolve the kicad token carried by an ApiRequest header
    for field_number, _, value in _decode_fields(request_payload):
        if field_number == 1:
            for header_field, _, header_value in _decode_fields(value):
                if header_field == 1:
                    return header_value.decode()
    return ""


MOCK_NETLIST = """\
.title KiCad schematic
* Simple RLC Series Circuit
* Simulation directives
.TRAN 1u 20m 0
.PRINT TRAN FORMAT=RAW FILE=kicad-run.raw V(*) I(*) P(*)
V1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)
R1 IN N1 100
L1 N1 N2 10mH
C1 N2 0 1uF
.end
"""


class MockKiCadApiServer:
    # minimal in-process KiCad API server speaking the protobuf envelope over nng

    TYPE_URL_PREFIX = "type.googleapis.com/"

    def __init__(self, kicad_cli_path: str) -> None:
        # kicad_cli_path is the fake kicad-cli binary reported to the plugin
        self._kicad_cli_path = kicad_cli_path
        self._rep = None
        self._thread = None
        self._running = False
        self._port = 0

    def start(self) -> None:
        # bind a rep socket on a free localhost port and serve requests in the background
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind(("127.0.0.1", 0))
            self._port = probe.getsockname()[1]
        import pynng
        self._rep = pynng.Rep0(listen=f"tcp://127.0.0.1:{self._port}")
        self._running = True
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        # close the socket and join the serving thread
        self._running = False
        if self._rep is not None:
            # close may raise when the socket is already gone from a failed recv
            try:
                self._rep.close()
            except Exception:
                pass
            self._rep = None
        if self._thread is not None:
            self._thread.join(timeout=5.0)
            self._thread = None

    def socket_url(self) -> str:
        # nng url handed to the application through KICAD_API_SOCKET
        return f"tcp://127.0.0.1:{self._port}"

    def _serve(self) -> None:
        # reply to each request until the server is stopped
        while self._running:
            try:
                request = self._rep.recv()
            except Exception:
                return
            try:
                self._rep.send(self._handle(request))
            except Exception:
                return

    def _handle(self, request: bytes) -> bytes:
        # build the ApiResponse envelope for a single ApiRequest envelope
        type_url = _extract_any_type_url(request)
        token = _extract_request_token(request)
        if type_url.endswith("kiapi.common.commands.GetKiCadBinaryPath"):
            payload = _encode_string_field(1, self._kicad_cli_path)
            message_type = "kiapi.common.commands.PathResponse"
        elif type_url.endswith("kiapi.common.commands.GetVersion"):
            # KiCadVersion{major=8, minor=0, patch=0, full_version="8.0.0"}
            version = _encode_varint_field(1, 8) + _encode_varint_field(2, 0) + _encode_varint_field(3, 0) + _encode_string_field(4, "8.0.0")
            payload = _encode_length_delimited(1, version)
            message_type = "kiapi.common.commands.GetVersionResponse"
        else:
            payload = b""
            message_type = "kiapi.common.commands.Empty"
        any_message = _encode_string_field(1, self.TYPE_URL_PREFIX + message_type)
        if payload:
            any_message += _encode_length_delimited(2, payload)
        status = _encode_varint_field(1, 1)
        header = _encode_string_field(1, token)
        return _encode_length_delimited(1, header) + _encode_length_delimited(2, status) + _encode_length_delimited(3, any_message)


class KiCadPluginReconfigureChecks(unittest.TestCase):

    def test_second_run_uses_the_edited_transient_parameters(self) -> None:
        # arrange: resolve the xyce executable
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: build a fake KiCad project holding a schematic with the simulation directives
        with tempfile.TemporaryDirectory(prefix="xyce-kicad-project-") as project_root, tempfile.TemporaryDirectory(prefix="xyce-kicad-cli-") as cli_root:
            project_dir = Path(project_root)
            schematic = project_dir / "demo.kicad_sch"
            schematic.write_text("(kicad_sch (version 8))")
            (project_dir / "demo.kicad_pro").write_text("{}")
            # arrange: build a fake kicad-cli exporting the schematic spice netlist
            kicad_cli = Path(cli_root) / "kicad-cli"
            kicad_cli.write_text("#!/bin/sh\nout=\"\"\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--output\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\ncat > \"$out\" <<'EOF'\n" + MOCK_NETLIST + "EOF\nexit 0\n")
            kicad_cli.chmod(kicad_cli.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
            # arrange: start the mock KiCad API server the plugin dials on launch
            server = MockKiCadApiServer(str(kicad_cli))
            server.start()
            try:
                # arrange: launch the application as a KiCad plugin session
                environment = {
                    "KICAD_API_SOCKET": server.socket_url(),
                    "KICAD_API_TOKEN": "integration-test-token",
                    "KIPRJMOD": str(project_dir),
                }
                with TestSession(launch(env=environment, args=["--xyce", xyce]), self.id()) as app:
                    # arrange: locate the status bar text and the toolbar tools
                    status = app.get_by_id("MainWindow::status-message")
                    tools = app.get_by_type("ToolbarButton")
                    # step 1: run the simulation and wait for the final status message
                    tools.nth(5).click()
                    expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                    # step 2: open the configure simulation dialog from the toolbar
                    tools.nth(6).click()
                    # arrange: locate the transient analysis form fields
                    fields = app.get_by_type("LineEdit")
                    fields.nth(0).wait_for_exists()
                    # assert: the dialog shows the schematic directive values before the edit
                    expect(fields.nth(0)).to_have_text("1u")
                    expect(fields.nth(1)).to_have_text("20m")
                    # step 3: edit the simulation end time and accept the dialog
                    fields.nth(1).fill("25m")
                    root = app.client().get_window_properties()["rootElementHandle"]
                    ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
                    app.client().click_element(ok)
                    # assert: the dialog closed
                    fields.nth(0).wait_for_gone()
                    # step 4: KiCad autosaves the schematic, changing its modification time;
                    # set the mtime deterministically into the future so the re-export is
                    # guaranteed even on filesystems with coarse timestamp resolution
                    first_export_time = schematic.stat().st_mtime
                    os.utime(schematic, (first_export_time + 10.0, first_export_time + 10.0))
                    # step 5: run the simulation again and wait for the final status message
                    tools.nth(5).click()
                    expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                    # step 6: show the netlist view to inspect the effective directives
                    tools.nth(2).click()
                    editor = app.get_by_id("NetlistEditor::input")
                    editor.wait_for_exists()
                    # assert: the netlist shows the edited transient parameters
                    tran_lines = [line for line in editor.text().splitlines() if line.strip().upper().startswith(".TRAN")]
                    self.assertEqual(tran_lines, [".TRAN 1u 25m 0"])
            finally:
                server.stop()


if __name__ == "__main__":
    unittest.main()

import os
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class MeasureDirectiveChecks(unittest.TestCase):
    # .MEASURE / .MEAS directives survive configuration and reach Xyce

    def test_op_and_tran_netlist_preserves_measure_on_run(self) -> None:
        # arrange: a netlist with both .OP and .TRAN plus a TRAN measure
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\ncat \"$1\"\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "op-tran-meas.cir"
            netlist.write_text("* OP + TRAN measure\nV1 1 0 1\nR1 1 0 1k\n.OP\n.TRAN 1u 1m\n.MEASURE TRAN vmax MAX V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step: open and accept the simulation parameters dialog
                app.get_by_id("main_configure").click()
                app.get_by_id("simulation_parameters_ok").wait_for_exists()
                app.get_by_id("simulation_parameters_ok").click()
                app.get_by_id("simulation_parameters_ok").wait_for_gone()
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the measure directive reaches Xyce
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                app.wait_for_condition(lambda: ".MEASURE TRAN vmax MAX V(1)" in log.value(), timeout=10.0, message="expected TRAN measure in run netlist")

    def test_ac_and_tran_netlist_preserves_measure_on_run(self) -> None:
        # arrange: a netlist with both .AC and .TRAN plus a TRAN measure
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\ncat \"$1\"\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "ac-tran-meas.cir"
            netlist.write_text("* AC + TRAN measure\nV1 1 0 AC 1\nR1 1 0 1k\n.AC DEC 10 1 1e6\n.TRAN 1u 1m\n.MEASURE TRAN vmax MAX V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step: open and accept the simulation parameters dialog
                app.get_by_id("main_configure").click()
                app.get_by_id("simulation_parameters_ok").wait_for_exists()
                app.get_by_id("simulation_parameters_ok").click()
                app.get_by_id("simulation_parameters_ok").wait_for_gone()
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the measure directive reaches Xyce
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                app.wait_for_condition(lambda: ".MEASURE TRAN vmax MAX V(1)" in log.value(), timeout=10.0, message="expected TRAN measure in run netlist")

    def test_noise_netlist_preserves_measure_on_run(self) -> None:
        # arrange: a NOISE netlist with a NOISE measure
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\ncat \"$1\"\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "noise-meas.cir"
            netlist.write_text("* NOISE measure\nV1 1 0 AC 1\nR1 1 0 1k\n.NOISE V(1) V1 DEC 10 1 1e6\n.MEASURE NOISE nmax MAX ONOISE\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step: open and accept the simulation parameters dialog
                app.get_by_id("main_configure").click()
                app.get_by_id("simulation_parameters_ok").wait_for_exists()
                app.get_by_id("simulation_parameters_ok").click()
                app.get_by_id("simulation_parameters_ok").wait_for_gone()
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the NOISE measure directive reaches Xyce
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                app.wait_for_condition(lambda: ".MEASURE NOISE nmax MAX ONOISE" in log.value(), timeout=10.0, message="expected NOISE measure in run netlist")

    def test_analysisless_netlist_preserves_measure_when_configured_for_run(self) -> None:
        # arrange: a netlist with no analysis and a TRAN measure
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\ncat \"$1\"\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "no-analysis-meas.cir"
            netlist.write_text("* No analysis measure\nV1 1 0 1\nR1 1 0 1k\n.MEASURE TRAN vmax MAX V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step: run opens the simulation parameters dialog
                app.get_by_id("main_run").click()
                tran_tab = app.get_by_id("simulation_tab_transient")
                tran_tab.wait_for_exists()
                # step: switch to the transient tab and verify the measure field is populated
                tran_tab.click()
                measure = app.get_by_id("simulation_measure")
                measure.wait_for_exists()
                expect(measure).to_have_value(".MEASURE TRAN vmax MAX V(1)")
                # step: fill required transient timing fields
                app.get_by_id("simulation_tran_initial_step").fill("1u")
                app.get_by_id("simulation_tran_final_time").fill("1m")
                # act: accept the transient configuration
                app.get_by_id("simulation_parameters_ok").click()
                measure.wait_for_gone()
                # step: run after configuring the analysis
                app.get_by_id("main_run").click()
                # assert: the measure directive reaches Xyce
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                app.wait_for_condition(lambda: ".MEASURE TRAN vmax MAX V(1)" in log.value(), timeout=10.0, message="expected measure in run netlist")

    def test_fft_and_meas_qualifiers_and_validation_in_dialog(self) -> None:
        # arrange: a transient netlist carrying FFT and .MEAS directives
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "fft-meas.cir"
            netlist.write_text("* FFT measure\nV1 1 0 1\nR1 1 0 1k\n.TRAN 1u 1m\n.MEASURE FFT e ENOB V(1)\n.MEAS TRAN vout MAX V(out) DEFAULT_VAL=0\n.END\n")
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step: open the transient page and verify both measures appear verbatim
                app.get_by_id("main_configure").click()
                measure = app.get_by_id("simulation_measure")
                measure.wait_for_exists()
                expect(measure).to_have_value(".MEASURE FFT e ENOB V(1)\n.MEAS TRAN vout MAX V(out) DEFAULT_VAL=0")
                # step: enter an invalid measure line and verify accept keeps the dialog open
                measure.fill(".MEASURE BAD")
                app.get_by_id("simulation_parameters_ok").click()
                app.get_by_id("simulation_parameters_ok").wait_for_exists()
                expect(app.get_by_id("simulation_parameters_error")).to_have_name("Invalid measure directive")
                # act: cancel
                app.get_by_id("simulation_parameters_cancel").click()
                app.get_by_id("simulation_parameters_ok").wait_for_gone()

    def test_fft_measure_survives_run(self) -> None:
        # arrange: a transient netlist with an FFT measure
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\ncat \"$1\"\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "fft-run.cir"
            netlist.write_text("* FFT measure run\nV1 1 0 1\nR1 1 0 1k\n.TRAN 1u 1m\n.MEASURE FFT e ENOB V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the FFT measure reaches Xyce
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                app.wait_for_condition(lambda: ".MEASURE FFT e ENOB V(1)" in log.value(), timeout=10.0, message="expected FFT measure in run netlist")

    def test_edited_measure_reaches_run_netlist(self) -> None:
        # arrange: a transient netlist
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\ncat \"$1\"\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "edit-meas.cir"
            netlist.write_text("* Edit measure\nV1 1 0 1\nR1 1 0 1k\n.TRAN 1u 1m\n.MEASURE TRAN vold MAX V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step: open configuration and edit the measure
                app.get_by_id("main_configure").click()
                measure = app.get_by_id("simulation_measure")
                measure.wait_for_exists()
                measure.fill(".MEASURE TRAN vnew MAX V(1)")
                app.get_by_id("simulation_parameters_ok").click()
                measure.wait_for_gone()
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the edited measure reaches Xyce
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                app.wait_for_condition(lambda: ".MEASURE TRAN vnew MAX V(1)" in log.value(), timeout=10.0, message="expected edited measure in run netlist")


if __name__ == "__main__":
    unittest.main()

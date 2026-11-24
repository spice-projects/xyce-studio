import os
import shutil
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class SmithChartChecks(unittest.TestCase):
    # a stepped touchstone dataset plots on the Smith plane and keeps the step tool

    def test_the_step_tool_opens_from_the_smith_chart_context_menu(self) -> None:
        # arrange: a fake Xyce that writes a three-step touchstone beside its netlist
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lin-simple-with-step-01.s2p"
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                f"cat > '{output}' <<'EOF'\n"
                "[Version] 2.0\n"
                "# Hz S RI R 50\n"
                "[Number of Ports] 2\n"
                "[Two-Port Data Order] 12_21\n"
                "[Number of Frequencies] 2\n"
                "[Reference] 50 50\n"
                "[Network Data]\n"
                "1.0e+08 0.20 0.00 0.80 0.00 0.80 0.00 -0.20 0.00\n"
                "2.0e+08 0.10 0.10 0.70 -0.10 0.70 -0.10 -0.10 0.10\n"
                "1.0e+08 0.21 0.00 0.79 0.00 0.79 0.00 -0.21 0.00\n"
                "2.0e+08 0.11 0.10 0.69 -0.10 0.69 -0.10 -0.11 0.10\n"
                "1.0e+08 0.22 0.00 0.78 0.00 0.78 0.00 -0.22 0.00\n"
                "2.0e+08 0.12 0.10 0.68 -0.10 0.68 -0.10 -0.12 0.10\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            # arrange: a scratch copy of the repository netlist that declares three steps
            source = Path(__file__).resolve().parents[1] / "netlists" / "lin-simple-with-step-01.cir"
            netlist = Path(tmp) / "lin-simple-with-step-01.cir"
            shutil.copy(source, netlist)
            # arrange: launch the application with the fake simulator
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the produced touchstone opens as a chart tab and the run finishes
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")
                # act: select the smith companion tab
                app.get_by_id("document_tabs").click("select-tab-2")
                # assert: the smith chart plots the stepped touchstone
                expect(chart).to_have_name("2 datasets, 1 smith chart, active: Smith Chart")
                # act: open the step tool through the smith chart action
                chart.click("chart-0-step-tool")
                # assert: the dialog lists all three steps the run carried
                app.get_by_id("step_tool_check_0").wait_for_exists()
                app.get_by_id("step_tool_check_1").wait_for_exists()
                app.get_by_id("step_tool_check_2").wait_for_exists()
                expect(app.get_by_id("step_tool_count")).to_have_name("Selected 3 / 3")
                # act: dismiss the dialog
                app.get_by_id("step_tool_cancel").click()
                # assert: the dialog closes without changing the chart
                app.get_by_id("step_tool_check_0").wait_for_gone()
                expect(chart).to_have_name("2 datasets, 1 smith chart, active: Smith Chart")


if __name__ == "__main__":
    unittest.main()

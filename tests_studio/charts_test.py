import io
import os
import tempfile
import time
import unittest
from pathlib import Path

from PIL import Image

from albor_automation import TestSession, expect, launch


class ChartsPanelChecks(unittest.TestCase):
    # the output dataset argument opens the native chart panel, and the document
    # strip swaps the body between the waveform panel and the netlist editor

    def test_dataset_argument_opens_the_charts_panel(self) -> None:
        # arrange: a repository output file and a session scoped to this test
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # assert: the chart surface and the toolbar action are present
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 line chart")
            # assert: the document strip lists the netlist and the dataset
            expect(app.get_by_id("document_tabs")).to_have_name("Documents, active: tran-simple-01")
            # assert: the chart panel replaced the netlist editor in the body
            expect(app.get_by_id("netlist_editor")).not_().to_exist()

    def test_the_document_tabs_switch_between_the_netlist_and_the_charts(self) -> None:
        # arrange: start on the chart panel through the dataset argument
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # step 1: select the netlist tab
            app.get_by_id("document_tabs").click("select-tab-0")
            # assert: the editor mounts and the chart panel leaves the body
            app.get_by_id("netlist_editor").wait_for_exists()
            expect(app.get_by_id("charts_view")).not_().to_exist()
            # step 2: select the dataset tab
            app.get_by_id("document_tabs").click("select-tab-1")
            # assert: the chart panel returns and the editor unmounts
            app.get_by_id("charts_view").wait_for_exists()
            expect(app.get_by_id("netlist_editor")).not_().to_exist()

    def test_the_document_strip_starts_on_the_netlist(self) -> None:
        # arrange: a plain launch with no output file
        with TestSession(launch(), self.id()) as app:
            # assert: the strip lists only the netlist and the editor keeps the body
            expect(app.get_by_id("document_tabs")).to_have_name("Documents, active: Netlist")
            expect(app.get_by_id("netlist_editor")).to_exist()
            expect(app.get_by_id("charts_view")).not_().to_exist()

    def test_two_dataset_arguments_open_two_tabs(self) -> None:
        # arrange: the same output file offered twice, which the presenter keeps as two tabs
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset), "--dataset", str(dataset)]), self.id()) as app:
            # assert: the chart surface reports the two dataset tabs
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("2 datasets, 1 line chart, active: tran-simple-01 (2)")

    def test_a_successful_run_auto_opens_the_charts(self) -> None:
        # arrange: a fake Xyce that writes a csv analysis output beside its netlist
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "2,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            # arrange: launch the application with the fake simulator
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: run the simulation
                app.get_by_id("main_run").click()
                # assert: the produced csv opens as a chart tab
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("1 line chart")
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")

    def test_a_run_uses_the_plot_directives_for_its_charts(self) -> None:
        # arrange: a fake Xyce whose table carries the two declared quantities
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1),I(L1)\n"
                "0,0,0\n"
                "1,1,2\n"
                "2,2,4\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(*) I(*)\n.PLOT V(1)\n.PLOT I(L1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the two .PLOT groups build two charts
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("2 line charts")
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")

    def test_touchstone_dataset_opens_a_smith_chart(self) -> None:
        # arrange: a repository touchstone file and a session scoped to this test
        dataset = Path(__file__).resolve().parent / "fixtures" / "lin-simple-01.s2p"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # assert: the chart surface names the single smith chart
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 smith chart")

    def test_switching_tabs_updates_the_active_dataset_name(self) -> None:
        # arrange: two dataset tabs where the second is active
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset), "--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("2 datasets, 1 line chart, active: tran-simple-01 (2)")
            # act: select the first dataset tab through the semantic action
            app.get_by_id("document_tabs").click("select-tab-1")
            # assert: the active dataset follows the selection
            expect(chart).to_have_name("2 datasets, 1 line chart, active: tran-simple-01")

    def test_closing_the_last_user_dataset_returns_to_the_editor(self) -> None:
        # arrange: one closable user dataset
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 line chart")
            # act: close the only dataset tab through the semantic action
            app.get_by_id("document_tabs").click("close-tab-1")
            # assert: the editor returns to the body and the chart panel leaves it
            app.get_by_id("netlist_editor").wait_for_exists()
            expect(app.get_by_id("charts_view")).not_().to_exist()

    def test_the_chart_context_menu_commands_change_the_panel(self) -> None:
        # arrange: one dataset whose chart offers the context menu commands
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # arrange: the chart panel is the body
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 line chart")
            # act: add a chart through the menu trigger the automation reaches
            chart.click("chart-0-add-chart")
            # assert: the empty chart joins the panel
            expect(chart).to_have_name("2 line charts")
            # act: delete the added chart through the same menu path
            chart.click("chart-1-delete-chart")
            # assert: the original chart remains
            expect(chart).to_have_name("1 line chart")

    def test_the_plot_selection_dialog_edits_a_chart(self) -> None:
        # arrange: one dataset whose chart plots two expressions
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # arrange: open the dialog through the chart menu trigger
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            chart.click("chart-0-add-remove-plots")
            app.get_by_id("add_plot_filter").wait_for_exists()
            # assert: the grid carries the two plotted expressions as selected chips
            expect(app.get_by_id("add_plot_card_0")).to_have_name("V(1)")
            expect(app.get_by_id("add_plot_card_1")).to_have_name("V(2)")
            # act: filter the grid down to one expression
            app.get_by_id("add_plot_filter").fill("V(1)")
            # assert: the other chip leaves the grid
            expect(app.get_by_id("add_plot_card_1")).not_().to_exist()
            # act: clear the filter and deselect the first expression
            app.get_by_id("add_plot_filter").fill("")
            app.get_by_id("add_plot_card_0").click("toggle")
            # step: apply the selection
            app.get_by_id("add_plot_ok").click()
            app.get_by_id("add_plot_filter").wait_for_gone()
            # act: reopen the dialog over the edited chart
            chart.click("chart-0-add-remove-plots")
            app.get_by_id("add_plot_filter").wait_for_exists()
            # step: show only the selected expressions
            app.get_by_id("add_plot_breadcrumb").click("selected-only")
            # assert: the chart kept the one accepted expression
            expect(app.get_by_id("add_plot_card_0")).to_have_name("V(2)")
            expect(app.get_by_id("add_plot_card_1")).not_().to_exist()
            # step: leave the selected view and add a custom expression
            app.get_by_id("add_plot_breadcrumb").click("selected-only")
            app.get_by_id("add_plot_custom").fill("V(1) * 2")
            app.get_by_id("add_plot_add").click()
            # assert: the custom expression joins the grid selected
            expect(app.get_by_id("add_plot_card_2")).to_have_name("V(1) * 2")
            # step: apply and reopen
            app.get_by_id("add_plot_ok").click()
            app.get_by_id("add_plot_filter").wait_for_gone()
            chart.click("chart-0-add-remove-plots")
            app.get_by_id("add_plot_filter").wait_for_exists()
            app.get_by_id("add_plot_breadcrumb").click("selected-only")
            # assert: the chart kept the custom expression beside the remaining signal
            expect(app.get_by_id("add_plot_card_0")).to_have_name("V(2)")
            expect(app.get_by_id("add_plot_card_1")).to_have_name("V(1) * 2")
            app.get_by_id("add_plot_cancel").click()
            app.get_by_id("add_plot_filter").wait_for_gone()

    def test_a_refused_custom_expression_keeps_the_dialog_open(self) -> None:
        # arrange: one dataset whose chart plots two expressions
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            chart.click("chart-0-add-remove-plots")
            app.get_by_id("add_plot_filter").wait_for_exists()
            # act: add an expression the parser refuses
            app.get_by_id("add_plot_custom").fill("V(1) *")
            app.get_by_id("add_plot_add").click()
            # assert: the dialog stays open with the message and the grid unchanged
            expect(app.get_by_id("add_plot_error")).to_have_name("The expression could not be evaluated")
            expect(app.get_by_id("add_plot_card_0")).to_have_name("V(1)")
            # step: cancel the dialog
            app.get_by_id("add_plot_cancel").click()
            app.get_by_id("add_plot_filter").wait_for_gone()

    def test_a_smith_plot_selection_offers_no_custom_expression(self) -> None:
        # arrange: a touchstone dataset the charts show on the smith plane
        dataset = Path(__file__).resolve().parent / "fixtures" / "lin-simple-01.s2p"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 smith chart")
            # act: open the plot selection dialog
            chart.click("chart-0-add-remove-plots")
            app.get_by_id("add_plot_filter").wait_for_exists()
            # assert: only the diagonal entries are offered and no custom row shows
            expect(app.get_by_id("add_plot_card_0")).to_have_name("S11")
            expect(app.get_by_id("add_plot_card_1")).to_have_name("S22")
            expect(app.get_by_id("add_plot_custom")).not_().to_exist()
            # step: cancel the dialog
            app.get_by_id("add_plot_cancel").click()
            app.get_by_id("add_plot_filter").wait_for_gone()

    def test_moving_a_chart_through_the_semantic_action_keeps_the_stack(self) -> None:
        # arrange: one dataset that opens its chart
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 line chart")
            # act: add a second chart
            chart.click("chart-0-add-chart")
            expect(chart).to_have_name("2 line charts")
            # act: move the first chart down, then the one below it back up
            chart.click("chart-0-move-chart-down")
            chart.click("chart-1-move-chart-up")
            # assert: both charts survive the reorder round trip
            expect(chart).to_have_name("2 line charts")

    def test_the_step_tool_edits_the_chart_steps(self) -> None:
        # arrange: a dataset whose sweep restarts, so it carries two steps
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-stepped-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 line chart")
            # act: open the step tool through the chart action
            chart.click("chart-0-step-tool")
            app.get_by_id("step_tool_check_0").wait_for_exists()
            app.get_by_id("step_tool_check_1").wait_for_exists()
            expect(app.get_by_id("step_tool_count")).to_have_name("Selected 2 / 2")
            # act: keep only the second step
            app.get_by_id("step_tool_clear_all").click()
            app.get_by_id("step_tool_check_1").click("toggle")
            expect(app.get_by_id("step_tool_count")).to_have_name("Selected 1 / 2")
            app.get_by_id("step_tool_ok").click()
            app.get_by_id("step_tool_check_0").wait_for_gone()
            # assert: the chart survives the accepted selection
            expect(chart).to_have_name("1 line chart")
            # act: reopen the dialog over the edited chart
            chart.click("chart-0-step-tool")
            app.get_by_id("step_tool_check_0").wait_for_exists()
            # assert: the chart kept the accepted step
            expect(app.get_by_id("step_tool_count")).to_have_name("Selected 1 / 2")
            app.get_by_id("step_tool_cancel").click()
            app.get_by_id("step_tool_check_0").wait_for_gone()

    def test_closing_the_active_dataset_selects_its_neighbour(self) -> None:
        # arrange: two dataset tabs where the second is active
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset), "--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("2 datasets, 1 line chart, active: tran-simple-01 (2)")
            # act: close the active dataset tab
            app.get_by_id("document_tabs").click("close-tab-2")
            # assert: the remaining dataset takes the body and the canvas names it
            expect(chart).to_have_name("1 line chart")
            expect(app.get_by_id("netlist_editor")).not_().to_exist()

    def test_closing_an_inactive_dataset_keeps_the_active_one(self) -> None:
        # arrange: three dataset tabs
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset), "--dataset", str(dataset), "--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            # step: select the first dataset
            app.get_by_id("document_tabs").click("select-tab-1")
            expect(chart).to_have_name("3 datasets, 1 line chart, active: tran-simple-01")
            # act: close the second dataset, which is not active
            app.get_by_id("document_tabs").click("close-tab-2")
            # assert: the first dataset stays active
            expect(chart).to_have_name("2 datasets, 1 line chart, active: tran-simple-01")

    def test_closing_a_dataset_while_the_netlist_is_active_keeps_the_netlist(self) -> None:
        # arrange: two dataset tabs
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset), "--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            # step: return to the netlist tab
            app.get_by_id("document_tabs").click("select-tab-0")
            app.get_by_id("netlist_editor").wait_for_exists()
            # act: close a dataset while the netlist holds the body
            app.get_by_id("document_tabs").click("close-tab-1")
            # assert: the netlist stays visible and the remaining dataset is hidden
            expect(app.get_by_id("document_tabs")).to_have_name("Documents, active: Netlist")
            app.get_by_id("netlist_editor").wait_for_exists()
            expect(app.get_by_id("charts_view")).not_().to_exist()
            # step: the remaining dataset still opens
            app.get_by_id("document_tabs").click("select-tab-1")
            app.get_by_id("charts_view").wait_for_exists()
            expect(app.get_by_id("charts_view")).to_have_name("1 line chart")

    def test_a_finished_run_selects_the_primary_tab(self) -> None:
        # arrange: a fake Xyce that writes a csv analysis output beside its netlist
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "2,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the produced csv opens as the active document tab
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("1 line chart")
                # step: the netlist tab and the run tab switch the body both ways
                app.get_by_id("document_tabs").click("select-tab-0")
                app.get_by_id("netlist_editor").wait_for_exists()
                app.get_by_id("document_tabs").click("select-tab-1")
                app.get_by_id("charts_view").wait_for_exists()

    def test_a_click_in_the_chart_keeps_the_document_strip(self) -> None:
        # arrange: one dataset whose chart fills the body
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            # the first frame settles before the comparison
            time.sleep(0.3)
            # the top band carries the toolbar and the document strip
            before_image = Image.open(io.BytesIO(app.client().screenshot())).convert("RGB")
            before_top = before_image.crop((0, 0, before_image.width, min(200, before_image.height))).tobytes()
            # act: press the center of the first plot, which invalidates the canvas
            chart.click("press-center")
            time.sleep(0.3)
            # assert: the toolbar and the document strip are pixel identical after the press
            after_image = Image.open(io.BytesIO(app.client().screenshot())).convert("RGB")
            after_top = after_image.crop((0, 0, after_image.width, min(200, after_image.height))).tobytes()
            self.assertEqual(before_top, after_top)
            # assert: both the strip and the canvas are still live
            expect(app.get_by_id("document_tabs")).to_have_name("Documents, active: tran-simple-01")
            expect(chart).to_have_name("1 line chart")

    def test_the_fft_dialog_appends_a_frequency_dataset(self) -> None:
        # arrange: a transient dataset whose chart plots two real signals
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            # act: open the FFT dialog through the chart action
            chart.click("chart-0-calculate-fft")
            app.get_by_id("fft_ok").wait_for_exists()
            expect(app.get_by_id("fft_card_0")).to_have_name("V(1)")
            expect(app.get_by_id("fft_card_1")).to_have_name("V(2)")
            # act: accept the default transform
            app.get_by_id("fft_ok").click()
            app.get_by_id("fft_ok").wait_for_gone()
            # assert: the transform opens as the active dataset tab
            expect(chart).to_have_name("2 datasets, 2 line charts, active: FFT: Hanning, 0–56833 Hz")
            expect(app.get_by_id("document_tabs")).to_have_name("Documents, active: FFT: Hanning, 0–56833 Hz")

    def test_an_unsupported_dataset_argument_keeps_the_editor(self) -> None:
        # arrange: a text file the charts cannot parse and a session scoped to this test
        with TestSession(launch(args=["--dataset", __file__]), self.id()) as app:
            # assert: the shell stays on the editor and no chart panel is revealed
            expect(app.get_by_id("netlist_editor")).to_exist()
            expect(app.get_by_id("charts_view")).not_().to_exist()

    def test_a_run_with_an_open_dataset_adds_a_primary_tab(self) -> None:
        # arrange: a fake Xyce, a print netlist and a user dataset already open
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "2,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist), "--dataset", str(dataset)]), self.id()) as app:
                # act: run the simulation over the open user dataset
                app.get_by_id("main_run").click()
                # assert: the run adds its primary tab beside the user dataset
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("2 datasets, 1 line chart, active: tran.cir")

    def test_a_second_run_keeps_one_primary_tab(self) -> None:
        # arrange: a fake Xyce and a print netlist
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "2,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: run once
                app.get_by_id("main_run").click()
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("1 line chart")
                # step 2: run again
                app.get_by_id("main_run").click()
                # assert: the rerun repoints the primary tab instead of adding one
                expect(chart).to_have_name("1 line chart")
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")

    def test_a_run_copies_the_requested_print_destination(self) -> None:
        # arrange: a fake Xyce, a print netlist with a FILE= destination and a workspace
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "2,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV FILE=requested.csv V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the run opens its chart and copies the produced file to the requested name
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("1 line chart")
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")
                copied = Path(tmp) / "requested.csv"
                self.assertTrue(copied.exists())

    def test_a_failed_run_does_not_open_the_charts(self) -> None:
        # arrange: a fake Xyce that writes an output but exits with an error
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "EOF\n"
                "exit 2\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the failing simulation
                app.get_by_id("main_run").click()
                # assert: the strip still lists only the netlist and the editor keeps the body
                expect(app.get_by_id("main_status")).to_have_name("Simulation failed with exit code 2")
                expect(app.get_by_id("document_tabs")).to_have_name("Documents, active: Netlist")
                expect(app.get_by_id("netlist_editor")).to_exist()
                expect(app.get_by_id("charts_view")).not_().to_exist()

    def test_a_fourier_output_opens_as_its_results_table(self) -> None:
        # arrange: a Fourier analysis output and a session scoped to this test
        dataset = Path(__file__).resolve().parent / "fixtures" / "four-simple-01.four0"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # assert: the results table replaces the chart canvas
            table = app.get_by_id("fourier_results_view")
            table.wait_for_exists()
            expect(table).to_have_name("Fourier results: FOUR: 1000 Hz")
            expect(app.get_by_id("charts_view")).not_().to_exist()
            # assert: the strip lists the netlist and the Fourier dataset
            expect(app.get_by_id("document_tabs")).to_have_name("Documents, active: FOUR: 1000 Hz")
            # assert: every signal block announces its own THD and its analysis settings
            expect(app.get_by_id("fourier_block_0")).to_have_name("V(1)  -  THD: 28.64 %")
            expect(app.get_by_id("fourier_block_1")).to_have_name("V(2)  -  THD: 2.291 %")
            expect(app.get_by_id("fourier_block_0_settings")).to_have_name("No. Harmonics: 5  -  Gridsize: 200  -  Cubic Spline")
            # assert: the second block carries its own settings strip
            expect(app.get_by_id("fourier_block_1_settings")).to_have_name("No. Harmonics: 5  -  Gridsize: 200  -  Cubic Spline")
            # assert: the harmonic rows carry the spectrum cells and the normalized ones
            expect(app.get_by_id("fourier_block_0_row_2")).to_have_name("Harmonic 2, 2.000000e+03 Hz, 1.250000e+00, 9.000000e+01 deg, normalized 2.500000e-01, 9.000000e+01 deg")
            # assert: the last harmonic is listed and the block stops there
            expect(app.get_by_id("fourier_block_0_row_4")).to_exist()
            expect(app.get_by_id("fourier_block_0_row_5")).not_().to_exist()

    def test_the_fourier_table_yields_the_body_to_the_netlist_tab(self) -> None:
        # arrange: start on the Fourier results table through the dataset argument
        dataset = Path(__file__).resolve().parent / "fixtures" / "four-simple-01.four0"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            app.get_by_id("fourier_results_view").wait_for_exists()
            # step: select the netlist tab
            app.get_by_id("document_tabs").click("select-tab-0")
            # assert: the editor takes the body and the table leaves it
            app.get_by_id("netlist_editor").wait_for_exists()
            expect(app.get_by_id("fourier_results_view")).not_().to_exist()
            # step: select the Fourier dataset tab again
            app.get_by_id("document_tabs").click("select-tab-1")
            # assert: the table returns and the chart canvas stays hidden
            app.get_by_id("fourier_results_view").wait_for_exists()
            expect(app.get_by_id("fourier_results_view")).to_have_name("Fourier results: FOUR: 1000 Hz")
            expect(app.get_by_id("charts_view")).not_().to_exist()

    def test_the_fourier_table_yields_the_body_to_a_chart_tab(self) -> None:
        # arrange: a Fourier output and a transient dataset opened as two tabs
        four_dataset = Path(__file__).resolve().parent / "fixtures" / "four-simple-01.four0"
        chart_dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(four_dataset), "--dataset", str(chart_dataset)]), self.id()) as app:
            # assert: the last opened dataset owns the body
            app.get_by_id("charts_view").wait_for_exists()
            expect(app.get_by_id("fourier_results_view")).not_().to_exist()
            # step: select the Fourier dataset tab
            app.get_by_id("document_tabs").click("select-tab-1")
            # assert: the table takes the body in place of the canvas
            app.get_by_id("fourier_results_view").wait_for_exists()
            expect(app.get_by_id("fourier_results_view")).to_have_name("Fourier results: FOUR: 1000 Hz")
            expect(app.get_by_id("charts_view")).not_().to_exist()
            # step: select the transient dataset tab again
            app.get_by_id("document_tabs").click("select-tab-2")
            # assert: the charts return and the table leaves the body
            app.get_by_id("charts_view").wait_for_exists()
            expect(app.get_by_id("charts_view")).to_have_name("2 datasets, 1 line chart, active: tran-simple-01")
            expect(app.get_by_id("fourier_results_view")).not_().to_exist()

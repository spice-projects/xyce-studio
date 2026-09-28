import tempfile
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch

# scroll step in logical pixels per wheel tick; a negative delta_y reveals
# content further down
SCROLL_STEP = -60.0


class InitialConditionsChecks(unittest.TestCase):

    def test_transient_ic_line_merges_two_statements_into_one(self) -> None:
        # arrange: write a transient netlist carrying two .IC statements into a scratch directory
        netlist = Path(tempfile.mkdtemp(prefix="xyce-ic-transient-")) / "ic-transient.cir"
        netlist.write_text("* IC transient test\nR1 a 0 1k\nR2 b 0 1k\nR3 a b 1k\n.IC V(a)=2\n.IC V(b)=10\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            # arrange: the transient tab is the active analysis and the dialog scroll view is the second scroll view in the window (the first is the netlist editor scroller)
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the transient page until the initial conditions field is fully inside the scroll viewport
            for _ in range(30):
                editors = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. V(out)=1.0 V(in)=0"]
                if editors:
                    editor_rect = app.client().get_element_properties(editors[0])
                    viewport = scroller.properties()
                    top = editor_rect["absolutePosition"]["y"]
                    bottom = top + editor_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the initial conditions field to scroll into view")
            # assert: both statements appear on one line without the .IC prefix
            expect(app.get_by_id("TransientParametersPanel::ic-input")).to_have_text("V(a)=2 V(b)=10")
            # step 3: edit the merged line and accept the dialog
            app.get_by_id("TransientParametersPanel::ic-input").fill("V(a)=3 V(b)=10")
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed
            app.get_by_id("TransientParametersPanel::ic-input").wait_for_gone(timeout=10.0)
            # assert: the netlist editor holds the single rewritten .IC line
            editor_text = app.get_by_id("NetlistEditor::input").text()
            ic_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".IC")]
            self.assertEqual(ic_lines, [".IC V(a)=3 V(b)=10"])
            # step 4: reopen the dialog to verify the accepted line persisted
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            # step 5: scroll the transient page until the initial conditions field is fully inside the scroll viewport again
            for _ in range(30):
                editors = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. V(out)=1.0 V(in)=0"]
                if editors:
                    editor_rect = app.client().get_element_properties(editors[0])
                    viewport = scroller.properties()
                    top = editor_rect["absolutePosition"]["y"]
                    bottom = top + editor_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the initial conditions field to scroll into view")
            # assert: the field shows the accepted line
            expect(app.get_by_id("TransientParametersPanel::ic-input")).to_have_text("V(a)=3 V(b)=10")

    def test_dc_ic_line_round_trips(self) -> None:
        # arrange: write a dc sweep netlist carrying an .IC statement into a scratch directory
        netlist = Path(tempfile.mkdtemp(prefix="xyce-ic-dc-")) / "ic-dc.cir"
        netlist.write_text("* IC dc test\nV1 in 0 DC 1\nR1 in out 1k\nR2 out 0 1k\n.IC V(out)=2\n.DC V1 0 5 1\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            # arrange: the dc tab is the active analysis and the dialog scroll view is the second scroll view in the window
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the dc page until the initial conditions field is fully inside the scroll viewport
            for _ in range(30):
                editors = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. V(out)=1.0 V(in)=0"]
                if editors:
                    editor_rect = app.client().get_element_properties(editors[0])
                    viewport = scroller.properties()
                    top = editor_rect["absolutePosition"]["y"]
                    bottom = top + editor_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the initial conditions field to scroll into view")
            # assert: the field shows the parsed statement without the .IC prefix
            expect(app.get_by_id("DcParametersPanel::ic-input")).to_have_text("V(out)=2")
            # step 3: edit the line and accept the dialog
            app.get_by_id("DcParametersPanel::ic-input").fill("V(out)=3")
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed
            app.get_by_id("DcParametersPanel::ic-input").wait_for_gone(timeout=10.0)
            # assert: the netlist editor holds the single rewritten .IC line
            editor_text = app.get_by_id("NetlistEditor::input").text()
            ic_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".IC")]
            self.assertEqual(ic_lines, [".IC V(out)=3"])
            # step 4: reopen the dialog to verify the accepted line persisted
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            # step 5: scroll the dc page until the initial conditions field is fully inside the scroll viewport again
            for _ in range(30):
                editors = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. V(out)=1.0 V(in)=0"]
                if editors:
                    editor_rect = app.client().get_element_properties(editors[0])
                    viewport = scroller.properties()
                    top = editor_rect["absolutePosition"]["y"]
                    bottom = top + editor_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the initial conditions field to scroll into view")
            # assert: the field shows the accepted line
            expect(app.get_by_id("DcParametersPanel::ic-input")).to_have_text("V(out)=3")

    def test_ac_tab_keeps_the_ic_line_as_pass_through(self) -> None:
        # arrange: write an ac netlist carrying an .IC statement into a scratch directory
        netlist = Path(tempfile.mkdtemp(prefix="xyce-ic-ac-")) / "ic-ac.cir"
        netlist.write_text("* IC ac test\nV1 in 0 DC 1 AC 1\nR1 in out 1k\nR2 out 0 1k\n.IC V(out)=2\n.AC DEC 10 1k 1meg\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            # arrange: the ac tab is the active analysis
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            # assert: no visible text field on the ac page exposes the collected line
            texts = [app.get_by_type("LineEdit").nth(i).text() for i in range(app.get_by_type("LineEdit").count())]
            self.assertNotIn("V(out)=2", texts)
            # step 2: accept the dialog without editing anything
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed
            app.get_by_type("LineEdit").nth(0).wait_for_gone(timeout=10.0)
            # assert: the untouched statement was still rewritten as one merged line
            editor_text = app.get_by_id("NetlistEditor::input").text()
            ic_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".IC")]
            self.assertEqual(ic_lines, [".IC V(out)=2"])

    def test_ic_line_validation_rejects_unparseable_text(self) -> None:
        # arrange: write a transient netlist carrying an .IC statement into a scratch directory
        netlist = Path(tempfile.mkdtemp(prefix="xyce-ic-invalid-")) / "ic-invalid.cir"
        netlist.write_text("* IC validation test\nR1 a 0 1k\n.IC V(a)=2\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            # arrange: the transient tab is the active analysis and the dialog scroll view is the second scroll view in the window
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the transient page until the initial conditions field is fully inside the scroll viewport
            for _ in range(30):
                editors = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. V(out)=1.0 V(in)=0"]
                if editors:
                    editor_rect = app.client().get_element_properties(editors[0])
                    viewport = scroller.properties()
                    top = editor_rect["absolutePosition"]["y"]
                    bottom = top + editor_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the initial conditions field to scroll into view")
            # step 3: replace the line with text that parses to nothing and accept
            app.get_by_id("TransientParametersPanel::ic-input").fill("garbage")
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog stays open for corrections (the footer ok button remains)
            app.wait_for_condition(lambda: any(app.client().get_element_properties(handle).get("accessibleLabel") == "OK" for handle in app.client().find_by_type_in(root, "Button")), timeout=5.0, message="expected the dialog to stay open after the rejected accept")
            # assert: the validation error names the problem
            app.wait_for_condition(lambda: any("Invalid initial conditions" in (app.client().get_element_properties(handle).get("accessibleLabel") or "") for handle in app.client().find_by_type_in(root, "Text")), timeout=5.0, message="expected the initial conditions validation error to appear")
            # step 4: scroll the transient page until the initial conditions field is fully inside the scroll viewport again
            for _ in range(30):
                editors = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. V(out)=1.0 V(in)=0"]
                if editors:
                    editor_rect = app.client().get_element_properties(editors[0])
                    viewport = scroller.properties()
                    top = editor_rect["absolutePosition"]["y"]
                    bottom = top + editor_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the initial conditions field to scroll into view")
            # step 5: correct the line and accept again
            app.get_by_id("TransientParametersPanel::ic-input").fill("V(a)=3")
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed
            app.get_by_id("TransientParametersPanel::ic-input").wait_for_gone(timeout=10.0)
            # assert: the netlist editor holds the corrected .IC line
            editor_text = app.get_by_id("NetlistEditor::input").text()
            ic_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".IC")]
            self.assertEqual(ic_lines, [".IC V(a)=3"])

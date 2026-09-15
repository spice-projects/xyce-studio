import shutil
import unittest
from pathlib import Path

from slint_automation import TestSession, launch


class ChartsContextActionChecks(unittest.TestCase):

    def test_add_chart_appends_after_the_single_chart(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation and wait for the
        # initial transient chart
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            # act: add a chart targeting the bottom of the stack
            app.invoke_chart_action("add-chart", chart_position=1.0)
            # assert: a second chart appeared after the first one
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=5.0, message="expected a second chart after the invoke")
            labels = [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")]
            self.assertEqual(labels, [None, None])

    def test_add_chart_inserts_after_the_targeted_chart(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation and switch to the
        # first generated fft tab showing two plotted charts
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 3)
            tabs.nth(1).click()
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=10.0, message="expected the two fft charts")
            app.wait_for_condition(lambda: [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == ["FFT(I(C1))", "FFT(I(L1))"], timeout=10.0, message="expected the fft charts to carry their series names")
            # act: add a chart targeting the first chart (position 0.25 of two)
            app.invoke_chart_action("add-chart", chart_position=0.25)
            # assert: the empty chart sits directly after the first chart,
            # pushing the second chart down to the third slot
            app.wait_for_condition(lambda: [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == ["FFT(I(C1))", None, "FFT(I(L1))"], timeout=5.0, message="expected the new chart to insert after the first fft chart")

    def test_add_chart_joins_the_shared_zoomed_abscissa_range(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation and switch to the
        # first generated fft tab showing two charts sharing the full abscissa
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 3)
            tabs.nth(1).click()
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=10.0, message="expected the two fft charts")
            # act: dump the abscissa band labels of both charts; the abscissa
            # labels sit in the band below the plot (relative y in the last
            # 60 px of the view)
            first_view = app.get_by_type("ChartView").nth(0)
            first_view_props = first_view.properties()
            first_origin_y = first_view_props.get("absolutePosition", {}).get("y", 0)
            first_height = first_view_props.get("size", {}).get("height", 0)
            first_texts = first_view.child("Text")
            pre_abscissa_0: list[str] = []
            for index in range(first_texts.count()):
                props = first_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - first_origin_y
                if relative_y > first_height - 60:
                    pre_abscissa_0.append(props.get("accessibleLabel"))
            second_view = app.get_by_type("ChartView").nth(1)
            second_view_props = second_view.properties()
            second_origin_y = second_view_props.get("absolutePosition", {}).get("y", 0)
            second_texts = second_view.child("Text")
            pre_abscissa_1: list[str] = []
            for index in range(second_texts.count()):
                props = second_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - second_origin_y
                if relative_y > first_height - 60:
                    pre_abscissa_1.append(props.get("accessibleLabel"))
            pre_ticks_0 = [label for label in pre_abscissa_0 if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            pre_ticks_1 = [label for label in pre_abscissa_1 if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            self.assertEqual(pre_ticks_0, pre_ticks_1)
            # act: drag a zoom selection inside the first fft chart
            app.get_by_type("ChartView").nth(0).drag(660.0, 310.0)
            # act: read the zoomed abscissa labels of the first chart
            zoomed_view = app.get_by_type("ChartView").nth(0)
            zoomed_texts = zoomed_view.child("Text")
            zoomed_abscissa_0: list[str] = []
            for index in range(zoomed_texts.count()):
                props = zoomed_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - first_origin_y
                if relative_y > first_height - 60:
                    zoomed_abscissa_0.append(props.get("accessibleLabel"))
            zoomed_ticks_0 = [label for label in zoomed_abscissa_0 if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            self.assertNotEqual(zoomed_ticks_0, pre_ticks_0)
            # act: add a chart after the first chart while zoomed
            app.invoke_chart_action("add-chart", chart_position=0.25)
            # assert: three charts in the ticket order and the new chart joined
            # the shared zoomed abscissa range of the panel
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 3, timeout=5.0, message="expected the new chart to be added")
            app.wait_for_condition(lambda: [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == ["FFT(I(C1))", None, "FFT(I(L1))"], timeout=5.0, message="expected the new chart to insert after the first fft chart")
            added_view = app.get_by_type("ChartView").nth(1)
            added_view_props = added_view.properties()
            added_origin_y = added_view_props.get("absolutePosition", {}).get("y", 0)
            added_height = added_view_props.get("size", {}).get("height", 0)
            added_texts = added_view.child("Text")
            added_abscissa: list[str] = []
            for index in range(added_texts.count()):
                props = added_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - added_origin_y
                if relative_y > added_height - 60:
                    added_abscissa.append(props.get("accessibleLabel"))
            added_ticks = [label for label in added_abscissa if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            self.assertNotEqual(added_ticks, pre_ticks_0)
            self.assertEqual(added_ticks, zoomed_ticks_0)

    def test_delete_chart_removes_the_targeted_chart(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation and switch to the
        # first generated fft tab showing two plotted charts
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 3)
            tabs.nth(1).click()
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=10.0, message="expected the two fft charts")
            app.wait_for_condition(lambda: [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == ["FFT(I(C1))", "FFT(I(L1))"], timeout=10.0, message="expected the fft charts to carry their series names")
            # act: delete the first chart through its context menu position
            app.invoke_chart_action("delete-chart", chart_position=0.25)
            # assert: the first chart was removed and the second chart moved up
            app.wait_for_condition(lambda: [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == ["FFT(I(L1))"], timeout=5.0, message="expected the first fft chart to be removed")

    def test_delete_chart_keeps_a_blank_chart_when_the_last_is_removed(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation and switch to the
        # first generated fft tab showing two plotted charts
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 3)
            tabs.nth(1).click()
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=10.0, message="expected the two fft charts")
            app.wait_for_condition(lambda: [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == ["FFT(I(C1))", "FFT(I(L1))"], timeout=10.0, message="expected the fft charts to carry their series names")
            # act: remove both charts one by one
            app.invoke_chart_action("delete-chart", chart_position=0.25)
            app.wait_for_condition(lambda: [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == ["FFT(I(L1))"], timeout=5.0, message="expected the first fft chart to be removed")
            app.invoke_chart_action("delete-chart", chart_position=0.5)
            # assert: a blank chart replaces the last removed one so the panel
            # is never empty
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1 and [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == [None], timeout=5.0, message="expected a single blank chart after removing the last one")

    def test_autorange_resets_the_ordinate_axis_of_the_targeted_chart(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation, switch to the
        # first fft tab and drag a zoom on its first chart
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 3)
            tabs.nth(1).click()
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=10.0, message="expected the two fft charts")
            # act: dump the ordinate labels of the first chart
            view = app.get_by_type("ChartView").nth(0)
            view_props = view.properties()
            origin_y = view_props.get("absolutePosition", {}).get("y", 0)
            height = view_props.get("size", {}).get("height", 0)
            texts = view.child("Text")
            pre_ordinate_0: list[str] = []
            for index in range(texts.count()):
                props = texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - origin_y
                if relative_y <= height - 60:
                    pre_ordinate_0.append(props.get("accessibleLabel"))
            # act: drag a zoom selection inside the first fft chart
            app.get_by_type("ChartView").nth(0).drag(660.0, 310.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=5.0, message="expected the charts to stay after the drag")
            zoomed_view = app.get_by_type("ChartView").nth(0)
            zoomed_texts = zoomed_view.child("Text")
            zoomed_ordinate_0: list[str] = []
            for index in range(zoomed_texts.count()):
                props = zoomed_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - origin_y
                if relative_y <= height - 60:
                    zoomed_ordinate_0.append(props.get("accessibleLabel"))
            self.assertNotEqual(zoomed_ordinate_0, pre_ordinate_0)
            # act: autorange the first chart through its context menu action
            app.invoke_chart_action("autorange", chart_position=0.25)
            # assert: the first chart regained a different ordinate range and
            # the zoomed abscissa stays shared with the other chart
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=5.0, message="expected the charts to stay after autorange")
            ranged_view = app.get_by_type("ChartView").nth(0)
            ranged_texts = ranged_view.child("Text")
            ranged_ordinate_0: list[str] = []
            ranged_abscissa_0: list[str] = []
            for index in range(ranged_texts.count()):
                props = ranged_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - origin_y
                if relative_y > height - 60:
                    ranged_abscissa_0.append(props.get("accessibleLabel"))
                else:
                    ranged_ordinate_0.append(props.get("accessibleLabel"))
            self.assertNotEqual(ranged_ordinate_0, zoomed_ordinate_0)
            other_view = app.get_by_type("ChartView").nth(1)
            other_texts = other_view.child("Text")
            other_abscissa: list[str] = []
            for index in range(other_texts.count()):
                props = other_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - origin_y
                if relative_y > height - 60:
                    other_abscissa.append(props.get("accessibleLabel"))
            ranged_ticks = [label for label in ranged_abscissa_0 if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            other_ticks = [label for label in other_abscissa if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            self.assertEqual(ranged_ticks, other_ticks)

    def test_drag_handle_moves_the_chart_to_the_target_slot(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation and switch to the
        # first generated fft tab showing two plotted charts
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 3)
            tabs.nth(1).click()
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=10.0, message="expected the two fft charts")
            app.wait_for_condition(lambda: [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == ["FFT(I(C1))", "FFT(I(L1))"], timeout=10.0, message="expected the fft charts to carry their series names")
            # assert: one drag grip renders per chart
            self.assertEqual(app.get_by_type("ChartDragHandle").count(), 2)
            # act: drag the grip of the second chart onto the first chart slot
            first_view = app.get_by_type("ChartView").nth(0)
            first_view_props = first_view.properties()
            first_position = first_view_props.get("absolutePosition", {})
            first_size = first_view_props.get("size", {})
            target_x = first_position.get("x", 0) + first_size.get("width", 0) / 2
            target_y = first_position.get("y", 0) + first_size.get("height", 0) / 2
            app.get_by_type("ChartDragHandle").nth(1).drag(target_x, target_y)
            # assert: the dragged chart moved up to the first slot and the
            # other chart shifted down to the second slot
            app.wait_for_condition(lambda: [app.client().get_element_properties(handle).get("accessibleLabel") for handle in app.client().find_by_type("ChartView")] == ["FFT(I(L1))", "FFT(I(C1))"], timeout=5.0, message="expected the second fft chart to move to the first slot")

    def test_zoom_to_fit_restores_the_full_abscissa_range(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation, switch to the
        # first fft tab and drag a zoom on its first chart
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 3)
            tabs.nth(1).click()
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=10.0, message="expected the two fft charts")
            # act: dump the abscissa labels of both charts before the zoom
            first_view = app.get_by_type("ChartView").nth(0)
            first_view_props = first_view.properties()
            first_origin_y = first_view_props.get("absolutePosition", {}).get("y", 0)
            first_height = first_view_props.get("size", {}).get("height", 0)
            first_texts = first_view.child("Text")
            pre_abscissa_0: list[str] = []
            for index in range(first_texts.count()):
                props = first_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - first_origin_y
                if relative_y > first_height - 60:
                    pre_abscissa_0.append(props.get("accessibleLabel"))
            second_view = app.get_by_type("ChartView").nth(1)
            second_view_props = second_view.properties()
            second_origin_y = second_view_props.get("absolutePosition", {}).get("y", 0)
            second_texts = second_view.child("Text")
            pre_abscissa_1: list[str] = []
            for index in range(second_texts.count()):
                props = second_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - second_origin_y
                if relative_y > first_height - 60:
                    pre_abscissa_1.append(props.get("accessibleLabel"))
            pre_ticks = [label for label in pre_abscissa_0 if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            self.assertEqual(pre_ticks, [label for label in pre_abscissa_1 if label.endswith(("Hz", "s")) or label.endswith(" ms")])
            # act: drag a zoom selection inside the first fft chart
            app.get_by_type("ChartView").nth(0).drag(660.0, 310.0)
            zoomed_view = app.get_by_type("ChartView").nth(0)
            zoomed_texts = zoomed_view.child("Text")
            zoomed_abscissa: list[str] = []
            for index in range(zoomed_texts.count()):
                props = zoomed_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - first_origin_y
                if relative_y > first_height - 60:
                    zoomed_abscissa.append(props.get("accessibleLabel"))
            self.assertNotEqual([label for label in zoomed_abscissa if label.endswith(("Hz", "s")) or label.endswith(" ms")], pre_ticks)
            # act: zoom to fit from any chart context menu position
            app.invoke_chart_action("zoom-to-fit", chart_position=0.75)
            # assert: both charts show the full abscissa range again
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=5.0, message="expected the charts to stay after zoom to fit")
            reset_view = app.get_by_type("ChartView").nth(0)
            reset_texts = reset_view.child("Text")
            reset_abscissa_0: list[str] = []
            for index in range(reset_texts.count()):
                props = reset_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - first_origin_y
                if relative_y > first_height - 60:
                    reset_abscissa_0.append(props.get("accessibleLabel"))
            self.assertEqual([label for label in reset_abscissa_0 if label.endswith(("Hz", "s")) or label.endswith(" ms")], pre_ticks)
            other_reset_view = app.get_by_type("ChartView").nth(1)
            other_reset_texts = other_reset_view.child("Text")
            reset_abscissa_1: list[str] = []
            for index in range(other_reset_texts.count()):
                props = other_reset_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - first_origin_y
                if relative_y > first_height - 60:
                    reset_abscissa_1.append(props.get("accessibleLabel"))
            self.assertEqual([label for label in reset_abscissa_1 if label.endswith(("Hz", "s")) or label.endswith(" ms")], pre_ticks)

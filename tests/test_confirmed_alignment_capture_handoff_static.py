from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8-sig")


class ConfirmedAlignmentCaptureHandoffStaticTest(unittest.TestCase):
    def test_alignment_exposes_one_click_live_capture_action(self):
        header = read("src/DIMM.h")
        dimm = read("src/DIMM.cpp")
        alignment = read("src/DIMM.Alignment.cpp")
        ui = read("src/DIMM.Ui.cpp")

        self.assertIn("void onConfirmAndStartCapture();", header)
        self.assertIn("m_actionConfirmAndStartCapture", dimm)
        self.assertIn("&DIMM::onConfirmAndStartCapture", dimm)
        self.assertIn("stopAlignmentMode();", alignment)
        self.assertIn("onStartCapture();", alignment)
        self.assertIn("确认并开始采集", ui)

    def test_action_requires_both_alignment_targets(self):
        ui = read("src/DIMM.Ui.cpp")
        action_body = ui.split(
            "if (m_actionConfirmAndStartCapture)", 1
        )[1].split(
            "const bool alignmentControlsVisible", 1
        )[0]

        self.assertIn("m_captureState == CaptureState::Alignment", action_body)
        self.assertIn("!m_alignmentCoarseActive", action_body)
        self.assertIn("hasConfirmedAlignmentTargets()", action_body)

    def test_live_handoff_prefers_spatially_confirmed_target(self):
        live = read("src/DIMM.LiveRoi.cpp")
        pipeline = read("src/PolarisDetectionPipeline.cpp")

        self.assertIn("selectNearestCandidate", live)
        self.assertIn("runtime.hasLastTargetPosition[cameraIndex]", live)
        self.assertIn("runtime.hasConfirmedPolarisPosition[cameraIndex]", live)
        self.assertIn("不切换到其他候选星点", live)
        self.assertIn("const QPointF& target", pipeline)
        self.assertIn("maxDistancePx", pipeline)

    def test_no_confirmed_target_keeps_existing_automatic_selection_path(self):
        live = read("src/DIMM.LiveRoi.cpp")
        preferred_start = live.find("if (runtime.hasLastTargetPosition[cameraIndex])")
        automatic_start = live.find(
            "selection = PolarisDetectionPipeline::selectFullFrameStarCandidate(",
            preferred_start,
        )

        self.assertGreaterEqual(preferred_start, 0)
        self.assertGreaterEqual(automatic_start, 0)
        self.assertLess(preferred_start, automatic_start)


if __name__ == "__main__":
    unittest.main()

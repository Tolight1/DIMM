from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentFrameTrackingHandlersStaticTest(unittest.TestCase):
    def test_dimm_frame_dispatch_delegates_tracking_branches_to_handlers(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "bool DIMM::handleManualAlignmentFrameTracking", 1
        )[0]
        self.assertIn("bool DIMM::handleManualAlignmentFrameTracking", alignment_cpp)
        self.assertIn("bool DIMM::handleAutomaticAlignmentFrameTracking", alignment_cpp)
        manual_body = alignment_cpp.split("bool DIMM::handleManualAlignmentFrameTracking", 1)[1].split(
            "bool DIMM::handleAutomaticAlignmentFrameTracking", 1
        )[0]
        automatic_body = alignment_cpp.split("bool DIMM::handleAutomaticAlignmentFrameTracking", 1)[1].split(
            "bool DIMM::prepareAlignmentFramePreview", 1
        )[0]

        self.assertIn("bool handleManualAlignmentFrameTracking", header)
        self.assertIn("bool handleAutomaticAlignmentFrameTracking", header)
        self.assertIn("handleManualAlignmentFrameTracking(cameraIndex, packet.image)", packet_body)
        self.assertIn("handleAutomaticAlignmentFrameTracking(cameraIndex, packet.image, nowMs)", packet_body)
        self.assertNotIn("AlignmentController::applyManualTrackingSuccess", packet_body)
        self.assertNotIn("AlignmentController::applyTrackingSuccess", packet_body)
        self.assertIn("AlignmentController::applyManualTrackingSuccess", manual_body)
        self.assertIn("AlignmentUiPresenter::formatManualTrackingSolveLabel", manual_body)
        self.assertIn("PolarisTracker::recordTrackSuccess", automatic_body)
        self.assertIn("PolarisTracker::recordTrackFailure", automatic_body)
        self.assertIn("AlignmentUiPresenter::formatTrackingLostSolveLabel", automatic_body)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentFrameTrackingControllerStaticTest(unittest.TestCase):
    def test_controller_owns_manual_only_tracking_state_updates(self):
        header = read("src/AlignmentController.h")
        cpp = read("src/AlignmentController.cpp")
        dimm = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]

        self.assertIn("applyManualTrackingSuccess", header)
        self.assertIn("applyManualTrackingFailure", header)
        self.assertIn("AlignmentController::applyManualTrackingSuccess", packet_body)
        self.assertIn("AlignmentController::applyManualTrackingFailure", packet_body)
        self.assertIn("runtime->state = AlignmentSolveState::ManualOnly", cpp)
        self.assertIn("runtime->lastPolarisPosition = trackedPosition", cpp)
        self.assertIn("++runtime->consecutiveTrackFailures", cpp)
        self.assertNotIn("solveRuntime.lastPolarisPosition = trackedPosition", packet_body)
        self.assertNotIn("++solveRuntime.consecutiveTrackFailures", packet_body)

    def test_automatic_tracking_uses_polaris_tracker_directly(self):
        header = read("src/AlignmentController.h")
        cpp = read("src/AlignmentController.cpp")
        dimm = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]

        self.assertNotIn("applyTrackingSuccess", header)
        self.assertNotIn("applyTrackingFailure", header)
        self.assertNotIn("AlignmentController::applyTrackingSuccess", cpp)
        self.assertNotIn("AlignmentController::applyTrackingFailure", cpp)
        self.assertIn("PolarisTracker::recordTrackSuccess", packet_body)
        self.assertIn("PolarisTracker::recordTrackFailure", packet_body)


if __name__ == "__main__":
    unittest.main()

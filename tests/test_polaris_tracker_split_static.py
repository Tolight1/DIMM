from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisTrackerSplitStaticTest(unittest.TestCase):
    def test_tracker_module_declares_tracking_state_helpers(self):
        header = read("src/PolarisTracker.h")
        cpp = read("src/PolarisTracker.cpp")

        self.assertIn('#include "AlignmentTypes.h"', header)
        self.assertIn("namespace PolarisTracker", header)
        self.assertIn("QRect trackingWindowForPosition", header)
        self.assertIn("bool isUsableTrackingWindow", header)
        self.assertIn("void recordTrackSuccess", header)
        self.assertIn("bool recordTrackFailure", header)
        self.assertIn("bool shouldHoldFullSolveRequest", header)
        self.assertIn("void markFullSolveSubmitted", header)
        self.assertIn("QRect PolarisTracker::trackingWindowForPosition", cpp)
        self.assertIn("bool PolarisTracker::recordTrackFailure", cpp)

    def test_dimm_delegates_tracking_window_and_state_transitions(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]
        request_body = alignment_cpp.split("void DIMM::requestAutomaticPolarisSolve", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolveBoth", 1
        )[0]
        track_body = cpp.split("bool DIMM::trackAlignmentPolarisLocally", 1)[1].split(
            "void DIMM::logPolarisSolveResult", 1
        )[0]
        task_cpp = read("src/AlignmentTaskManager.cpp")
        local_tracker_cpp = read("src/AlignmentLocalTracker.cpp")

        self.assertIn('#include "PolarisTracker.h"', cpp)
        self.assertIn("PolarisTracker::recordTrackSuccess", packet_body)
        self.assertIn("PolarisTracker::recordTrackFailure", packet_body)
        self.assertIn("AlignmentTaskManager::prepareFullSolveRequest", request_body)
        self.assertIn("PolarisTracker::shouldHoldFullSolveRequest", task_cpp)
        self.assertIn("PolarisTracker::markFullSolveSubmitted", task_cpp)
        self.assertIn("AlignmentLocalTracker::trackFromConfirmedPosition", track_body)
        self.assertIn("PolarisTracker::trackingWindowForPosition", local_tracker_cpp)
        self.assertIn("PolarisTracker::isUsableTrackingWindow", local_tracker_cpp)
        self.assertNotIn("constexpr int kTrackingHalfWindowPx", track_body)
        self.assertIn("bool trackAlignmentPolarisLocally", header)


if __name__ == "__main__":
    unittest.main()

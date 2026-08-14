from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentLocalTrackerRuntimeStaticTest(unittest.TestCase):
    def test_local_tracker_owns_confirmed_position_and_window_bookkeeping(self):
        header = read("src/AlignmentLocalTracker.h")
        cpp = read("src/AlignmentLocalTracker.cpp")
        dimm = read("src/DIMM.cpp")
        track_body = dimm.split("bool DIMM::trackAlignmentPolarisLocally", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolveBoth", 1
        )[0]

        self.assertIn("trackFromConfirmedPosition", header)
        self.assertIn("AlignmentCameraSolveRuntime* runtime", header)
        self.assertIn("PolarisTracker::trackingWindowForPosition", cpp)
        self.assertIn("runtime->trackingWindow = trackingWindow", cpp)
        self.assertIn("PolarisTracker::isUsableTrackingWindow", cpp)
        self.assertIn("AlignmentLocalTracker::trackFromConfirmedPosition", track_body)
        self.assertNotIn("PolarisTracker::trackingWindowForPosition", track_body)
        self.assertNotIn("m_alignmentSession.camera(cameraIndex).solveRuntime.trackingWindow", track_body)


if __name__ == "__main__":
    unittest.main()

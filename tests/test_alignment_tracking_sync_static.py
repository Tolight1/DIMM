from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentTrackingSyncStaticTest(unittest.TestCase):
    def test_controller_owns_live_runtime_tracking_position_sync(self):
        header = read("src/AlignmentController.h")
        cpp = read("src/AlignmentController.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]

        self.assertIn("syncTrackedPolarisPosition", header)
        self.assertIn("confirmedPolarisPosition", header)
        self.assertIn("lastTargetPosition", header)
        self.assertIn("confirmedPolarisPosition[cameraIndex] = trackedPosition", cpp)
        self.assertIn("lastTargetPosition[cameraIndex] = trackedPosition", cpp)
        self.assertIn("AlignmentController::syncTrackedPolarisPosition", packet_body)
        self.assertNotIn("runtime.confirmedPolarisPosition[cameraIndex] = trackedPosition", packet_body)
        self.assertNotIn("runtime.lastTargetPosition[cameraIndex] = trackedPosition", packet_body)


if __name__ == "__main__":
    unittest.main()

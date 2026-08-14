from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentFrameCoordinatorStaticTest(unittest.TestCase):
    def test_frame_coordinator_is_build_unit_and_owns_preview_gate(self):
        cmake = read("CMakeLists.txt")
        self.assertTrue((ROOT / "src/AlignmentFrameCoordinator.h").exists())
        self.assertTrue((ROOT / "src/AlignmentFrameCoordinator.cpp").exists())
        header = read("src/AlignmentFrameCoordinator.h")
        cpp = read("src/AlignmentFrameCoordinator.cpp")
        dimm = read("src/DIMM.cpp")
        packet_body = dimm.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]

        self.assertIn("src/AlignmentFrameCoordinator.h", cmake)
        self.assertIn("src/AlignmentFrameCoordinator.cpp", cmake)
        self.assertIn("struct FrameGateInput", header)
        self.assertIn("bool shouldAcceptAlignmentFrame", header)
        self.assertIn("int previewIntervalMs", header)
        self.assertIn("std::max(100", cpp)
        self.assertIn("lastPreviewMs", cpp)
        self.assertIn("AlignmentFrameCoordinator::shouldAcceptAlignmentFrame", packet_body)
        self.assertIn("AlignmentFrameCoordinator::previewIntervalMs", packet_body)
        self.assertNotIn("static_cast<int>(1000.0 / std::max", packet_body)


if __name__ == "__main__":
    unittest.main()

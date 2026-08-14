from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisGuiDetectionReuseStaticTest(unittest.TestCase):
    def test_alignment_overlay_reuses_worker_detections_before_full_frame_scan(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/DIMM.cpp")
        solver_cpp = read("src/PolarisSolver.cpp")
        session_cpp = read("src/AlignmentSession.cpp")
        overlay_body = cpp.split("void DIMM::updateAlignmentOverlay", 1)[1].split(
            "void DIMM::onStartCapture", 1
        )[0]
        packet_body = cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]

        self.assertIn("quint64 frameId", header)
        self.assertIn("quint64 frameId", solver_cpp)
        self.assertIn("packet.frameId", packet_body)
        self.assertIn("cameraState.lastFrameId = packet.frameId", packet_body)
        self.assertIn("initialCandidatesFromPolarisDetections", cpp)
        self.assertIn("const bool canReuseSolverDetections", session_cpp)
        self.assertIn("solved.frameId == input.lastFrameId", session_cpp)
        self.assertIn("solved.detections", session_cpp)
        self.assertIn("initialCandidatesFromPolarisDetections(solved.detections)", session_cpp)
        self.assertIn("if (!input.allowGuiCandidateDetection", session_cpp)
        self.assertLess(
            session_cpp.find("initialCandidatesFromPolarisDetections(solved.detections)"),
            session_cpp.find("input.candidateDetector"),
        )


if __name__ == "__main__":
    unittest.main()

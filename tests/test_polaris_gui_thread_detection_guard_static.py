from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisGuiThreadDetectionGuardStaticTest(unittest.TestCase):
    def test_alignment_overlay_keeps_candidate_detection_running_in_alignment_mode(self):
        cpp = read("src/DIMM.Alignment.cpp")
        overlay_body = cpp.split("void DIMM::updateAlignmentOverlay", 1)[1].split(
            "void DIMM::onStartCapture", 1
        )[0]
        collect_body = cpp.split("QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1)[1].split(
            "bool DIMM::handleAlignmentCandidateSelection", 1
        )[0]

        self.assertIn("const bool shouldRefreshCandidateDetection", overlay_body)
        self.assertIn("const bool allowGuiCandidateDetection = shouldRefreshCandidateDetection", overlay_body)
        self.assertIn("m_alignmentCachedCandidates[cameraIndex].isEmpty()", overlay_body)
        self.assertIn("m_alignmentLastCandidateDetectionMs[cameraIndex]", overlay_body)
        self.assertIn("m_alignmentSession.camera(cameraIndex).selectionRequested", overlay_body)
        self.assertNotIn("const bool allowGuiCandidateDetection = manualSelectionRequested", overlay_body)
        self.assertNotIn("const bool allowGuiCandidateDetection = true", overlay_body)
        self.assertNotIn("!m_alignmentAutoSolveEnabled || m_alignmentSession.camera(cameraIndex).selectionRequested", overlay_body)
        self.assertIn("if (!input.allowGuiCandidateDetection || !input.candidateDetector)", read("src/AlignmentSession.cpp"))
        self.assertIn("allowGuiCandidateDetection", collect_body)
        self.assertLess(overlay_body.find("const bool shouldRefreshCandidateDetection"),
                        overlay_body.find("collectAlignmentStarCandidates"))

    def test_confirmed_auto_tracking_overlay_does_not_rescan_full_frame(self):
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        cpp = read("src/DIMM.cpp")
        overlay_body = alignment_cpp.split("void DIMM::updateAlignmentOverlay", 1)[1].split(
            "void DIMM::onStartCapture", 1
        )[0]
        fallback_body = cpp.split("void DIMM::updateConfirmedPolarisFromFallbackCentroid", 1)[1].split(
            "void DIMM::onStartCapture", 1
        )[0]
        session_cpp = read("src/AlignmentSession.cpp")

        self.assertIn("allowGuiCandidateDetection", overlay_body)
        self.assertIn("if (mono8 && mono8->empty() && allowGuiCandidateDetection)", fallback_body)
        self.assertIn("allowGuiCandidateDetection,", fallback_body)
        self.assertIn("!allowGuiCandidateDetection", session_cpp)


if __name__ == "__main__":
    unittest.main()

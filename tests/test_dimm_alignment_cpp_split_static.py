from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmAlignmentCppSplitStaticTest(unittest.TestCase):
    def test_alignment_members_live_in_dimm_alignment_cpp(self):
        dimm = read("src/DIMM.cpp")
        alignment = read("src/DIMM.Alignment.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "void DIMM::onToggleAlignmentMode",
            "void DIMM::onConfirmCamera1PolarisCandidate",
            "void DIMM::onConfirmCamera2PolarisCandidate",
            "void DIMM::requestAlignmentPolarisSelection",
            "bool DIMM::startAlignmentMode",
            "void DIMM::stopAlignmentMode",
            "bool DIMM::prepareAlignmentCamerasForPreview",
            "void DIMM::restoreCamerasAfterAlignment",
            "void DIMM::showAlignmentModeStarted",
            "void DIMM::showAlignmentModeStopped",
            "void DIMM::resetAlignmentRuntimeForStart",
            "void DIMM::resetAlignmentRuntimeForStop",
            "void DIMM::clearAlignmentCanvasesForStart",
            "void DIMM::clearAlignmentCanvasesForStop",
            "double DIMM::fallbackAlignmentOrbitRadiusPx",
            "double DIMM::alignmentOrbitRadiusPx",
            "void DIMM::handleAlignmentFramePacket",
            "bool DIMM::handleManualAlignmentFrameTracking",
            "bool DIMM::handleAutomaticAlignmentFrameTracking",
            "bool DIMM::prepareAlignmentFramePreview",
            "void DIMM::finishAlignmentFramePreview",
            "void DIMM::requestAutomaticPolarisSolve",
            "void DIMM::requestAutomaticPolarisSolveBoth",
            "PolarisSolverConfig DIMM::buildPolarisSolverConfig",
            "void DIMM::onPolarisSolveFinished",
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates",
            "bool DIMM::handleAlignmentCandidateSelection",
            "bool DIMM::promptAlignmentCandidateSelection",
            "void DIMM::updateAlignmentOverlay",
        ]:
            self.assertIn(token, alignment)
            self.assertNotIn(token, dimm)

        self.assertIn("src/DIMM.Alignment.cpp", cmake)

if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmLiveRoiCppSplitStaticTest(unittest.TestCase):
    def test_live_roi_members_live_in_dimm_live_roi_cpp(self):
        dimm = read("src/DIMM.cpp")
        live = read("src/DIMM.LiveRoi.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "bool DIMM::isCentroidNearCurrentRoiEdge",
            "bool DIMM::isCentroidTooFarFromCurrentRoiCenter",
            "bool DIMM::shouldUpdateRoiForRecentering",
            "void DIMM::requestLiveFullFrameRelocalization",
            "void DIMM::handleLiveRoiCentroidLoss",
            "bool DIMM::isUsableCentroidSample",
            "RoiRect DIMM::sanitizeRoi",
            "RoiRect DIMM::buildCameraCentroidRoi",
            "void DIMM::applyRoiSummary",
            "void DIMM::recordLiveRoiUpdate",
            "QString DIMM::roiRuleDescription",
            "bool DIMM::validateAndCacheLiveRoiCapabilities",
            "bool DIMM::readLivePairRoiPosition",
            "RoiRect DIMM::buildLiveCameraRoi",
            "bool DIMM::configureLiveCameras",
            "bool DIMM::applyContinuousCameraFrameRate",
            "void DIMM::advanceLiveAcquisitionGeneration",
            "void DIMM::resetLiveFrameAcceptanceGates",
            "bool DIMM::startDualCameraLocalization",
            "bool DIMM::applyLiveHardwareRois",
            "bool DIMM::applyLiveFullFrameForRelocalization",
            "bool DIMM::selectLiveRelocalizationCentroid",
            "bool DIMM::maybeSeedRoiFromFrame",
            "void DIMM::handleLiveRelocalizationWatchdog",
            "void DIMM::updateFullFrameRoiOverlay",
            "void DIMM::showDeferredLiveRelocalizationPreview",
            "void DIMM::clearPendingLiveRelocalizationRois",
            "bool DIMM::commitPairedInitialRoisIfReady",
            "bool DIMM::startHardwarePulseStage",
            "bool DIMM::startFullFrameLocalizationPulse",
            "bool DIMM::switchToRoiTrackingPulse",
            "void DIMM::updateMinuteRoi",
            "void DIMM::hideLegacyRoiScheduleUi",
        ]:
            self.assertIn(token, live)
            self.assertNotIn(token, dimm)

        self.assertIn("src/DIMM.LiveRoi.cpp", cmake)

if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class LiveRoiStartupBoundariesStaticTest(unittest.TestCase):
    def test_live_full_frame_localization_does_not_enter_roi_processing_before_tracking(self):
        source = read("src/DIMM.CommCamera.cpp")
        body = source.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "void DIMM::scheduleHardwareTriggerStartupCheck", 1
        )[0]

        self.assertIn("const bool roiAvailableForThisCamera", body)
        condition = body.split("const bool roiAvailableForThisCamera", 1)[1].split(
            "if (roiAvailableForThisCamera)", 1
        )[0]
        self.assertIn("roiConfirmed", condition)
        self.assertIn("m_liveHardwareRoiActive && frameLooksLikeHardwareRoi", condition)
        self.assertNotIn("runtime.pendingInitialRoiReady[cameraIndex] || roiConfirmed ||", body)
        self.assertNotIn("m_liveStartupPhase == LiveStartupPhase::Tracking);", body)

    def test_live_minute_roi_update_applies_actual_sensor_roi_once(self):
        source = read("src/DIMM.LiveRoi.cpp")
        body = source.split("void DIMM::updateMinuteRoi", 1)[1].split(
            "void DIMM::hideLegacyRoiScheduleUi", 1
        )[0]

        live_block = body.split("if (m_captureState == CaptureState::Live && hasValidCentroidsForRoiUpdate())", 1)[0]
        self.assertNotIn("buildLiveCameraRoi", live_block)
        self.assertIn("const RoiRect liveRois[2] = {roi0, roi1};", body)
        self.assertIn("applyLiveHardwareRois(liveRois, &reason, actualRois)", body)
        self.assertIn("actualRoi0 = actualRois[0]", body)
        self.assertIn("actualRoi1 = actualRois[1]", body)

    def test_live_centroid_callback_does_not_use_software_roi_update_for_startup_commit(self):
        source = read("src/DIMM.cpp")
        body = source.split("&ImageProcessor::centroidReady", 1)[1].split(
            "connect(m_imageProcessor,\n            &ImageProcessor::differentialSampleReady", 1
        )[0]

        self.assertNotIn("!m_liveHardwareRoiActive &&", body)
        self.assertNotIn("updateMinuteRoi(true);\n            m_liveStartupPhase = LiveStartupPhase::Tracking;", body)


if __name__ == "__main__":
    unittest.main()

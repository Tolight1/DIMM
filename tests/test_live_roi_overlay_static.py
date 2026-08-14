from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class LiveRoiOverlayStaticTest(unittest.TestCase):
    def test_initial_roi_commit_updates_deferred_full_frame_preview_and_overlays(self):
        dimm_cpp = read("src/DIMM.LiveRoi.cpp")
        commit_body = dimm_cpp.split("bool DIMM::commitPairedInitialRoisIfReady()", 1)[1].split(
            "bool DIMM::startHardwarePulseStage", 1
        )[0]

        self.assertIn("showDeferredLiveRelocalizationPreview()", commit_body)

    def test_initial_roi_commit_uses_actual_hardware_roi_for_processing(self):
        dimm_cpp = read("src/DIMM.LiveRoi.cpp")
        commit_body = dimm_cpp.split("bool DIMM::commitPairedInitialRoisIfReady()", 1)[1].split(
            "bool DIMM::startHardwarePulseStage", 1
        )[0]

        self.assertIn("actualRois", commit_body)
        self.assertIn("applyLiveHardwareRois(pairedRois, &reason, actualRois)", commit_body)
        self.assertIn("m_imageProcessor->setPairRois(actualRois)", commit_body)
        self.assertLess(commit_body.find("applyLiveHardwareRois(pairedRois, &reason, actualRois)"),
                        commit_body.find("m_imageProcessor->setPairRois(actualRois)"))

    def test_overlay_update_is_not_gated_by_preview_interval(self):
        dimm_cpp = read("src/DIMM.LiveRoi.cpp")
        helper_body = dimm_cpp.split("void DIMM::updateFullFrameRoiOverlay(int cameraIndex)", 1)[1].split(
            "void DIMM::showDeferredLiveRelocalizationPreview()", 1
        )[0]

        self.assertIn("setRoiList", helper_body)
        self.assertNotIn("lastLivePreviewUpdateMs", helper_body)

        comm_cpp = read("src/DIMM.CommCamera.cpp")
        self.assertIn("kLiveFullFramePreviewIntervalMs", comm_cpp)

    def test_tracking_roi_update_does_not_refresh_full_frame_overlays_immediately(self):
        dimm_cpp = read("src/DIMM.LiveRoi.cpp")
        update_body = dimm_cpp.split("void DIMM::updateMinuteRoi", 1)[1].split(
            "void DIMM::hideLegacyRoiScheduleUi",
            1,
        )[0]

        success_pos = update_body.find("if (applyLiveHardwareRois(liveRois, &reason, actualRois))")
        self.assertGreaterEqual(success_pos, 0)
        self.assertNotIn("updateFullFrameRoiOverlay(0)", update_body)
        self.assertNotIn("updateFullFrameRoiOverlay(1)", update_body)

    def test_tracking_roi_update_uses_actual_hardware_roi_for_processing(self):
        dimm_cpp = read("src/DIMM.LiveRoi.cpp")
        update_body = dimm_cpp.split("void DIMM::updateMinuteRoi", 1)[1].split(
            "void DIMM::hideLegacyRoiScheduleUi",
            1,
        )[0]

        self.assertIn("actualRoi0", update_body)
        self.assertIn("actualRoi1", update_body)
        self.assertIn("RoiRect actualRois[2] = {actualRoi0, actualRoi1}", update_body)
        self.assertIn("const RoiRect liveRois[2] = {roi0, roi1}", update_body)
        self.assertIn("applyLiveHardwareRois(liveRois, &reason, actualRois)", update_body)
        self.assertIn("actualRoi0 = actualRois[0]", update_body)
        self.assertIn("actualRoi1 = actualRois[1]", update_body)
        self.assertIn("m_imageProcessor->setPairRois(actualRois)", update_body)


if __name__ == "__main__":
    unittest.main()

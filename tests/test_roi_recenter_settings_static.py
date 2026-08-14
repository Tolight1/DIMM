import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DIMM_CPP = ROOT / "src" / "DIMM.cpp"
DIMM_H = ROOT / "src" / "DIMM.h"
SETTINGS_CPP = ROOT / "src" / "SettingsDialog.cpp"
SETTINGS_H = ROOT / "src" / "SettingsDialog.h"


class RoiRecenterSettingsStaticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpp = DIMM_CPP.read_text(encoding="utf-8")
        cls.header = DIMM_H.read_text(encoding="utf-8")
        cls.settings_cpp = SETTINGS_CPP.read_text(encoding="utf-8")
        cls.settings_header = SETTINGS_H.read_text(encoding="utf-8")

    def test_settings_dialog_exposes_roi_recenter_fields(self):
        for field in [
            "roiRecenterThresholdEdit",
            "roiRecenterRequiredFramesEdit",
            "roiRecenterCooldownMsEdit",
            "roiRecenterMinimumShiftEdit",
        ]:
            self.assertIn(f"QLineEdit* {field} = nullptr;", self.settings_header)
            self.assertIn(field, self.settings_cpp)
        self.assertIn("ROI 重居中参数", self.settings_cpp)
        self.assertIn("距边缘阈值(px)", self.settings_cpp)

    def test_apply_settings_validates_and_emits_roi_recenter_parameters(self):
        apply_body = self.settings_cpp.split("bool SettingsDialog::applySettings()", 1)[1].split(
            "void SettingsDialog::setPulseGeneratorState", 1
        )[0]
        self.assertIn("roiRecenterThreshold", apply_body)
        self.assertIn("roiRecenterRequiredFrames", apply_body)
        self.assertIn("roiRecenterCooldownMs", apply_body)
        self.assertIn("roiRecenterMinimumShift", apply_body)
        self.assertIn("onApplyRoiRecentering", apply_body)

    def test_dimm_stores_runtime_roi_recenter_settings(self):
        for member in [
            "double m_roiRecenteringThresholdPx = 16.0;",
            "int m_roiRecenteringRequiredFrames = 5;",
            "qint64 m_roiRecenteringCooldownMs = 3000;",
            "double m_roiRecenteringMinimumShiftPx = 8.0;",
        ]:
            self.assertIn(member, self.header)

        config = (ROOT / "src" / "DIMM.Config.cpp").read_text(encoding="utf-8")
        setup_body = config.split("m_settingsDialog->onApplyProcessing", 1)[1].split(
            "m_settingsDialog->onApplyFullFrameStarDetection", 1
        )[0]
        self.assertIn("onApplyRoiRecentering", setup_body)
        self.assertIn("m_roiRecenteringThresholdPx = thresholdPx", setup_body)
        self.assertIn("m_roiRecenteringRequiredFrames = requiredFrames", setup_body)
        self.assertIn("m_roiRecenteringCooldownMs = cooldownMs", setup_body)
        self.assertIn("m_roiRecenteringMinimumShiftPx = minimumShiftPx", setup_body)

    def test_recenter_threshold_means_distance_to_roi_edge(self):
        live_cpp = (ROOT / "src" / "DIMM.LiveRoi.cpp").read_text(encoding="utf-8")
        edge_body = live_cpp.split("bool DIMM::isCentroidTooFarFromCurrentRoiCenter", 1)[1].split(
            "bool DIMM::shouldUpdateRoiForRecentering", 1
        )[0]
        decision_body = live_cpp.split("bool DIMM::shouldUpdateRoiForRecentering()", 1)[1].split(
            "void DIMM::requestLiveFullFrameRelocalization", 1
        )[0]
        self.assertIn("localX <= m_roiRecenteringThresholdPx", edge_body)
        self.assertIn("localY <= m_roiRecenteringThresholdPx", edge_body)
        self.assertIn("static_cast<double>(roi.w - 1) - m_roiRecenteringThresholdPx", edge_body)
        self.assertIn("static_cast<double>(roi.h - 1) - m_roiRecenteringThresholdPx", edge_body)
        self.assertNotIn("std::hypot(localX - static_cast<double>(roi.w - 1) * 0.5", edge_body)
        self.assertIn("m_roiRecenteringMinimumShiftPx", decision_body)
        self.assertIn("m_roiRecenteringRequiredFrames", decision_body)
        self.assertIn("m_roiRecenteringCooldownMs", decision_body)


if __name__ == "__main__":
    unittest.main()

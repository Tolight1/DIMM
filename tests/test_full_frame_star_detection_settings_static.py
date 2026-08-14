from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class FullFrameStarDetectionSettingsStaticTest(unittest.TestCase):
    def test_settings_dialog_exposes_full_frame_star_detection_fields(self):
        settings_h = read("src/SettingsDialog.h")
        settings_cpp = read("src/SettingsDialog.cpp")
        settings_ctor = settings_cpp.split("SettingsDialog::SettingsDialog", 1)[1].split(
            "auto* hotPixelGroup",
            1,
        )[0]

        for field in [
            "starThresholdAbsoluteEdit",
            "starSigmaThresholdEdit",
            "starPeakFractionEdit",
            "starMinimumIntensityEdit",
            "starMinAreaEdit",
            "starMaxAreaEdit",
        ]:
            self.assertIn(field, settings_h)
            self.assertIn(field, settings_ctor)

        self.assertIn("全画幅找星参数", settings_ctor)
        self.assertIn("绝对阈值", settings_ctor)
        self.assertIn("背景倍数", settings_ctor)
        self.assertIn("峰值比例", settings_ctor)
        self.assertIn("最低亮度", settings_ctor)
        self.assertIn("最小面积", settings_ctor)
        self.assertIn("最大面积", settings_ctor)

    def test_settings_apply_updates_runtime_full_frame_detection_config(self):
        dimm_cpp = read("src/DIMM.cpp")
        config_cpp = read("src/DIMM.Config.cpp")
        settings_h = read("src/SettingsDialog.h")
        settings_cpp = read("src/SettingsDialog.cpp")
        apply_body = settings_cpp.split("bool SettingsDialog::applySettings()", 1)[1].split(
            "void SettingsDialog::setPulseGeneratorState",
            1,
        )[0]
        settings_callback_body = config_cpp.split("m_settingsDialog->onApplyFullFrameStarDetection", 1)[1].split(
            "m_settingsDialog->onApplyHotPixelTemplates",
            1,
        )[0]

        self.assertIn("onApplyFullFrameStarDetection", settings_h)
        self.assertIn("starThresholdAbsoluteEdit->text().toDouble", apply_body)
        self.assertIn("starSigmaThresholdEdit->text().toDouble", apply_body)
        self.assertIn("starPeakFractionEdit->text().toDouble", apply_body)
        self.assertIn("starMinimumIntensityEdit->text().toDouble", apply_body)
        self.assertIn("starMinAreaEdit->text().toInt", apply_body)
        self.assertIn("starMaxAreaEdit->text().toInt", apply_body)
        self.assertIn("onApplyFullFrameStarDetection", apply_body)
        self.assertIn("setCurrentInitialStarDetectionConfig(config)", settings_callback_body)

    def test_detectors_use_runtime_config_not_static_file_cache(self):
        dimm_cpp = read("src/DIMM.cpp")
        dimm_config_cpp = read("src/DIMM.Config.cpp")
        detector_cpp = read("src/FullFrameStarDetector.cpp")
        config_cpp = read("src/InitialStarDetectionConfig.cpp")

        self.assertIn("currentInitialStarDetectionConfig()", detector_cpp)
        self.assertIn("setCurrentInitialStarDetectionConfig", dimm_config_cpp)
        self.assertNotIn("static const InitialStarDetectionConfig config = loadInitialStarDetectionConfig()", detector_cpp)
        self.assertIn("loadInitialStarDetectionConfig()", config_cpp)
        self.assertIn("InitialStarDetectionConfig scaledInitialStarDetectionConfigForMono8", detector_cpp)
        self.assertGreaterEqual(
            detector_cpp.count("InitialStarDetectionConfig config = scaledInitialStarDetectionConfigForMono8()"),
            2,
        )

    def test_full_frame_star_detector_is_extracted_from_dimm(self):
        cmake = read("CMakeLists.txt")
        dimm_cpp = read("src/DIMM.cpp")
        detector_h = read("src/FullFrameStarDetector.h")
        detector_cpp = read("src/FullFrameStarDetector.cpp")

        self.assertIn('#include "FullFrameStarDetector.h"', dimm_cpp)
        self.assertIn("src/FullFrameStarDetector.h", cmake)
        self.assertIn("src/FullFrameStarDetector.cpp", cmake)
        self.assertIn("QVector<PolarisDetectionPipeline::InitialStarCandidate> detectInitialStarCandidates", detector_h)
        self.assertIn("QVector<InitialStarCandidate> detectInitialStarCandidates", detector_cpp)
        self.assertNotIn("QVector<InitialStarCandidate> detectInitialStarCandidates", dimm_cpp)


if __name__ == "__main__":
    unittest.main()

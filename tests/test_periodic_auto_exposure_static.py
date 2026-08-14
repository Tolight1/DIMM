from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PeriodicAutoExposureStaticTest(unittest.TestCase):
    def test_auto_exposure_is_periodic_and_template_bound(self):
        header = read("src/DIMM.h")
        app_config = read("src/AppConfig.h")
        dimm_cpp = read("src/DIMM.cpp")
        config = read("src/DIMM.Config.cpp")
        auto_exp = read("src/DIMM.AutoExposure.cpp")
        helpers_h = read("src/DimmRuntimeHelpers.h")

        # Periodic auto-exposure state lives on the DIMM config and controller.
        self.assertIn("m_autoExposureConfig", header)
        self.assertIn("m_autoExposureController", header)
        self.assertIn("m_hotPixelTemplateExposureUs", header)
        self.assertIn("double medianOfSamples(QVector<double> samples)", helpers_h)
        self.assertIn("scanHotPixelExposureTemplates", config)

        # Periodic sampling is configured by sample interval and statistics window.
        self.assertIn("autoExposureSampleIntervalMs", app_config)
        self.assertIn("sampleWindowSec", app_config)
        self.assertIn("m_autoExposureConfig.autoExposureSampleIntervalMs", dimm_cpp)

        # Samples feed the auto-exposure state machine, which applies decisions.
        self.assertIn("void DIMM::handleAutoExposureSample", auto_exp)
        self.assertIn("applyExposureAndHotPixelTemplate(i, decision.camera[i].targetExposureUs",
                      auto_exp)

        # Template-bound exposure application selects a matching hot-pixel template.
        apply_body = config.split(
            "bool DIMM::applyExposureAndHotPixelTemplate(int cameraIndex, int exposureUs, QString* reason)", 1
        )[1].split("void DIMM::refreshHotPixelTemplates", 1)[0]
        self.assertIn("selectHotPixelTemplateExposureForCameraExposure", apply_body)
        self.assertIn("resolveHotPixelTemplatePathsForCameraExposure", apply_body)
        self.assertIn("m_hotPixelTemplateExposureUs[cameraIndex] = templateExposureUs", apply_body)

    def test_startup_hot_pixel_template_settings_helper_is_extracted(self):
        header = read("src/HotPixelTemplateSettings.h")
        helper = read("src/HotPixelTemplateSettings.cpp")
        dimm = read("src/DIMM.cpp")

        self.assertIn("struct HotPixelTemplateSettings", header)
        self.assertIn("bool loadHotPixelTemplateSettings(const QString& path", header)
        self.assertIn("bool loadHotPixelTemplateSettings(const QString& path", helper)
        self.assertIn("ConfigTextUtils::stripInlineComment(input.readLine())", helper)
        self.assertIn("camera_a_hot_pixel_mask", helper)
        self.assertIn('#include "HotPixelTemplateSettings.h"', dimm)
        self.assertIn("PathUtils::relativizePathToAppDir(hotSettings.camera0Mask)", dimm)

    def test_frame_cooldown_auto_exposure_is_removed(self):
        header = read("src/DIMM.h")
        source = read("src/DIMM.cpp")

        self.assertNotIn("m_autoExposureCooldownFrames", header)
        self.assertNotIn("lastAutoExposureFrame", header)
        self.assertNotIn("runtime.frameCount - runtime.lastAutoExposureFrame", source)

    def test_manual_exposure_apply_syncs_hot_pixel_template_paths(self):
        config = read("src/DIMM.Config.cpp")
        controller = read("src/ConfigApplicationController.cpp")

        # Manual exposure application stores the new exposure and applies it to cameras.
        camera_body = config.split("m_settingsDialog->onApplyCamera", 1)[1].split(
            "m_settingsDialog->onApplyAutoExposure", 1
        )[0]
        self.assertIn("m_configExposureUs = exposure", camera_body)
        self.assertIn("m_cameraExposureUs[0] = exposure", camera_body)
        self.assertIn("m_cameraManager->setExposure", camera_body)

        # Applying hot-pixel templates re-matches each camera's template against the
        # current exposure and reports the outcome.
        template_body = config.split("m_settingsDialog->onApplyHotPixelTemplates", 1)[1].split(
            "m_settingsDialog->onApplyOptics", 1
        )[0]
        self.assertIn("selectHotPixelTemplateExposureForCameraExposure(cameraIndex", template_body)
        self.assertIn("resolveHotPixelTemplatePathsForCameraExposure(cameraIndex", template_body)
        self.assertIn("热像素模板已按相机曝光自动匹配并启用", template_body)
        self.assertIn("部分相机当前曝光未找到对应热像素模板", template_body)

        # The settings apply pipeline runs camera apply (pre-validation) before hot-pixel
        # template apply (validated), so a fresh manual exposure re-matches templates.
        self.assertIn("callbacks.applyCamera(", controller)
        self.assertIn("callbacks.applyHotPixelTemplates(", controller)
        self.assertLess(controller.find("applyCamera("),
                        controller.find("applyHotPixelTemplates("))


if __name__ == "__main__":
    unittest.main()

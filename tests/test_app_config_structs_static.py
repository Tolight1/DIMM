from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AppConfigStructsStaticTest(unittest.TestCase):
    def test_simple_settings_config_structs_are_declared(self):
        header = read("src/AppConfig.h")

        self.assertIn("#pragma once", header)
        for struct_name in [
            "CameraConfig",
            "AutoExposureConfig",
            "ProcessingConfig",
            "RoiRecenteringConfig",
            "StarDetectionConfig",
            "HotPixelConfig",
            "OpticalConfig",
            "AlignmentConfig",
            "PolarisSolverSettingsConfig",
            "StorageConfig",
            "TriggerConfig",
            "PulseGeneratorConfig",
            "NetworkConfig",
            "AppConfig",
        ]:
            self.assertIn(f"struct {struct_name}", header)

        self.assertIn("CameraConfig camera", header)
        self.assertIn("AutoExposureConfig autoExposure", header)
        self.assertIn("ProcessingConfig processing", header)
        self.assertIn("RoiRecenteringConfig roiRecentering", header)
        self.assertIn("StarDetectionConfig starDetection", header)
        self.assertIn("HotPixelConfig hotPixel", header)
        self.assertIn("OpticalConfig optical", header)
        self.assertIn("AlignmentConfig alignment", header)
        self.assertIn("PolarisSolverSettingsConfig polarisSolver", header)
        self.assertIn("StorageConfig storage", header)
        self.assertIn("TriggerConfig trigger", header)
        self.assertIn("PulseGeneratorConfig pulseGenerator", header)
        self.assertIn("NetworkConfig network", header)

    def test_settings_dialog_uses_structs_without_changing_callbacks(self):
        settings_cpp = read("src/SettingsDialog.cpp")

        self.assertIn('#include "AppConfig.h"', settings_cpp)
        self.assertIn('#include "ConfigApplicationController.h"', settings_cpp)
        for local_name in [
            "const CameraConfig cameraConfig",
            "AutoExposureConfig autoExposureConfig",
            "const ProcessingConfig processingConfig",
            "const RoiRecenteringConfig roiRecenteringConfig",
            "const StarDetectionConfig starDetectionConfig",
            "const HotPixelConfig hotPixelConfig",
            "const OpticalConfig opticalConfig",
            "const AlignmentConfig alignmentConfig",
            "const PolarisSolverSettingsConfig polarisSolverConfig",
            "const StorageConfig storageConfig",
            "const TriggerConfig triggerConfig",
            "const PulseGeneratorConfig pulseGeneratorConfig",
            "const NetworkConfig networkConfig",
        ]:
            self.assertIn(local_name, settings_cpp)

        self.assertIn("ConfigApplicationCallbacks configCallbacks", settings_cpp)
        self.assertIn("ConfigApplicationController::applyPreValidationConfig(appConfig, configCallbacks)", settings_cpp)
        self.assertIn("ConfigApplicationController::applyValidatedConfig(appConfig, configCallbacks)", settings_cpp)
        self.assertIn("m_committedPulseFrequencyHz = appConfig.pulseGenerator.frequencyHz", settings_cpp)

    def test_settings_dialog_builds_app_config_aggregate_without_reordering_pulse_validation(self):
        settings_cpp = read("src/SettingsDialog.cpp")
        apply_body = settings_cpp.split("bool SettingsDialog::applySettings()", 1)[1]

        self.assertIn("AppConfig appConfig", apply_body)
        self.assertIn("appConfig.camera = cameraConfig", apply_body)
        self.assertIn("appConfig.pulseGenerator = pulseGeneratorConfig", apply_body)
        self.assertIn("ConfigApplicationController::applyPreValidationConfig(appConfig, configCallbacks)", apply_body)
        self.assertIn("ConfigApplicationController::applyValidatedConfig(appConfig, configCallbacks)", apply_body)
        self.assertLess(
            apply_body.find("ConfigApplicationController::applyPreValidationConfig(appConfig, configCallbacks)"),
            apply_body.find("const PulseGeneratorConfig pulseGeneratorConfig"),
        )
        self.assertLess(
            apply_body.find("const PulseGeneratorConfig pulseGeneratorConfig"),
            apply_body.find("appConfig.pulseGenerator = pulseGeneratorConfig"),
        )
        self.assertLess(
            apply_body.find("appConfig.pulseGenerator = pulseGeneratorConfig"),
            apply_body.find("ConfigApplicationController::applyValidatedConfig(appConfig, configCallbacks)"),
        )

    def test_cmake_registers_app_config_header(self):
        cmake = read("CMakeLists.txt")

        self.assertIn("src/AppConfig.h", cmake)
        self.assertIn("src/ConfigApplicationController.h", cmake)
        self.assertIn("src/ConfigApplicationController.cpp", cmake)


if __name__ == "__main__":
    unittest.main()

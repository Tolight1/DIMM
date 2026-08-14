from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ConfigApplicationControllerStaticTest(unittest.TestCase):
    def test_controller_declares_structured_callbacks_and_apply_steps(self):
        header = read("src/ConfigApplicationController.h")
        cpp = read("src/ConfigApplicationController.cpp")

        self.assertIn('#include "AppConfig.h"', header)
        self.assertIn("struct ConfigApplicationCallbacks", header)
        self.assertIn("applyPreValidationConfig", header)
        self.assertIn("applyValidatedConfig", header)
        self.assertIn("callbacks.applyCamera(config.camera.exposureUs", cpp)
        self.assertIn("callbacks.applyAutoExposure(config.autoExposure)", cpp)
        self.assertIn("callbacks.applyTriggerMode(config.trigger.mode)", cpp)
        self.assertIn("callbacks.applyProcessing(config.processing.backgroundKernelSize", cpp)
        self.assertIn("callbacks.applyPolarisSolver(config.polarisSolver.enabled", cpp)
        self.assertIn("config.storage.syncDiagnosticLoggingEnabled", cpp)
        self.assertIn("callbacks.applyNetwork(config.network.ip, config.network.port)", cpp)

    def test_settings_dialog_keeps_pulse_validation_between_apply_steps(self):
        settings_cpp = read("src/SettingsDialog.cpp")
        apply_body = settings_cpp.split("bool SettingsDialog::applySettings()", 1)[1].split(
            "void SettingsDialog::setPulseGeneratorState", 1
        )[0]

        self.assertLess(
            apply_body.find("ConfigApplicationController::applyPreValidationConfig"),
            apply_body.find("const PulseGeneratorConfig pulseGeneratorConfig"),
        )
        self.assertLess(
            apply_body.find("applyCommittedPulseSettings(true)"),
            apply_body.find("ConfigApplicationController::applyValidatedConfig"),
        )
        self.assertIn("if (onAfterApply)", apply_body)


if __name__ == "__main__":
    unittest.main()

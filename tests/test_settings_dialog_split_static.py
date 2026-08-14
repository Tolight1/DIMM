from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class SettingsDialogSplitStaticTest(unittest.TestCase):
    def test_settings_dialog_definition_lives_in_own_header(self):
        dimm_h = read("src/DIMM.h")
        settings_h = read("src/SettingsDialog.h")

        self.assertIn("#pragma once", settings_h)
        self.assertIn("class SettingsDialog : public QDialog", settings_h)
        self.assertIn("Q_OBJECT", settings_h)
        self.assertIn("enum class UiStatusLevel", settings_h)
        self.assertIn("std::function<void(double exposure, double gain, double continuousFrameRateHz)> onApplyCamera", settings_h)
        self.assertIn("QLineEdit* continuousFrameRateEdit = nullptr", settings_h)
        self.assertIn("QCheckBox* alignmentAllowSaturatedPolarisCheck = nullptr", settings_h)

        self.assertIn("class SettingsDialog;", dimm_h)
        self.assertIn("enum class UiStatusLevel;", dimm_h)
        self.assertNotIn("class SettingsDialog : public QDialog", dimm_h)
        self.assertNotIn("std::function<void(double exposure, double gain, double continuousFrameRateHz)> onApplyCamera", dimm_h)

    def test_settings_dialog_implementation_lives_in_own_cpp(self):
        dimm_cpp = read("src/DIMM.cpp")
        settings_cpp = read("src/SettingsDialog.cpp")

        self.assertIn('#include "SettingsDialog.h"', dimm_cpp)
        self.assertIn('#include "SettingsDialog.h"', settings_cpp)
        self.assertIn("SettingsDialog::SettingsDialog(QWidget* parent)", settings_cpp)
        self.assertIn("bool SettingsDialog::applySettings()", settings_cpp)
        self.assertIn("void SettingsDialog::setPulseGeneratorState", settings_cpp)
        self.assertNotIn("SettingsDialog::SettingsDialog(QWidget* parent)", dimm_cpp)
        self.assertNotIn("bool SettingsDialog::applySettings()", dimm_cpp)

    def test_cmake_explicitly_registers_settings_dialog_sources(self):
        cmake = read("CMakeLists.txt")

        self.assertIn("src/SettingsDialog.h", cmake)
        self.assertIn("src/SettingsDialog.cpp", cmake)


if __name__ == "__main__":
    unittest.main()

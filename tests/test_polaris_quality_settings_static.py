from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisQualitySettingsStaticTest(unittest.TestCase):
    def test_settings_dialog_exposes_polaris_quality_controls(self):
        header = read("src/SettingsDialog.h")
        cpp = read("src/SettingsDialog.cpp")

        self.assertIn("QLineEdit* alignmentMinSpatialSpreadEdit", header)
        self.assertIn("QLineEdit* alignmentMinPolarisSnrEdit", header)
        self.assertIn("QCheckBox* alignmentAllowSaturatedPolarisCheck", header)
        self.assertIn("最小空间跨度", cpp)
        self.assertIn("北极星最小 SNR", cpp)
        self.assertIn("允许饱和北极星自动确认", cpp)

    def test_settings_validation_and_apply_pass_quality_values(self):
        cpp = read("src/SettingsDialog.cpp")
        apply_body = cpp.split("bool SettingsDialog::applySettings", 1)[1].split(
            "void SettingsDialog::setPulseGeneratorState", 1
        )[0]

        self.assertIn("alignmentMinSpatialSpreadEdit->text().toDouble", apply_body)
        self.assertIn("alignmentMinPolarisSnrEdit->text().toDouble", apply_body)
        self.assertIn("最小空间跨度必须在 0 到 1000 px 之间", apply_body)
        self.assertIn("北极星最小 SNR 必须在 0 到 100 之间", apply_body)
        self.assertIn("alignmentAllowSaturatedPolarisCheck->isChecked()", apply_body)
        self.assertIn("alignmentMinSpatialSpread", apply_body)
        self.assertIn("alignmentMinPolarisSnr", apply_body)

    def test_dimm_stores_quality_settings_and_builds_solver_config(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.Alignment.cpp")
        config_cpp = read("src/DIMM.Config.cpp")
        callback_body = config_cpp.split("m_settingsDialog->onApplyPolarisSolver", 1)[1].split(
            "m_settingsDialog->onApplyStorage", 1
        )[0]
        config_body = cpp.split("PolarisSolverConfig DIMM::buildPolarisSolverConfig", 1)[1].split(
            "void DIMM::onPolarisSolveFinished", 1
        )[0]

        self.assertIn("double m_alignmentMinMatchedSpatialSpreadPx", header)
        self.assertIn("double m_alignmentMinPolarisSnr", header)
        self.assertIn("bool m_alignmentAllowSaturatedPolarisConfirmation", header)
        self.assertIn("m_alignmentMinMatchedSpatialSpreadPx = minMatchedSpatialSpreadPx", callback_body)
        self.assertIn("m_alignmentMinPolarisSnr = minPolarisSnr", callback_body)
        self.assertIn("m_alignmentAllowSaturatedPolarisConfirmation = allowSaturatedPolarisConfirmation", callback_body)
        self.assertIn("config.minMatchedSpatialSpreadPx = m_alignmentMinMatchedSpatialSpreadPx", config_body)
        self.assertIn("config.minPolarisSnr = m_alignmentMinPolarisSnr", config_body)
        self.assertIn("config.allowSaturatedPolarisConfirmation = m_alignmentAllowSaturatedPolarisConfirmation", config_body)


if __name__ == "__main__":
    unittest.main()

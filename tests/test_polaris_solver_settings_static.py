from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverSettingsStaticTest(unittest.TestCase):
    def test_settings_dialog_exposes_solver_controls(self):
        header = read("src/SettingsDialog.h")
        cpp = read("src/SettingsDialog.cpp")

        self.assertIn("onApplyPolarisSolver", header)
        self.assertIn("QCheckBox* alignmentAutoSolveCheck", header)
        self.assertIn("QLineEdit* alignmentMaxDetectedStarsEdit", header)
        self.assertIn("QLineEdit* alignmentMinMatchedStarsEdit", header)
        self.assertIn("QLineEdit* alignmentMaxRmsEdit", header)
        self.assertIn("QLineEdit* alignmentRetryIntervalEdit", header)
        self.assertIn("启用北极星自动识别", cpp)
        self.assertIn("最大参与匹配星数", cpp)
        self.assertIn("最少匹配星数", cpp)
        self.assertIn("最大匹配 RMS", cpp)
        self.assertIn("自动重试间隔", cpp)

    def test_settings_validation_and_apply_solver_values(self):
        cpp = read("src/SettingsDialog.cpp")
        apply_body = cpp.split("bool SettingsDialog::applySettings", 1)[1].split(
            "void SettingsDialog::setPulseGeneratorState", 1
        )[0]

        self.assertIn("alignmentMaxDetectedStarsEdit", apply_body)
        self.assertIn("alignmentMinMatchedStarsEdit", apply_body)
        self.assertIn("alignmentMaxRmsEdit", apply_body)
        self.assertIn("alignmentRetryIntervalEdit", apply_body)
        self.assertIn("最大参与匹配星数必须在 6 到 40 之间", apply_body)
        self.assertIn("最少匹配星数必须在 4 到最大参与匹配星数之间", apply_body)
        self.assertIn("最大匹配 RMS 必须在 0.5 到 10 px 之间", apply_body)
        self.assertIn("自动重试间隔必须在 1 到 30 秒之间", apply_body)
        self.assertIn("onApplyPolarisSolver", apply_body)

    def test_dimm_uses_solver_setting_members_to_build_config(self):
        header = read("src/DIMM.h")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        config_body = alignment_cpp.split("PolarisSolverConfig DIMM::buildPolarisSolverConfig", 1)[1].split(
            "void DIMM::onPolarisSolveFinished", 1
        )[0]

        self.assertIn("bool m_alignmentAutoSolveEnabled", header)
        self.assertIn("int m_alignmentMaxDetectedStars", header)
        self.assertIn("int m_alignmentMinMatchedStars", header)
        self.assertIn("double m_alignmentMaxRmsPx", header)
        self.assertIn("int m_alignmentRetryIntervalMs", header)
        self.assertIn("config.enabled = m_alignmentAutoSolveEnabled", config_body)
        self.assertIn("config.maxDetectedStars = m_alignmentMaxDetectedStars", config_body)
        self.assertIn("config.minMatchedStars = m_alignmentMinMatchedStars", config_body)
        self.assertIn("config.maxRmsPx = m_alignmentMaxRmsPx", config_body)
        self.assertIn("config.retryIntervalMs = m_alignmentRetryIntervalMs", config_body)

    def test_auto_polaris_solver_uses_full_frame_star_detection_settings(self):
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        config_body = alignment_cpp.split("PolarisSolverConfig DIMM::buildPolarisSolverConfig", 1)[1].split(
            "void DIMM::onPolarisSolveFinished", 1
        )[0]

        self.assertIn("currentInitialStarDetectionConfig()", config_body)
        self.assertIn("config.starThresholdAbsolute = starConfig.thresholdAbsolute >= 0.0", config_body)
        self.assertIn("config.starThresholdSigma = starConfig.sigmaThreshold", config_body)
        self.assertIn("config.starPeakFraction = starConfig.peakFraction", config_body)
        self.assertIn("config.starMinimumIntensity = starConfig.minimumIntensity", config_body)
        self.assertIn("config.minStarAreaPx = starConfig.minArea", config_body)
        self.assertIn("config.maxStarAreaPx = starConfig.maxArea", config_body)


if __name__ == "__main__":
    unittest.main()

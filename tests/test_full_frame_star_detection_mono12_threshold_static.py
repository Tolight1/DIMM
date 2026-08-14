from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class FullFrameStarDetectionMono12ThresholdStaticTest(unittest.TestCase):
    def test_settings_accept_mono12_threshold_range(self):
        source = read("src/SettingsDialog.cpp")
        apply_body = source.split("bool SettingsDialog::applySettings()", 1)[1].split(
            "void SettingsDialog::setPulseGeneratorState",
            1,
        )[0]

        self.assertIn("starThresholdAbsolute <= 4095.0", apply_body)
        self.assertIn("starMinimumIntensity > 4095.0", apply_body)
        self.assertIn("0 到 4095", apply_body)
        self.assertNotIn("0 到 255", apply_body)

    def test_initial_star_config_preserves_mono12_values_for_ui(self):
        source = read("src/InitialStarDetectionConfig.cpp")
        load_body = source.split("InitialStarDetectionConfig loadInitialStarDetectionConfig()", 1)[1].split(
            "InitialStarDetectionConfig sanitizeInitialStarDetectionConfig",
            1,
        )[0]
        sanitize_body = source.split("InitialStarDetectionConfig sanitizeInitialStarDetectionConfig", 1)[1].split(
            "InitialStarDetectionConfig& mutableInitialStarDetectionConfig",
            1,
        )[0]

        self.assertIn("config.thresholdAbsolute = number", load_body)
        self.assertIn("config.minimumIntensity = std::max(0.0, number)", load_body)
        self.assertIn("constexpr double kMono12MaxDn = 4095.0", source)
        self.assertIn("std::clamp(config.thresholdAbsolute, 0.0, kMono12MaxDn)", sanitize_body)
        self.assertIn("std::clamp(std::max(0.0, config.minimumIntensity), 0.0, kMono12MaxDn)", sanitize_body)
        self.assertNotIn("normalizeThresholdToMono8", load_body)
        self.assertNotIn("normalizeThresholdToMono8", sanitize_body)

    def test_full_frame_detectors_scale_mono12_config_to_mono8_working_frame(self):
        source = read("src/FullFrameStarDetector.cpp")

        self.assertIn("InitialStarDetectionConfig scaledInitialStarDetectionConfigForMono8", source)
        self.assertIn("ImageUtils::normalizeThresholdToMono8(config.thresholdAbsolute)", source)
        self.assertIn("ImageUtils::normalizeThresholdToMono8(config.minimumIntensity)", source)
        self.assertGreaterEqual(source.count("scaledInitialStarDetectionConfigForMono8("), 3)


if __name__ == "__main__":
    unittest.main()

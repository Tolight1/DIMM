from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class FullFrameStarDetectorConfigInjectionStaticTest(unittest.TestCase):
    def test_detector_exposes_explicit_config_overload(self):
        header = read("src/FullFrameStarDetector.h")
        cpp = read("src/FullFrameStarDetector.cpp")

        self.assertIn('#include "InitialStarDetectionConfig.h"', header)
        self.assertIn(
            "detectInitialStarCandidates(const cv::Mat& grayscale,",
            header,
        )
        self.assertIn("const InitialStarDetectionConfig& config", header)
        self.assertIn(
            "detectInitialStarCandidates(const cv::Mat& grayscale,",
            cpp,
        )
        self.assertIn("const InitialStarDetectionConfig& config", cpp)
        self.assertIn(
            "return detectInitialStarCandidates(grayscale, currentInitialStarDetectionConfig(), peakValue, thresholdValue);",
            cpp,
        )

    def test_explicit_overload_uses_passed_config_for_threshold_and_area(self):
        cpp = read("src/FullFrameStarDetector.cpp")
        explicit_body = cpp.split(
            "QVector<InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,",
            1,
        )[1].split(
            "bool detectRawInitialStarPeakCandidate",
            1,
        )[0]

        self.assertIn("config.minimumIntensity", explicit_body)
        self.assertIn("config.sigmaThreshold", explicit_body)
        self.assertIn("config.peakFraction", explicit_body)
        self.assertIn("config.thresholdAbsolute", explicit_body)
        self.assertIn("area < config.minArea", explicit_body)
        self.assertIn("area > config.maxArea", explicit_body)
        self.assertNotIn("currentInitialStarDetectionConfig()", explicit_body)

if __name__ == "__main__":
    unittest.main()

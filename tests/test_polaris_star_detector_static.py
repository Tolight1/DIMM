from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisStarDetectorStaticTest(unittest.TestCase):
    def test_solver_exposes_frame_star_detection(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")
        image_utils = read("src/ImageUtils.cpp")

        self.assertIn("double starThresholdSigma", header)
        self.assertIn("double starThresholdAbsolute", header)
        self.assertIn("double starPeakFraction", header)
        self.assertIn("int minStarAreaPx", header)
        self.assertIn("int maxStarAreaPx", header)
        self.assertIn("QVector<DetectedStar> detectStarsFromFrame", header)
        self.assertIn('#include "ImageUtils.h"', cpp)
        self.assertIn("cv::cvtColor", image_utils)
        self.assertIn("ImageUtils::grayscaleDetectionFrame(correctedFrame)", cpp)
        self.assertNotIn("ImageUtils::normalizeDetectionFrame(grayscale)", cpp)
        self.assertIn("const double threshold = config.starThresholdAbsolute >= 0.0", cpp)
        self.assertIn("rawIntensityAt(grayscale, y, x)", cpp)
        self.assertIn("cv::connectedComponentsWithStats", cpp)
        self.assertIn("background", cpp)
        self.assertIn("snr", cpp)
        self.assertIn("saturated", cpp)

    def test_frame_solver_uses_detector_then_existing_solver(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")

        self.assertIn("PolarisSolveResult solveFrame", header)
        self.assertIn("detectStarsFromFrame(frame, config, cancelled)", cpp)
        self.assertIn("solveDetectedStars(detections, config, cancelled)", cpp)
        self.assertIn("PolarisSolveStatus::InsufficientStars", cpp)
        self.assertIn("detectedStarCount", cpp)


if __name__ == "__main__":
    unittest.main()

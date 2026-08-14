from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisStarDetectorPrecisionStaticTest(unittest.TestCase):
    def test_detector_uses_intensity_weighted_centroid_and_raw_saturation(self):
        cpp = read("src/PolarisSolver.cpp")
        detector_body = cpp.split("QVector<DetectedStar> detectStarsFromFrame", 1)[1].split(
            "PolarisSolveResult solveFrame", 1
        )[0]

        self.assertIn("rawSaturationLevelForDepth", cpp)
        self.assertIn("rawSaturationValue", detector_body)
        self.assertIn("componentWeightedX", detector_body)
        self.assertIn("componentWeightedY", detector_body)
        self.assertIn("const double signal = std::max(0.0, value - background)", detector_body)
        self.assertIn("detection.centroidPx = componentFlux", detector_body)
        self.assertNotIn("detection.centroidPx = QPointF(centroids.at<double>", detector_body)
        self.assertNotIn("value >= 254.0", detector_body)


if __name__ == "__main__":
    unittest.main()

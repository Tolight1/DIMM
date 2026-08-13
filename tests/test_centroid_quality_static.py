from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class CentroidQualityStaticTest(unittest.TestCase):
    def test_quality_enum_and_measurement_result_are_declared_without_replacing_signals(self):
        header = read("src/ImageProcessor.h")

        self.assertIn("enum class CentroidQuality", header)
        for quality in [
            "Valid",
            "InvalidCamera",
            "EmptyImage",
            "BelowThreshold",
            "LowFlux",
            "TooFewPixels",
            "TooManyPixels",
            "NearRoiEdge",
            "EdgeSignal",
            "OutOfFrame",
        ]:
            self.assertIn(quality, header)
        self.assertIn("struct CentroidMeasurement", header)
        self.assertIn("CentroidResult centroid", header)
        self.assertIn("return quality == CentroidQuality::Valid", header)
        self.assertIn("void centroidReady(int cameraIndex", header)
        self.assertIn("void frameProcessed(int cameraIndex, bool centroidValid", header)


if __name__ == "__main__":
    unittest.main()

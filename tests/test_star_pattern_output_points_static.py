from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class StarPatternOutputPointsStaticTest(unittest.TestCase):
    def test_match_result_reports_ncp_and_polaris_pixels(self):
        header = read("src/StarPatternMatcher.h")
        cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("hasNorthCelestialPolePixel", header)
        self.assertIn("northCelestialPolePixel", header)
        self.assertIn("hasPolarisPixel", header)
        self.assertIn("polarisPixel", header)
        self.assertIn("polarisPolarRadiusPx", header)
        self.assertIn("annotateSolvedReferencePoints", cpp)
        self.assertIn("QPointF(0.0, 0.0)", cpp)
        self.assertIn("catalogPoints[i].isPolaris", cpp)
        self.assertIn("distanceBetween(result.polarisPixel", cpp)


if __name__ == "__main__":
    unittest.main()

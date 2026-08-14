from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverEngineStaticTest(unittest.TestCase):
    def test_solver_builds_catalog_points_and_runs_matcher(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")

        self.assertIn("double observationEpochYear", header)
        self.assertIn("PolarisSolveResult solveDetectedStars", header)
        self.assertIn("buildPatternCatalogPoints", cpp)
        self.assertIn("northPolePlaneAtEpoch", cpp)
        self.assertIn("PatternMatcherConfig matcherConfig", cpp)
        self.assertIn("StarPatternMatcher matcher", cpp)
        self.assertIn("matcher.matchDetectedToCatalog", cpp)
        self.assertIn("PolarisSolveStatus::Solved", cpp)
        self.assertIn("PolarisSolveStatus::NoCatalogMatch", cpp)

    def test_solver_maps_matches_to_public_result(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")

        self.assertIn("bool hasPolarisPixel", header)
        self.assertIn("bool hasNorthCelestialPolePixel", header)
        self.assertIn("CatalogImageMatch outputMatch", cpp)
        self.assertIn("matchedCatalog.sourceId", cpp)
        self.assertIn("matchedDetected.centroidPx", cpp)
        self.assertIn("result.hasPolarisPixel = matchResult.hasPolarisPixel", cpp)
        self.assertIn("result.hasNorthCelestialPolePixel = matchResult.hasNorthCelestialPolePixel", cpp)
        self.assertIn("result.polarisPixel = matchResult.polarisPixel", cpp)
        self.assertIn("result.northCelestialPolePixel = matchResult.northCelestialPolePixel", cpp)
        self.assertIn("result.plateScaleArcsecPx", cpp)
        self.assertIn("qRadiansToDegrees", cpp)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class StarPatternMatcherContractStaticTest(unittest.TestCase):
    def test_matcher_has_ransac_ready_contract(self):
        header = read("src/StarPatternMatcher.h")
        cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("struct PatternCatalogPoint", header)
        self.assertIn("struct PatternMatchPair", header)
        self.assertIn("struct PatternMatchResult", header)
        self.assertIn("class StarPatternMatcher", header)
        self.assertIn("matchPoints(const QVector<QPointF>& detectedPoints", header)
        self.assertIn("collectNearestOneToOneMatches", cpp)
        self.assertIn("computeRmsPx", cpp)
        self.assertIn("usedDetected", cpp)
        self.assertIn("usedCatalog", cpp)
        self.assertIn("maxResidualPx", cpp)
        self.assertIn("matchedCount", cpp)

    def test_solver_still_does_not_submit_matcher_runtime_work(self):
        solver_cpp = read("src/PolarisSolver.cpp")

        self.assertIn('#include "StarPatternMatcher.h"', solver_cpp)
        self.assertNotIn(".matchPoints(", solver_cpp)


if __name__ == "__main__":
    unittest.main()

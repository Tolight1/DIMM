from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class StarPatternConfidenceStaticTest(unittest.TestCase):
    def test_matcher_reports_best_second_best_and_score_margin(self):
        header = read("src/StarPatternMatcher.h")
        cpp = read("src/StarPatternMatcher.cpp")
        match_body = cpp.split("PatternMatchResult StarPatternMatcher::matchDetectedToCatalog", 1)[1].split(
            "PatternMatchResult StarPatternMatcher::matchPoints", 1
        )[0]
        solver_body = read("src/PolarisSolver.cpp").split("PolarisSolveResult solveDetectedStars", 1)[1].split(
            "PolarisSolverController::PolarisSolverController", 1
        )[0]

        self.assertIn("double bestScore", header)
        self.assertIn("double secondBestScore", header)
        self.assertIn("double scoreMargin", header)
        self.assertIn("double minScoreMargin", header)
        self.assertIn("secondBestClusterScore", match_body)
        self.assertIn("bestResult.secondBestScore = secondBestClusterScore", match_body)
        self.assertIn("bestResult.scoreMargin = bestScore - secondBestClusterScore", match_body)
        self.assertIn("solutionClusters", match_body)
        self.assertIn("config.minScoreMargin", solver_body)
        self.assertIn("PolarisSolveStatus::LowConfidence", solver_body)


if __name__ == "__main__":
    unittest.main()

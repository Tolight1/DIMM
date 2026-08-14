from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class StarPatternSpatialConfidenceStaticTest(unittest.TestCase):
    def test_match_result_reports_max_residual_and_spatial_spread(self):
        header = read("src/StarPatternMatcher.h")
        cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("double maxResidualPx", header)
        self.assertIn("double matchedSpatialSpreadPx", header)
        self.assertIn("computeMaxResidualPx", cpp)
        self.assertIn("computeMatchedSpatialSpreadPx", cpp)
        self.assertIn("result.maxResidualPx = computeMaxResidualPx(result.pairs)", cpp)
        self.assertIn("result.matchedSpatialSpreadPx = computeMatchedSpatialSpreadPx(result.pairs)", cpp)

    def test_solver_rejects_spatially_collapsed_matches_as_low_confidence(self):
        solver_header = read("src/PolarisSolver.h")
        solver_cpp = read("src/PolarisSolver.cpp")
        solve_body = solver_cpp.split("PolarisSolveResult solveDetectedStars", 1)[1].split(
            "PolarisSolverController::PolarisSolverController", 1
        )[0]

        self.assertIn("double minMatchedSpatialSpreadPx", solver_header)
        self.assertIn("double matchedSpatialSpreadPx", solver_header)
        self.assertIn("matcherConfig.minMatchedSpatialSpreadPx = config.minMatchedSpatialSpreadPx", solver_cpp)
        self.assertIn("result.matchedSpatialSpreadPx = matchResult.matchedSpatialSpreadPx", solve_body)
        self.assertIn("matchResult.matchedSpatialSpreadPx < config.minMatchedSpatialSpreadPx", solve_body)
        self.assertIn("匹配星空间分布不足", solve_body)


if __name__ == "__main__":
    unittest.main()

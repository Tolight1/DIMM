from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class StarPatternSolutionClusteringStaticTest(unittest.TestCase):
    def test_matcher_clusters_physical_solutions_before_score_margin(self):
        header = read("src/StarPatternMatcher.h")
        cpp = read("src/StarPatternMatcher.cpp")
        match_body = cpp.split("PatternMatchResult StarPatternMatcher::matchDetectedToCatalog", 1)[1].split(
            "PatternMatchResult StarPatternMatcher::matchPoints", 1
        )[0]

        self.assertIn("int solutionClusterCount", header)
        self.assertIn("int bestSolutionSupportCount", header)
        self.assertIn("int secondBestSolutionSupportCount", header)
        self.assertIn("struct SolutionSignature", cpp)
        self.assertIn("struct SolutionCluster", cpp)
        self.assertIn("samePhysicalSolution", cpp)
        self.assertIn("addResultToSolutionClusters", cpp)
        self.assertIn("QVector<SolutionCluster> solutionClusters", match_body)
        self.assertIn("addResultToSolutionClusters", match_body)
        self.assertIn("bestResult.secondBestScore = secondBestClusterScore", match_body)
        self.assertIn("bestResult.scoreMargin = bestScore - secondBestClusterScore", match_body)

    def test_solver_stats_and_log_report_solution_cluster_diagnostics(self):
        solver_h = read("src/PolarisSolver.h")
        solver_cpp = read("src/PolarisSolver.cpp")
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")

        self.assertIn("int solutionClusterCount", solver_h)
        self.assertIn("int bestSolutionSupportCount", solver_h)
        self.assertIn("int secondBestSolutionSupportCount", solver_h)
        self.assertIn("result.stats.solutionClusterCount = matchResult.solutionClusterCount", solver_cpp)
        self.assertIn("result.stats.bestSolutionSupportCount = matchResult.bestSolutionSupportCount", solver_cpp)
        self.assertIn("result.stats.secondBestSolutionSupportCount = matchResult.secondBestSolutionSupportCount", solver_cpp)
        self.assertIn("result.stats.solutionClusterCount", presenter_cpp)
        self.assertIn("result.stats.bestSolutionSupportCount", presenter_cpp)
        self.assertIn("result.stats.secondBestSolutionSupportCount", presenter_cpp)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolvePerformanceStatsStaticTest(unittest.TestCase):
    def test_matcher_reports_candidate_volume_statistics(self):
        header = read("src/StarPatternMatcher.h")
        cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("int catalogTriangleCount", header)
        self.assertIn("int imageTriangleCount", header)
        self.assertIn("int candidateTriangleCount", header)
        self.assertIn("int testedTransformCount", header)
        self.assertIn("bestResult.catalogTriangleCount", cpp)
        self.assertIn("++bestResult.imageTriangleCount", cpp)
        self.assertIn("bestResult.candidateTriangleCount += catalogCandidates.size()", cpp)
        self.assertIn("bestResult.testedTransformCount = testedTransformCount", cpp)

    def test_solver_result_carries_matcher_statistics(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")

        self.assertIn("struct PolarisSolveStats", header)
        self.assertIn("int catalogStarCount", header)
        self.assertIn("int catalogTriangleCount", header)
        self.assertIn("int imageTriangleCount", header)
        self.assertIn("int candidateTriangleCount", header)
        self.assertIn("int testedTransformCount", header)
        self.assertIn("PolarisSolveStats stats", header)
        self.assertIn("result.stats.catalogStarCount = catalogPoints.size()", cpp)
        self.assertIn("result.stats.catalogTriangleCount = matchResult.catalogTriangleCount", cpp)
        self.assertIn("result.stats.imageTriangleCount = matchResult.imageTriangleCount", cpp)
        self.assertIn("result.stats.candidateTriangleCount = matchResult.candidateTriangleCount", cpp)
        self.assertIn("result.stats.testedTransformCount = matchResult.testedTransformCount", cpp)

    def test_dimm_debug_log_includes_performance_statistics(self):
        cpp = read("src/AlignmentUiPresenter.cpp")
        log_body = cpp.split("QString AlignmentUiPresenter::formatPolarisSolveLogLine", 1)[1]

        self.assertIn("catalogStars=%15", log_body)
        self.assertIn("catalogTriangles=%16", log_body)
        self.assertIn("imageTriangles=%17", log_body)
        self.assertIn("candidateTriangles=%18", log_body)
        self.assertIn("testedTransforms=%19", log_body)
        self.assertIn("result.stats.catalogStarCount", log_body)
        self.assertIn("result.stats.catalogTriangleCount", log_body)
        self.assertIn("result.stats.imageTriangleCount", log_body)
        self.assertIn("result.stats.candidateTriangleCount", log_body)
        self.assertIn("result.stats.testedTransformCount", log_body)


if __name__ == "__main__":
    unittest.main()

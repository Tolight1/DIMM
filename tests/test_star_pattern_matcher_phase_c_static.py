from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class StarPatternMatcherPhaseCStaticTest(unittest.TestCase):
    def test_matcher_defines_triangle_and_similarity_foundation(self):
        header = read("src/StarPatternMatcher.h")
        cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("struct TriangleInvariant", header)
        self.assertIn("struct SimilarityTransform2D", header)
        self.assertIn("makeTriangleInvariant", header)
        self.assertIn("applySimilarityTransform", header)
        self.assertIn("triangleSignedArea", cpp)
        self.assertIn("std::sort", cpp)
        self.assertIn("r1", cpp)
        self.assertIn("r2", cpp)
        self.assertIn("mirrored", cpp)

    def test_solver_knows_matcher_header_without_running_it_yet(self):
        solver_cpp = read("src/PolarisSolver.cpp")

        self.assertIn('#include "StarPatternMatcher.h"', solver_cpp)
        submit_body = solver_cpp.split("void PolarisSolverController::submitFrame", 1)[1].split(
            "void PolarisSolverController::cancelAll", 1
        )[0]
        self.assertNotIn("StarPatternMatcher matcher", submit_body)
        self.assertNotIn("solveDetectedStars", submit_body)


if __name__ == "__main__":
    unittest.main()

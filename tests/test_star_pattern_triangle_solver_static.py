from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class StarPatternTriangleSolverStaticTest(unittest.TestCase):
    def test_matcher_can_generate_initial_transforms_from_indexed_triangles(self):
        header = read("src/StarPatternMatcher.h")
        cpp = read("src/StarPatternMatcher.cpp")
        match_body = cpp.split("PatternMatchResult StarPatternMatcher::matchDetectedToCatalog", 1)[1].split(
            "PatternMatchResult StarPatternMatcher::matchPoints", 1
        )[0]

        self.assertIn("struct PatternMatcherConfig", header)
        self.assertIn("ratioTolerance", header)
        self.assertIn("minTriangleLongestPx", header)
        self.assertIn("maxCatalogTrianglesPerImageTriangle", header)
        self.assertIn("maxTestedTransforms", header)
        self.assertIn("matchDetectedToCatalog", header)
        self.assertIn("makeSimilarityTransformFromPairs", header)
        self.assertIn("struct TriangleKey", cpp)
        self.assertIn("struct CatalogTriangleRecord", cpp)
        self.assertIn("buildCatalogTriangleIndex", cpp)
        self.assertIn("findCatalogTriangleCandidates", cpp)
        self.assertIn("forEachTriangle", cpp)
        self.assertIn("testedTransformCount", match_body)
        self.assertIn("considerCandidateTransform", match_body)
        self.assertNotIn("QVector<SimilarityTransform2D> candidateTransforms", match_body)
        self.assertNotIn("forEachTriangle(catalogPoints.size()", match_body)
        self.assertIn("permutation", cpp)
        self.assertIn("estimatePlateScaleArcsecPx", cpp)
        self.assertIn("scoreResult", cpp)

    def test_triangle_solver_checks_scale_range_and_mirror_variants(self):
        cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("config.minPlateScaleArcsecPx", cpp)
        self.assertIn("config.maxPlateScaleArcsecPx", cpp)
        self.assertIn("mirrored = false", cpp)
        self.assertIn("mirrored = true", cpp)
        self.assertIn("bestScore", cpp)
        self.assertIn("matchedCount * 100.0", cpp)


if __name__ == "__main__":
    unittest.main()

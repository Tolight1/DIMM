from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class StarPatternSimilarityRefineStaticTest(unittest.TestCase):
    def test_matcher_refits_similarity_transform_from_inliers(self):
        header = read("src/StarPatternMatcher.h")
        cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("fitSimilarityTransform", header)
        self.assertIn("minMatchedStars", header)
        self.assertIn("refinedTransform", cpp)
        self.assertIn("weightedSourceNorm", cpp)
        self.assertIn("cross =", cpp)
        self.assertIn("dot =", cpp)
        self.assertIn("std::atan2", cpp)
        self.assertIn("collectNearestOneToOneMatches", cpp)
        self.assertIn("if (initialPairs.size() < minMatchedStars)", cpp)
        self.assertIn("result.valid = result.matchedCount >= minMatchedStars", cpp)

    def test_match_points_keeps_mirror_flag_when_refining(self):
        cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("refinedTransform.mirrored = initialTransform.mirrored", cpp)
        self.assertIn("sourceX = transform.mirrored ? -point.x() : point.x()", cpp)


if __name__ == "__main__":
    unittest.main()

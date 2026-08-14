from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class StarPatternOneToOneMatchingStaticTest(unittest.TestCase):
    def test_collect_matches_uses_sorted_candidate_edges(self):
        cpp = read("src/StarPatternMatcher.cpp")
        body = cpp.split("QVector<PatternMatchPair> StarPatternMatcher::collectNearestOneToOneMatches", 1)[1].split(
            "SimilarityTransform2D StarPatternMatcher::fitSimilarityTransform", 1
        )[0]

        self.assertIn("struct MatchCandidateEdge", cpp)
        self.assertIn("QVector<MatchCandidateEdge> candidateEdges", body)
        self.assertIn("usedCatalog", body)
        self.assertIn("usedDetected", body)
        self.assertIn("std::sort(candidateEdges.begin()", body)
        self.assertIn("edge.residualPx", body)
        self.assertIn("if (usedCatalog[edge.catalogIndex] || usedDetected[edge.detectedIndex])", body)
        self.assertNotIn("for (int catalogIndex = 0; catalogIndex < catalogPoints.size(); ++catalogIndex)", body)


if __name__ == "__main__":
    unittest.main()

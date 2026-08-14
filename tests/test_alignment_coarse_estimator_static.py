from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class AlignmentCoarseEstimatorStaticTest(unittest.TestCase):
    def test_estimator_files_are_build_units_and_do_not_use_polaris_solver(self):
        self.assertTrue((ROOT / "src/AlignmentCoarseEstimator.h").exists())
        self.assertTrue((ROOT / "src/AlignmentCoarseEstimator.cpp").exists())
        cmake = read("CMakeLists.txt")
        header = read("src/AlignmentCoarseEstimator.h")
        cpp = read("src/AlignmentCoarseEstimator.cpp")

        self.assertIn("src/AlignmentCoarseEstimator.h", cmake)
        self.assertIn("src/AlignmentCoarseEstimator.cpp", cmake)
        self.assertNotIn("PolarisSolver", header)
        self.assertNotIn("PolarisSolver", cpp)
        self.assertIn("struct CoarseAlignmentConfig", header)
        self.assertIn("struct CoarseAlignmentEstimate", header)
        self.assertIn("class AlignmentCoarseTracker", header)

    def test_estimator_computes_velocity_and_ncp_center(self):
        cpp = read("src/AlignmentCoarseEstimator.cpp")
        self.assertIn("fitTrackVelocity", cpp)
        self.assertIn("vxNumerator", cpp)
        self.assertIn("vyNumerator", cpp)
        self.assertIn("durationSec", cpp)
        self.assertIn("speedPxSec", cpp)
        self.assertIn("solveNorthCelestialPoleCenter", cpp)
        self.assertIn("a00 += weight * vx * vx", cpp)
        self.assertIn("a01 += weight * vx * vy", cpp)
        self.assertIn("a11 += weight * vy * vy", cpp)
        self.assertIn("det = a00 * a11 - a01 * a01", cpp)
        self.assertIn("offsetDeg", cpp)
        self.assertIn("siderealArcsecSec", cpp)

    def test_estimator_has_candidate_and_track_limits(self):
        header = read("src/AlignmentCoarseEstimator.h")
        cpp = read("src/AlignmentCoarseEstimator.cpp")
        for token in [
            "maxCandidates",
            "maxAssociationDistancePx",
            "minTrackDurationSec",
            "minTrackPoints",
            "minTrackDisplacementPx",
            "maxTrackFitRmsPx",
            "maxStaleTrackSec",
        ]:
            self.assertIn(token, header)
            self.assertIn(token, cpp)

    def test_estimator_uses_same_type_for_track_count_max(self):
        cpp = read("src/AlignmentCoarseEstimator.cpp")

        self.assertIn("std::max<qsizetype>(1, tracks.size())", cpp)
        self.assertNotIn("std::max(1, tracks.size())", cpp)

if __name__ == "__main__":
    unittest.main()

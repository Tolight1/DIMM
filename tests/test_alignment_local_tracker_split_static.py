from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentLocalTrackerSplitStaticTest(unittest.TestCase):
    def test_local_tracker_module_is_build_unit(self):
        cmake = read("CMakeLists.txt")
        self.assertIn("src/AlignmentLocalTracker.h", cmake)
        self.assertIn("src/AlignmentLocalTracker.cpp", cmake)

    def test_local_tracker_owns_roi_crop_and_centroid_offset(self):
        header = read("src/AlignmentLocalTracker.h")
        cpp = read("src/AlignmentLocalTracker.cpp")
        dimm = read("src/DIMM.cpp")
        track_body = dimm.split("bool DIMM::trackAlignmentPolarisLocally", 1)[1].split(
            "void DIMM::logPolarisSolveResult", 1
        )[0]

        self.assertIn("using CentroidDetector", header)
        self.assertIn("trackInWindow", header)
        self.assertIn("AlignmentLocalTracker::trackInWindow", cpp)
        self.assertIn("cv::cvtColor", cpp)
        self.assertIn("grayscale(roi)", cpp)
        self.assertIn("localCentroid.x() + trackingWindow.x()", cpp)
        self.assertIn("AlignmentLocalTracker::trackFromConfirmedPosition", track_body)
        self.assertNotIn("cv::cvtColor", track_body)
        self.assertNotIn("grayscale(roi)", track_body)


if __name__ == "__main__":
    unittest.main()

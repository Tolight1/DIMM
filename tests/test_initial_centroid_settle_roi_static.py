import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DIMM_CPP = ROOT / "src" / "DIMM.cpp"
DIMM_H = ROOT / "src" / "DIMM.h"


class InitialCentroidSettleRoiStaticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpp = DIMM_CPP.read_text(encoding="utf-8")
        cls.header = DIMM_H.read_text(encoding="utf-8")

    def test_runtime_does_not_track_one_shot_initial_centroid_settle(self):
        self.assertNotIn("initialCentroidSettlePending", self.header)
        self.assertNotIn("initialCentroidSettleFrameCount", self.header)
        self.assertNotIn("tryApplyInitialCentroidSettleRoi", self.header)
        self.assertNotIn("kInitialCentroidSettleRequiredFrames", self.cpp)

    def test_initial_or_relocalized_roi_commit_does_not_arm_settle_pass(self):
        commit_body = (ROOT / "src" / "DIMM.LiveRoi.cpp").read_text(encoding="utf-8").split(
            "bool DIMM::commitPairedInitialRoisIfReady()", 1
        )[1].split(
            "bool DIMM::startHardwarePulseStage", 1
        )[0]
        self.assertNotIn("initialCentroidSettlePending", commit_body)
        self.assertNotIn("initialCentroidSettleFrameCount", commit_body)

    def test_centroid_callback_runs_normal_recenter_directly(self):
        callback_body = self.cpp.split("&ImageProcessor::centroidReady", 1)[1].split(
            "connect(m_imageProcessor,\n            &ImageProcessor::differentialSampleReady",
            1,
        )[0]
        self.assertNotIn("tryApplyInitialCentroidSettleRoi", callback_body)
        self.assertIn("if (shouldUpdateRoiForRecentering())", callback_body)

    def test_initial_centroid_settle_reason_is_removed(self):
        self.assertNotIn("initial_centroid_settle", self.cpp)


if __name__ == "__main__":
    unittest.main()

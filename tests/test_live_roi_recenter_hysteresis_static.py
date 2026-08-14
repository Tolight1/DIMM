import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DIMM_CPP = ROOT / "src" / "DIMM.cpp"
DIMM_H = ROOT / "src" / "DIMM.h"


class LiveRoiRecenterHysteresisStaticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpp = (ROOT / "src" / "DIMM.LiveRoi.cpp").read_text(encoding="utf-8")
        cls.header = DIMM_H.read_text(encoding="utf-8")

    def test_recenter_policy_has_hysteresis_constants(self):
        self.assertIn("m_roiRecenteringThresholdPx = 16.0", self.header)
        self.assertIn("m_roiRecenteringRequiredFrames = 5", self.header)
        self.assertIn("m_roiRecenteringCooldownMs = 3000", self.header)
        self.assertIn("m_roiRecenteringMinimumShiftPx = 8.0", self.header)

    def test_runtime_tracks_consecutive_recenter_candidates(self):
        self.assertIn("int roiRecenteringCandidateFrameCount = 0;", self.header)
        self.assertIn("runtime.roiRecenteringCandidateFrameCount = 0;", self.cpp)

    def test_recenter_decision_is_gated_before_update_minute_roi(self):
        body = self.cpp.split("bool DIMM::shouldUpdateRoiForRecentering()", 1)[1].split(
            "void DIMM::requestLiveFullFrameRelocalization", 1
        )[0]
        self.assertIn("++runtime.roiRecenteringCandidateFrameCount", body)
        self.assertIn("m_roiRecenteringRequiredFrames", body)
        self.assertIn("m_roiRecenteringCooldownMs", body)
        self.assertIn("m_roiRecenteringMinimumShiftPx", body)
        self.assertIn("maximumRoiRecenteringShift", body)

    def test_successful_recenter_resets_candidate_counter(self):
        body = self.cpp.split("void DIMM::updateMinuteRoi", 1)[1].split(
            "void DIMM::hideLegacyRoiScheduleUi", 1
        )[0]
        self.assertIn("runtime.roiRecenteringCandidateFrameCount = 0;", body)


if __name__ == "__main__":
    unittest.main()

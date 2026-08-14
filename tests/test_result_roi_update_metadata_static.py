import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DIMM_CPP = ROOT / "src" / "DIMM.cpp"
DIMM_H = ROOT / "src" / "DIMM.h"
RESULTS_CPP = ROOT / "src" / "DIMM.Results.cpp"


class ResultRoiUpdateMetadataStaticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpp = DIMM_CPP.read_text(encoding="utf-8")
        cls.results_cpp = RESULTS_CPP.read_text(encoding="utf-8")
        cls.header = DIMM_H.read_text(encoding="utf-8")

    def test_result_header_contains_roi_update_columns(self):
        init_body = self.results_cpp.split("void DIMM::initResultFile()", 1)[1].split(
            "void DIMM::closeResultFile()", 1
        )[0]

        expected_columns = [
            "roi_acquisition_generation",
            "roi_update_count",
            "roi_update_reason",
            "roi1_x",
            "roi1_y",
            "roi1_w",
            "roi1_h",
            "roi2_x",
            "roi2_y",
            "roi2_w",
            "roi2_h",
            "ms_since_last_roi_update",
        ]
        for column in expected_columns:
            self.assertIn(column, init_body)

    def test_result_rows_write_current_roi_update_metadata(self):
        save_body = self.results_cpp.split("void DIMM::saveResultRow", 1)[1].split(
            "void DIMM::flushPendingWrites", 1
        )[0]

        self.assertRegex(save_body, r"currentRois\s*\[\s*2\s*\]")
        self.assertIn("m_liveAcquisitionGeneration", save_body)
        self.assertIn("m_roiUpdateCount", save_body)
        self.assertIn("m_lastRoiUpdateReason", save_body)
        self.assertIn("msSinceLastRoiUpdate", save_body)
        self.assertIn("currentRois[0].x", save_body)
        self.assertIn("currentRois[1].x", save_body)

    def test_live_roi_commits_record_roi_update_event(self):
        self.assertIn("void recordLiveRoiUpdate(const RoiRect rois[2], const QString& reason)", self.header)
        self.assertIn("quint64 m_roiUpdateCount = 0;", self.header)
        self.assertIn("qint64 m_lastRoiUpdateMs = -1;", self.header)
        self.assertIn("QString m_lastRoiUpdateReason;", self.header)

        live_cpp = (ROOT / "src" / "DIMM.LiveRoi.cpp").read_text(encoding="utf-8")
        commit_body = live_cpp.split("bool DIMM::commitPairedInitialRoisIfReady()", 1)[1].split(
            "bool DIMM::startHardwarePulseStage", 1
        )[0]
        update_body = live_cpp.split("void DIMM::updateMinuteRoi", 1)[1].split(
            "void DIMM::hideLegacyRoiScheduleUi", 1
        )[0]

        self.assertIn("recordLiveRoiUpdate(actualRois,", commit_body)
        self.assertIn("recordLiveRoiUpdate(actualRois,", update_body)

    def test_measurement_reset_clears_roi_update_metadata(self):
        reset_body = self.cpp.split("void DIMM::resetMeasurementState()", 1)[1].split(
            "void DIMM::updateCaptureState", 1
        )[0]
        self.assertIn("m_roiUpdateCount = 0;", reset_body)
        self.assertIn("m_lastRoiUpdateMs = -1;", reset_body)
        self.assertIn("m_lastRoiUpdateReason.clear();", reset_body)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class LiveRoiUpdateRulesStaticTest(unittest.TestCase):
    def test_one_hz_tick_does_not_force_minute_59_roi_updates(self):
        source = read("src/DIMM.cpp")
        tick_body = source.split("void DIMM::on1hzTick", 1)[1].split(
            "void DIMM::matchRoiTimeSlot",
            1,
        )[0]

        self.assertNotIn("now.second() == 59", tick_body)
        self.assertNotIn("updateMinuteRoi()", tick_body)
        self.assertNotIn("lastRoiUpdateMinute", tick_body)

    def test_roi_update_failure_does_not_change_software_roi(self):
        source = read("src/DIMM.LiveRoi.cpp")
        update_body = source.split("void DIMM::updateMinuteRoi", 1)[1].split(
            "void DIMM::hideLegacyRoiScheduleUi",
            1,
        )[0]
        failure_block = update_body.split("setStatusMessage(reason, UiStatusLevel::Warning);", 1)[1].split(
            "} else {",
            1,
        )[0]

        self.assertNotIn("m_imageProcessor->setCurrentRoi", failure_block)
        self.assertNotIn("applyRoiSummary(roi0", failure_block)
        self.assertIn("return;", failure_block)

    def test_startup_no_longer_seeds_center_fallback_roi(self):
        source = read("src/DIMM.LiveRoi.cpp")
        update_body = source.split("void DIMM::updateMinuteRoi", 1)[1].split(
            "void DIMM::hideLegacyRoiScheduleUi",
            1,
        )[0]

        self.assertNotIn("fallbackSize0", update_body)
        self.assertNotIn("fallbackSize1", update_body)
        self.assertNotIn("frameSize.width() / 2 - kFixedRoiSize / 2", update_body)


if __name__ == "__main__":
    unittest.main()

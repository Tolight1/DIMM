from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionSchedulerStaticTest(unittest.TestCase):
    def test_scheduler_files_are_registered(self):
        cmake = read("CMakeLists.txt")
        self.assertIn("src/AutoAcquisitionScheduler.h", cmake)
        self.assertIn("src/AutoAcquisitionScheduler.cpp", cmake)

    def test_scheduler_declares_window_and_public_api(self):
        header = read("src/AutoAcquisitionScheduler.h")
        for fragment in [
            "struct AutoAcquisitionWindow",
            "bool valid = false",
            "QDateTime start",
            "QDateTime stop",
            "QString windowId",
            "QString errorMessage",
            "class AutoAcquisitionScheduler",
            "static AutoAcquisitionWindow resolveWindow",
            "static bool contains",
            "static QString formatWindowPreview",
        ]:
            self.assertIn(fragment, header)

    def test_scheduler_implements_test_override_and_cross_midnight(self):
        cpp = read("src/AutoAcquisitionScheduler.cpp")
        for fragment in [
            "config.testTimeOverrideEnabled",
            "config.testStartTime",
            "config.testStopTime",
            "if (stop <= start)",
            "stop = stop.addDays(1)",
            "start = start.addDays(-1)",
        ]:
            self.assertIn(fragment, cpp)

    def test_scheduler_implements_offline_sun_times(self):
        cpp = read("src/AutoAcquisitionScheduler.cpp")
        for fragment in [
            "calculateSunEvent",
            "const double zenithDeg = 90.833",
            "equationOfTime",
            "solarDeclination",
            "hourAngle",
            "startOffsetMinutesAfterSunset",
            "stopOffsetMinutesBeforeSunrise",
            "No local sunset",
            "No local sunrise",
        ]:
            self.assertIn(fragment, cpp)


if __name__ == "__main__":
    unittest.main()

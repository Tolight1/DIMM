from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionConfigStaticTest(unittest.TestCase):
    def test_app_config_declares_auto_acquisition(self):
        header = read("src/AppConfig.h")

        self.assertIn("#include <QTime>", header)
        self.assertIn("struct AutoAcquisitionConfig", header)
        for fragment in [
            "bool enabled = false",
            "double latitudeDeg = 0.0",
            "double longitudeDeg = 0.0",
            "int startOffsetMinutesAfterSunset = 30",
            "int stopOffsetMinutesBeforeSunrise = 30",
            "bool testTimeOverrideEnabled = false",
            "QTime testStartTime = QTime(18, 30)",
            "QTime testStopTime = QTime(6, 0)",
            "AutoAcquisitionConfig autoAcquisition",
        ]:
            self.assertIn(fragment, header)

    def test_qsettings_persists_auto_acquisition_group(self):
        cpp = read("src/AppConfigPersistence.cpp")

        for fragment in [
            "autoAcquisition/enabled",
            "autoAcquisition/latitudeDeg",
            "autoAcquisition/longitudeDeg",
            "autoAcquisition/startOffsetMinutesAfterSunset",
            "autoAcquisition/stopOffsetMinutesBeforeSunrise",
            "autoAcquisition/testTimeOverrideEnabled",
            "autoAcquisition/testStartTime",
            "autoAcquisition/testStopTime",
        ]:
            self.assertIn(f'settings.setValue(QStringLiteral("{fragment}")', cpp)
            self.assertIn(f'settings.value(QStringLiteral("{fragment}")', cpp)

        self.assertIn(".toTime()", cpp)

    def test_config_application_controller_applies_auto_acquisition(self):
        header = read("src/ConfigApplicationController.h")
        cpp = read("src/ConfigApplicationController.cpp")

        self.assertIn("std::function<void(const AutoAcquisitionConfig& config)> applyAutoAcquisition", header)
        self.assertIn("callbacks.applyAutoAcquisition", cpp)
        self.assertIn("callbacks.applyAutoAcquisition(config.autoAcquisition)", cpp)


if __name__ == "__main__":
    unittest.main()

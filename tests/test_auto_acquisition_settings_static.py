from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionSettingsStaticTest(unittest.TestCase):
    def test_settings_dialog_declares_auto_acquisition_controls(self):
        header = read("src/SettingsDialog.h")

        self.assertIn("onApplyAutoAcquisition", header)
        for name in [
            "autoAcquisitionEnableCheck",
            "autoAcquisitionLatitudeEdit",
            "autoAcquisitionLongitudeEdit",
            "autoAcquisitionStartOffsetEdit",
            "autoAcquisitionStopOffsetEdit",
            "autoAcquisitionTestOverrideCheck",
            "autoAcquisitionTestStartEdit",
            "autoAcquisitionTestStopEdit",
            "autoAcquisitionNextStartLabel",
            "autoAcquisitionNextStopLabel",
        ]:
            self.assertIn(name, header)

    def test_settings_dialog_builds_auto_acquisition_page(self):
        cpp = read("src/SettingsDialog.cpp")
        ctor = cpp.split("SettingsDialog::SettingsDialog", 1)[1].split(
            "mainLayout->addWidget(m_tabWidget)",
            1,
        )[0]

        for fragment in [
            "autoAcquisitionEnableCheck = new QCheckBox",
            "autoAcquisitionLatitudeEdit = new QLineEdit",
            "autoAcquisitionLongitudeEdit = new QLineEdit",
            "autoAcquisitionStartOffsetEdit = new QLineEdit",
            "autoAcquisitionStopOffsetEdit = new QLineEdit",
            "autoAcquisitionTestOverrideCheck = new QCheckBox",
            "autoAcquisitionTestStartEdit = new QLineEdit",
            "autoAcquisitionTestStopEdit = new QLineEdit",
            "autoAcquisitionNextStartLabel = new QLabel",
            "autoAcquisitionNextStopLabel = new QLabel",
            "addSettingsPage(autoAcquisitionTab",
        ]:
            self.assertIn(fragment, ctor)

    def test_apply_settings_validates_and_applies_auto_acquisition(self):
        cpp = read("src/SettingsDialog.cpp")
        apply_body = cpp.split("bool SettingsDialog::applySettings()", 1)[1].split(
            "void SettingsDialog::addSettingsPage",
            1,
        )[0]

        for fragment in [
            "AutoAcquisitionConfig autoAcquisitionConfig",
            "autoAcquisitionLatitudeEdit->text().toDouble",
            "autoAcquisitionLongitudeEdit->text().toDouble",
            "autoAcquisitionStartOffsetEdit->text().toInt",
            "autoAcquisitionStopOffsetEdit->text().toInt",
            "QTime::fromString(autoAcquisitionTestStartEdit->text().trimmed(), QStringLiteral(\"HH:mm\"))",
            "QTime::fromString(autoAcquisitionTestStopEdit->text().trimmed(), QStringLiteral(\"HH:mm\"))",
            "纬度必须在 -90 到 90 之间",
            "经度必须在 -180 到 180 之间",
            "日落后启动偏移必须在 0 到 240 分钟之间",
            "日出前停止偏移必须在 0 到 240 分钟之间",
            "测试开始时间格式必须为 HH:mm",
            "测试停止时间格式必须为 HH:mm",
            "appConfig.autoAcquisition = autoAcquisitionConfig",
        ]:
            self.assertIn(fragment, apply_body)


if __name__ == "__main__":
    unittest.main()

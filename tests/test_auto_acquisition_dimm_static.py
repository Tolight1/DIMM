from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionDimmStaticTest(unittest.TestCase):
    def test_dimm_declares_auto_acquisition_config_and_methods(self):
        header = read("src/DIMM.h")

        for fragment in [
            "AutoAcquisitionConfig m_autoAcquisitionConfig",
            "void setupAutoAcquisitionSettingsCallbacks()",
            "void evaluateAutoAcquisitionSchedule()",
            "void setAutoAcquisitionStatus",
            "void noteManualAutoAcquisitionStopIfNeeded()",
            "bool m_autoAcquisitionCommandInProgress = false",
            "bool m_autoAcquisitionStartedCurrentRun = false",
            "QString m_autoAcquisitionActiveWindowId",
            "QString m_autoAcquisitionSuppressedWindowId",
            "qint64 m_lastAutoAcquisitionAttemptMs = -1",
        ]:
            self.assertIn(fragment, header)

    def test_dimm_config_saves_loads_and_applies_auto_acquisition(self):
        config = read("src/DIMM.Config.cpp")

        for fragment in [
            '#include "AutoAcquisitionScheduler.h"',
            "setupAutoAcquisitionSettingsCallbacks();",
            "void DIMM::setupAutoAcquisitionSettingsCallbacks()",
            "m_settingsDialog->onApplyAutoAcquisition",
            "m_autoAcquisitionConfig = config",
            "config.autoAcquisition = m_autoAcquisitionConfig",
            "m_autoAcquisitionConfig = config.autoAcquisition",
            "autoAcquisitionEnableCheck->setChecked",
            "autoAcquisitionLatitudeEdit->setText",
            "autoAcquisitionLongitudeEdit->setText",
            "autoAcquisitionNextStartLabel->setText",
            "AutoAcquisitionScheduler::resolveWindow",
        ]:
            self.assertIn(fragment, config)

    def test_dimm_evaluates_auto_acquisition_from_1hz_tick(self):
        cpp = read("src/DIMM.cpp")
        tick_body = cpp.split("void DIMM::on1hzTick()", 1)[1].split(
            "void DIMM::matchRoiTimeSlot()",
            1,
        )[0]

        self.assertIn("evaluateAutoAcquisitionSchedule();", tick_body)
        self.assertIn("void DIMM::evaluateAutoAcquisitionSchedule()", cpp)
        scheduler_body = cpp.split("void DIMM::evaluateAutoAcquisitionSchedule()", 1)[1].split(
            "void DIMM::setAutoAcquisitionStatus",
            1,
        )[0]

        for fragment in [
            "AutoAcquisitionScheduler::resolveWindow",
            "AutoAcquisitionScheduler::contains",
            "m_autoAcquisitionSuppressedWindowId == window.windowId",
            "m_lastAutoAcquisitionAttemptMs",
            "canStartLiveCapture(&reason)",
            "m_autoAcquisitionCommandInProgress = true",
            "onStartCapture();",
            "onStopCapture();",
            "m_autoAcquisitionStartedCurrentRun = true",
            "m_autoAcquisitionStartedCurrentRun = false",
        ]:
            self.assertIn(fragment, scheduler_body)

    def test_manual_stop_suppresses_same_auto_window(self):
        cpp = read("src/DIMM.cpp")

        self.assertIn("void DIMM::noteManualAutoAcquisitionStopIfNeeded()", cpp)
        manual_body = cpp.split("void DIMM::noteManualAutoAcquisitionStopIfNeeded()", 1)[1].split(
            "void DIMM::onStartCapture()",
            1,
        )[0]
        for fragment in [
            "m_autoAcquisitionCommandInProgress",
            "m_autoAcquisitionStartedCurrentRun",
            "m_autoAcquisitionSuppressedWindowId = m_autoAcquisitionActiveWindowId",
            "m_autoAcquisitionStartedCurrentRun = false",
        ]:
            self.assertIn(fragment, manual_body)

        start_live_branch = cpp.split("if (m_captureState == CaptureState::Live)", 1)[1].split(
            "if (m_captureState == CaptureState::Simulation)",
            1,
        )[0]
        stop_body = cpp.split("void DIMM::onStopCapture()", 1)[1].split(
            "void DIMM::onShowMainPage()",
            1,
        )[0]
        self.assertIn("noteManualAutoAcquisitionStopIfNeeded();", start_live_branch)
        self.assertIn("noteManualAutoAcquisitionStopIfNeeded();", stop_body)

    def test_auto_acquisition_status_is_throttled(self):
        cpp = read("src/DIMM.cpp")
        status_body = cpp.split("void DIMM::setAutoAcquisitionStatus", 1)[1].split(
            "void DIMM::noteManualAutoAcquisitionStopIfNeeded()",
            1,
        )[0]
        self.assertIn("m_lastAutoAcquisitionStatusKey == throttleKey", status_body)
        self.assertIn("m_lastAutoAcquisitionStatusMs", status_body)
        self.assertIn("setStatusMessage(text, level)", status_body)


if __name__ == "__main__":
    unittest.main()

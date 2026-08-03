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


if __name__ == "__main__":
    unittest.main()

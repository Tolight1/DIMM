from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionRuntimeConflictStaticTest(unittest.TestCase):
    def test_manual_stop_inside_auto_window_suppresses_current_window(self):
        cpp = read("src/DIMM.cpp")
        body = cpp.split("void DIMM::noteManualAutoAcquisitionStopIfNeeded()", 1)[1].split(
            "bool DIMM::shouldRetryFailedLiveStartup() const",
            1,
        )[0]

        self.assertIn("m_autoAcquisitionCommandInProgress", body)
        self.assertIn("AutoAcquisitionScheduler::resolveWindow", body)
        self.assertIn("AutoAcquisitionScheduler::contains", body)
        self.assertIn("m_autoAcquisitionSuppressedWindowId = suppressedWindowId", body)
        self.assertIn("m_autoAcquisitionStartedCurrentRun = false", body)
        self.assertIn("m_autoAcquisitionActiveWindowId.clear()", body)

    def test_hardware_start_reuses_only_full_frame_localization_pulse(self):
        header = read("src/DIMM.h")
        self.assertIn("bool isFullFrameLocalizationPulseRunning() const", header)

        cpp = read("src/DIMM.cpp")
        start_body = cpp.split("void DIMM::onStartCapture()", 1)[1].split(
            "void DIMM::onStopCapture()",
            1,
        )[0]
        self.assertIn("isFullFrameLocalizationPulseRunning()", start_body)
        self.assertNotIn("m_pulseGenerator->isRunning();\n            if (reuseRunningPulse)", start_body)

        live_roi = read("src/DIMM.LiveRoi.cpp")
        helper_body = live_roi.split("bool DIMM::isFullFrameLocalizationPulseRunning() const", 1)[1].split(
            "bool DIMM::commitPairedInitialRoisIfReady()",
            1,
        )[0]
        self.assertIn("kFullFrameLocalizationPulseHz", helper_body)
        self.assertIn("pulseConfigsMatch", helper_body)
        self.assertIn("m_pulseGenerator->config()", helper_body)

    def test_auto_exposure_adjustment_defers_roi_relocalization(self):
        header = read("src/DIMM.h")
        self.assertIn("bool isAutoExposureRoiRelocalizationGraceActive(qint64 nowMs) const", header)
        self.assertIn("kAutoExposureRoiRelocalizationGraceMs", header)

        auto_exposure = read("src/DIMM.AutoExposure.cpp")
        self.assertIn("bool DIMM::isAutoExposureRoiRelocalizationGraceActive(qint64 nowMs) const", auto_exposure)
        self.assertIn("m_lastAutoExposureAdjustMs", auto_exposure)
        self.assertIn("kAutoExposureRoiRelocalizationGraceMs", auto_exposure)

        live_roi = read("src/DIMM.LiveRoi.cpp")
        loss_body = live_roi.split("void DIMM::handleLiveRoiCentroidLoss", 1)[1].split(
            "bool DIMM::validateAndCacheLiveRoiCapabilities",
            1,
        )[0]
        self.assertIn("isAutoExposureRoiRelocalizationGraceActive(nowMs)", loss_body)
        self.assertLess(
            loss_body.find("isAutoExposureRoiRelocalizationGraceActive(nowMs)"),
            loss_body.find("requestLiveFullFrameRelocalization"),
        )

    def test_pulse_board_timeout_uses_common_detector_and_throttled_status(self):
        header = read("src/DIMM.h")
        self.assertIn("bool isPulseBoardResponseTimeout(const QString& reason) const", header)
        self.assertIn("void setPulseBoardResponseTimeoutStatus", header)
        self.assertIn("m_lastPulseBoardTimeoutStatusMs", header)

        cpp = read("src/DIMM.cpp")
        self.assertIn("bool DIMM::isPulseBoardResponseTimeout(const QString& reason) const", cpp)
        self.assertIn("void DIMM::setPulseBoardResponseTimeoutStatus", cpp)
        self.assertIn("kPulseBoardTimeoutStatusThrottleMs", cpp + header)

        start_body = cpp.split("void DIMM::onStartCapture()", 1)[1].split(
            "void DIMM::onStopCapture()",
            1,
        )[0]
        self.assertIn("isPulseBoardResponseTimeout(reason)", start_body)
        self.assertIn("setPulseBoardResponseTimeoutStatus", start_body)

        comm = read("src/DIMM.CommCamera.cpp")
        live_body = comm.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "void DIMM::scheduleHardwareTriggerStartupCheck",
            1,
        )[0]
        self.assertIn("setPulseBoardResponseTimeoutStatus", live_body)

        live_roi = read("src/DIMM.LiveRoi.cpp")
        commit_body = live_roi.split("bool DIMM::commitPairedInitialRoisIfReady()", 1)[1].split(
            "bool DIMM::startHardwarePulseStage",
            1,
        )[0]
        self.assertIn("isPulseBoardResponseTimeout(reason)", commit_body)
        self.assertIn("setPulseBoardResponseTimeoutStatus", commit_body)


if __name__ == "__main__":
    unittest.main()

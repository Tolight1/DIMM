from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class MeasurementUiStaticTest(unittest.TestCase):
    def test_measurement_refresh_updates_status_frame_counter(self):
        ui_cpp = read("src/DIMM.Ui.cpp")
        refresh_body = ui_cpp.split("void DIMM::refreshMeasurementUi()", 1)[1].split(
            "void DIMM::refreshPanelUi()", 1
        )[0]

        self.assertIn("m_lblStatusFrames", refresh_body)
        self.assertIn("帧数: %1 帧", refresh_body)

    def test_statistics_labels_use_compact_runtime_text(self):
        ui_cpp = read("src/DIMM.Ui.cpp")

        for label_name in ["lblStatFrames", "lblStatValid", "lblStatLatency", "lblStatWindow"]:
            self.assertIn(f"ui->{label_name}", ui_cpp)
        self.assertIn("label->setWordWrap(true)", ui_cpp)

        # Each compact statistic keeps a human-readable caption and its units.
        for fragment in ["原始/入处理", "质心/配对", "未配对丢帧延迟", "同步抖动", "μs"]:
            self.assertIn(fragment, ui_cpp)

        # The compact labels are refreshed from the live runtime values.
        for fragment in [
            "runtime.frameCount",
            "runtime.processedFrameCount",
            "runtime.validCentroidCount",
            "runtime.pairedSampleCount",
            "runtime.droppedUnpairedSampleCount",
            "runtime.averageProcessingLatencyMs",
            "runtime.averageSyncJitterUs",
        ]:
            self.assertIn(fragment, ui_cpp)


    def test_continuous_capture_frame_rate_is_configurable(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        config_cpp = read("src/DIMM.Config.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        settings_h = read("src/SettingsDialog.h")
        settings_cpp = read("src/SettingsDialog.cpp")
        settings_ctor = settings_cpp.split("SettingsDialog::SettingsDialog", 1)[1].split(
            "auto* processingGroup",
            1,
        )[0]
        apply_body = settings_cpp.split("bool SettingsDialog::applySettings()", 1)[1].split(
            "void SettingsDialog::setPulseGeneratorState",
            1,
        )[0]
        configure_body = live_cpp.split("bool DIMM::configureLiveCameras", 1)[1].split(
            "bool DIMM::startDualCameraLocalization",
            1,
        )[0]

        self.assertIn("continuousFrameRateEdit", settings_h)
        self.assertIn("m_configContinuousFrameRateHz = 200.0", dimm_h)
        self.assertIn("连续采集帧率 (fps):", settings_ctor)
        self.assertIn("QStringLiteral(\"200\")", settings_ctor)
        self.assertIn("continuousFrameRateEdit->text().toDouble", apply_body)
        self.assertIn("连续采集帧率必须在 0.1 到 1000 fps 之间", apply_body)
        self.assertIn("m_configContinuousFrameRateHz", dimm_cpp)
        self.assertIn("applyContinuousCameraFrameRate(reason)", configure_body)
        self.assertIn("applyContinuousCameraFrameRate(&reason)", config_cpp)

    def test_continuous_frame_rate_is_reapplied_after_live_geometry_changes(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        roi_body = live_cpp.split("bool DIMM::applyLiveHardwareRois", 1)[1].split(
            "bool DIMM::applyLiveFullFrameForRelocalization",
            1,
        )[0]
        full_frame_body = live_cpp.split("bool DIMM::applyLiveFullFrameForRelocalization", 1)[1].split(
            "bool DIMM::maybeSeedRoiFromFrame",
            1,
        )[0]

        self.assertIn("bool applyContinuousCameraFrameRate(QString* reason = nullptr)", dimm_h)
        self.assertIn("applyContinuousCameraFrameRate(reason)", roi_body)
        self.assertIn("kFullFrameLocalizationPulseHz", full_frame_body)
        self.assertIn("m_cameraManager->setFrameRate(cameraIndex, kFullFrameLocalizationPulseHz)", full_frame_body)

    def test_continuous_frame_rate_is_applied_while_live_stream_is_paused(self):
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        helper_body = live_cpp.split("bool DIMM::applyContinuousCameraFrameRate", 1)[1].split(
            "bool DIMM::startDualCameraLocalization",
            1,
        )[0]

        self.assertIn("const bool restartLiveContinuousCapture", helper_body)
        self.assertIn("m_captureState == CaptureState::Live", helper_body)
        self.assertIn("m_cameraManager->stopAll()", helper_body)
        self.assertIn("m_cameraManager->startAll()", helper_body)
        self.assertIn("restartLiveCapture()", helper_body)
        self.assertLess(helper_body.find("m_cameraManager->stopAll()"),
                        helper_body.find("m_cameraManager->setFrameRate"))
        self.assertLess(helper_body.find("m_cameraManager->setFrameRate"),
                        helper_body.rfind("restartLiveCapture()"))

    def test_result_file_records_continuous_frame_rate_readback(self):
        dimm_h = read("src/DIMM.h")
        results_cpp = read("src/DIMM.Results.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        result_body = results_cpp.split("void DIMM::initResultFile()", 1)[1].split(
            "void DIMM::closeResultFile()",
            1,
        )[0]
        save_body = results_cpp.split("void DIMM::saveResultRow", 1)[1].split(
            "void DIMM::flushPendingWrites",
            1,
        )[0]
        helper_body = live_cpp.split("bool DIMM::applyContinuousCameraFrameRate", 1)[1].split(
            "bool DIMM::startDualCameraLocalization",
            1,
        )[0]

        self.assertIn("m_lastContinuousFrameRateReadback[2]", dimm_h)
        self.assertIn("continuous_frame_rate_target_hz", result_body)
        self.assertIn("camera1_frame_rate_readback_hz", result_body)
        self.assertIn("camera2_frame_rate_readback_hz", result_body)
        self.assertIn("m_lastContinuousFrameRateReadback[cameraIndex] = actualFrameRate", helper_body)
        self.assertIn("QString::number(m_configContinuousFrameRateHz", save_body)
        self.assertIn("QString::number(m_lastContinuousFrameRateReadback[0]", save_body)
        self.assertIn("QString::number(m_lastContinuousFrameRateReadback[1]", save_body)

    def test_live_statistics_ignore_stale_or_duplicate_camera_packets(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        comm_cpp = read("src/DIMM.CommCamera.cpp")
        live_body = comm_cpp.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "void DIMM::scheduleHardwareTriggerStartupCheck",
            1,
        )[0]
        rate_body = live_cpp.split("bool DIMM::applyContinuousCameraFrameRate", 1)[1].split(
            "bool DIMM::startDualCameraLocalization",
            1,
        )[0]
        reset_body = dimm_cpp.split("void DIMM::resetMeasurementState", 1)[1].split(
            "void DIMM::updateCaptureState",
            1,
        )[0]

        self.assertIn("m_liveFrameAcceptAfterMs", dimm_h)
        self.assertIn("m_lastAcceptedLiveFrameId[2]", dimm_h)
        self.assertIn("packet.receivedMs > 0 && packet.receivedMs < m_liveFrameAcceptAfterMs", live_body)
        self.assertIn("packet.frameId > 0 && packet.frameId <= m_lastAcceptedLiveFrameId[cameraIndex]", live_body)
        self.assertLess(live_body.find("packet.frameId > 0 && packet.frameId <= m_lastAcceptedLiveFrameId[cameraIndex]"),
                        live_body.find("++runtime.frameCount"))
        self.assertIn("m_lastAcceptedLiveFrameId[cameraIndex] = packet.frameId", live_body)
        self.assertIn("resetLiveFrameAcceptanceGates()", dimm_cpp)
        self.assertIn("resetLiveFrameAcceptanceGates();", rate_body)
        self.assertIn("resetLiveFrameAcceptanceGates();", reset_body)

    def test_continuous_mode_throttles_frames_before_statistics_and_processing(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        comm_cpp = read("src/DIMM.CommCamera.cpp")
        live_body = comm_cpp.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "void DIMM::scheduleHardwareTriggerStartupCheck",
            1,
        )[0]
        reset_body = dimm_cpp.split("void DIMM::resetMeasurementState", 1)[1].split(
            "void DIMM::updateCaptureState",
            1,
        )[0]
        rate_body = live_cpp.split("bool DIMM::applyContinuousCameraFrameRate", 1)[1].split(
            "bool DIMM::startDualCameraLocalization",
            1,
        )[0]

        self.assertIn("m_lastAcceptedContinuousFrameMs[2]", dimm_h)
        self.assertIn("m_configTriggerMode == 0", live_body)
        self.assertIn("continuousFrameIntervalMs", live_body)
        self.assertIn("return;", live_body)
        self.assertLess(live_body.find("continuousFrameIntervalMs"), live_body.find("++runtime.frameCount"))
        self.assertLess(live_body.find("m_lastAcceptedContinuousFrameMs[cameraIndex] = frameReceivedMs"),
                        live_body.find("++runtime.frameCount"))
        reset_gate_body = live_cpp.split("void DIMM::resetLiveFrameAcceptanceGates", 1)[1].split(
            "bool DIMM::startDualCameraLocalization",
            1,
        )[0]
        self.assertIn("m_lastAcceptedContinuousFrameMs[0] = -1", reset_gate_body)
        self.assertIn("resetLiveFrameAcceptanceGates();", reset_body)
        self.assertIn("resetLiveFrameAcceptanceGates();", rate_body)


if __name__ == "__main__":
    unittest.main()

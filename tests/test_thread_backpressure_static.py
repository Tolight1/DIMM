from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ThreadBackpressureStaticTest(unittest.TestCase):
    def test_dimm_keeps_latest_frame_preview_and_uses_full_capture_for_hardware_live(self):
        source = read("src/DIMM.cpp")
        comm_source = read("src/DIMM.CommCamera.cpp")
        camera_connection_block = source.split("connect(m_cameraManager", 1)[1].split(
            "connect(m_imageProcessor",
            1,
        )[0]

        self.assertIn("&CameraManager::frameReady", camera_connection_block)
        self.assertIn("&DIMM::onFrameReady", camera_connection_block)
        self.assertIn("&CameraManager::frameCaptured", camera_connection_block)
        self.assertIn("&DIMM::onCapturedFramePacket", camera_connection_block)

        frame_ready_body = comm_source.split("void DIMM::onFrameReady", 1)[1].split(
            "void DIMM::onCapturedFramePacket",
            1,
        )[0]
        captured_body = comm_source.split("void DIMM::onCapturedFramePacket", 1)[1].split(
            "void DIMM::handleLiveFramePacket",
            1,
        )[0]

        self.assertIn("m_configTriggerMode != 0", frame_ready_body)
        self.assertIn("return;", frame_ready_body)
        self.assertIn("m_configTriggerMode == 0", captured_body)
        self.assertIn("return;", captured_body)

    def test_hardware_capture_diagnostics_cover_each_sync_boundary(self):
        app_config = read("src/AppConfig.h")
        app_persistence = read("src/AppConfigPersistence.cpp")
        settings_h = read("src/SettingsDialog.h")
        settings_cpp = read("src/SettingsDialog.cpp")
        dimm_header = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        dimm_source = read("src/DIMM.CommCamera.cpp")
        results_source = read("src/DIMM.Results.cpp")
        processor_header = read("src/ImageProcessor.h")
        processor_source = read("src/ImageProcessor.cpp")

        storage_config = app_config.split("struct StorageConfig", 1)[1].split("};", 1)[0]
        self.assertIn("bool syncDiagnosticLoggingEnabled = false", storage_config)
        self.assertIn("storage/syncDiagnosticLoggingEnabled", app_persistence)

        self.assertIn("QCheckBox* syncDiagnosticLogCheck", settings_h)
        self.assertIn("bool syncDiagnosticLoggingEnabled", settings_h)
        self.assertIn("syncDiagnosticLogCheck = new QCheckBox", settings_cpp)
        self.assertIn("syncDiagnosticLogCheck->isChecked()", settings_cpp)

        self.assertIn("ResultWriter m_syncDiagnosticWriter", dimm_header)
        self.assertIn("QString m_syncDiagnosticFilePath", dimm_header)
        self.assertIn("bool m_syncDiagnosticLoggingEnabled = false", dimm_header)
        self.assertIn("m_diagnosticLastLiveFrameId", dimm_header)
        self.assertIn("m_diagnosticLivePacketCount", dimm_header)
        self.assertIn("m_diagnosticLivePacketGapCount", dimm_header)
        self.assertIn("initSyncDiagnosticFile", dimm_header)
        self.assertIn("recordSyncDiagnosticEvent", dimm_header)
        self.assertIn("resetSyncDiagnostics", dimm_header)

        self.assertIn("DIMM_%2_sync_diagnostics_%3.txt", results_source)
        self.assertIn("event,camera,frame_id,camera_timestamp", results_source)
        self.assertIn("m_syncDiagnosticWriter.enqueue", results_source)
        self.assertIn("m_syncDiagnosticWriter.close()", results_source)
        self.assertIn("m_syncDiagnosticWriter.flush()", results_source)

        self.assertIn("recordSyncDiagnosticEvent(QStringLiteral(\"capture\")", dimm_source)
        self.assertIn("recordSyncDiagnosticEvent(QStringLiteral(\"submit\")", dimm_source)
        self.assertIn("resetSyncDiagnostics", dimm_cpp)

        self.assertIn("m_diagnosticUnpairedDropLogCount", processor_header)
        self.assertIn("unpairedSampleDropped", processor_header)
        self.assertIn("emit unpairedSampleDropped", processor_source)

    def test_image_processor_submits_frames_in_arrival_order_without_coalescing(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")
        process_body = source.split("void ImageProcessor::processFrame(int cameraIndex", 1)[1]

        # The per-camera latest-frame coalescing slots are gone.
        self.assertNotIn("struct PendingFrameSlot", header)
        self.assertNotIn("m_pendingFrames", header)
        self.assertNotIn("processLatestFrameLoop", header)
        self.assertNotIn("clearPendingFrames", header)

        # Each accepted frame is deep-cloned before it is submitted.
        self.assertIn("cv::Mat frameCopy = frame.clone();", process_body)
        self.assertLess(process_body.find("frame.clone()"),
                        process_body.find("QMetaObject::invokeMethod"))

        # The queued submission carries the full per-frame payload.
        self.assertIn("QMetaObject::invokeMethod", process_body)
        self.assertIn('"processFrame"', process_body)
        self.assertIn("Qt::QueuedConnection", process_body)
        self.assertIn("Q_ARG(int, cameraIndex)", process_body)
        self.assertIn("Q_ARG(cv::Mat, frameCopy)", process_body)
        self.assertIn("Q_ARG(quint64, frameId)", process_body)
        self.assertIn("Q_ARG(quint64, cameraTimestamp)", process_body)
        self.assertIn("Q_ARG(quint64, acquisitionGeneration)", process_body)

    def test_each_submitted_frame_is_forwarded_to_worker_by_queued_connection(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")
        process_body = source.split("void ImageProcessor::processFrame(int cameraIndex", 1)[1]

        # No per-camera latest-frame replacement loop remains.
        self.assertNotIn("processLatestFrameLoop", source)
        self.assertNotIn("PendingFrameSlot", source)

        # The outer function submits through a single queued invoke per accepted call
        # and never calls the worker directly, preserving arrival order.
        self.assertIn("QMetaObject::invokeMethod", process_body)
        self.assertNotIn("m_worker->processFrame(", process_body)
        self.assertIn("Qt::QueuedConnection", process_body)

        # The frame is deep-cloned before asynchronous submission.
        self.assertIn("cv::Mat frameCopy = frame.clone();", process_body)
        self.assertLess(process_body.find("frame.clone()"),
                        process_body.find("QMetaObject::invokeMethod"))

        # The worker slot keeps the full five-argument payload signature.
        self.assertIn("void processFrame(int cameraIndex", header)
        self.assertIn("cv::Mat frame", header)
        self.assertIn("quint64 frameId = 0", header)
        self.assertIn("quint64 cameraTimestamp = 0", header)
        self.assertIn("quint64 acquisitionGeneration = 0", header)


if __name__ == "__main__":
    unittest.main()

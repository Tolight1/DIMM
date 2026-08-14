from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class LiveFullFrameLocatorStaticTest(unittest.TestCase):
    def test_live_roi_seed_uses_candidate_selection_path_without_centroid_fallback(self):
        source = read("src/DIMM.LiveRoi.cpp")
        body = source.split("bool DIMM::maybeSeedRoiFromFrame", 1)[1].split(
            "void DIMM::updateFullFrameRoiOverlay", 1
        )[0]

        self.assertIn("selectLiveRelocalizationCentroid(cameraIndex, grayscale, &centroid, &peakValue)", body)
        self.assertIn("detectInitialStarCandidates(grayscale, &peakValue)", body)
        self.assertIn("PolarisDetectionPipeline::selectInitialStarCandidate", body)
        self.assertNotIn("detectInitialStarCentroid(grayscale, &centroid, &peakValue)", body)
        self.assertNotIn("detectInitialStarCentroidFast(grayscale, &centroid, &peakValue)", body)

    def test_roi_geometry_update_restarts_stream_for_payload_size_change(self):
        source = read("src/CameraManager.cpp")
        body = source.split("bool CameraManager::pauseForRoiUpdate", 1)[1].split(
            "bool CameraManager::resumeAfterRoiUpdate", 1
        )[0]

        self.assertNotIn("if (roiWritableNow)", body)
        self.assertIn("camera.remoteFeatureControl->GetCommandFeature(\"AcquisitionStop\")->Execute()", body)
        self.assertIn("camera.stream->StopGrab()", body)
        self.assertIn("pauseState->streamStopped = true", body)

    def test_live_relocalization_ignores_stale_roi_sized_frames(self):
        source = read("src/DIMM.LiveRoi.cpp")
        body = source.split("bool DIMM::maybeSeedRoiFromFrame", 1)[1].split(
            "void DIMM::updateFullFrameRoiOverlay", 1
        )[0]
        live_guard = body.split("if (m_captureState == CaptureState::Live)", 1)[1].split(
            "auto& runtime = activeRuntime();", 1
        )[0]

        self.assertIn("frameLooksLikeHardwareRoi", live_guard)
        self.assertIn("!liveLocatePhase || frameLooksLikeHardwareRoi", live_guard)
        self.assertNotIn("m_liveHardwareRoiActive && frameLooksLikeHardwareRoi", live_guard)

    def test_relocalization_defers_full_frame_preview_until_roi_tracking_resumes(self):
        live_source = read("src/DIMM.LiveRoi.cpp")
        comm_source = read("src/DIMM.CommCamera.cpp")
        request_body = live_source.split("void DIMM::requestLiveFullFrameRelocalization", 1)[1].split(
            "void DIMM::handleLiveRoiCentroidLoss", 1
        )[0]
        preview_block = comm_source.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "const bool roiConfirmed",
            1,
        )[0]
        commit_body = live_source.split("bool DIMM::commitPairedInitialRoisIfReady", 1)[1].split(
            "bool DIMM::startHardwarePulseStage", 1
        )[0]

        self.assertIn("runtime.liveRelocalizationPreviewFrame[cameraIndex].release()", request_body)
        self.assertIn("m_liveStartupPhase == LiveStartupPhase::Tracking", preview_block)
        self.assertIn("showDeferredLiveRelocalizationPreview()", commit_body)

    def test_live_relocalization_caches_success_frame_without_candidate_overlay(self):
        header = read("src/DIMM.h")
        source = read("src/DIMM.LiveRoi.cpp")
        body = source.split("bool DIMM::maybeSeedRoiFromFrame", 1)[1].split(
            "void DIMM::updateFullFrameRoiOverlay", 1
        )[0]
        relocalization_body = source.split("bool DIMM::selectLiveRelocalizationCentroid", 1)[1].split(
            "bool DIMM::maybeSeedRoiFromFrame", 1
        )[0]

        self.assertIn("cv::Mat liveRelocalizationPreviewFrame[2]", header)
        self.assertIn("runtime.liveRelocalizationPreviewFrame[cameraIndex] = frame.clone()", body)
        self.assertNotIn("setStarCandidateOverlays", relocalization_body)

    def test_relocalization_clears_stale_roi_canvases(self):
        source = read("src/DIMM.LiveRoi.cpp")
        body = source.split("void DIMM::requestLiveFullFrameRelocalization", 1)[1].split(
            "void DIMM::handleLiveRoiCentroidLoss", 1
        )[0]

        self.assertIn("m_cam1RoiCanvas->clear()", body)
        self.assertIn("m_cam2RoiCanvas->clear()", body)
        self.assertIn("ui->lblCam1ROICoord->setText(QStringLiteral(\"(0.0, 0.0)\"))", body)
        self.assertIn("ui->lblCam2ROICoord->setText(QStringLiteral(\"(0.0, 0.0)\"))", body)

    def test_live_full_frame_preview_never_uses_roi_sized_frames(self):
        source = read("src/DIMM.CommCamera.cpp")
        body = source.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "void DIMM::scheduleHardwareTriggerStartupCheck", 1
        )[0]
        preview_block = body.split("const bool shouldRefreshPreview", 1)[1].split(
            "const bool roiConfirmed",
            1,
        )[0]

        self.assertIn("const bool canUpdateFullFramePreview", preview_block)
        self.assertIn("!frameLooksLikeHardwareRoi &&", preview_block)
        self.assertIn("m_liveStartupPhase == LiveStartupPhase::Tracking", preview_block)
        self.assertIn("canUpdateFullFramePreview && shouldRefreshPreview", preview_block)
        self.assertNotIn("if (!(m_liveHardwareRoiActive && frameLooksLikeHardwareRoi))", preview_block)

    def test_roi_tracking_frames_update_overlay_only_when_full_frame_image_refreshes(self):
        source = read("src/DIMM.CommCamera.cpp")
        body = source.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "void DIMM::scheduleHardwareTriggerStartupCheck", 1
        )[0]
        preview_block = body.split("if (cameraIndex >= 0 && cameraIndex < 2)", 2)[2].split(
            "const bool roiConfirmed",
            1,
        )[0]

        self.assertIn("const bool canUpdateFullFramePreview", preview_block)
        self.assertIn("if (targetCanvas && canUpdateFullFramePreview && shouldRefreshPreview)", preview_block)
        self.assertIn("targetCanvas->setImage(frame)", preview_block)
        self.assertIn("runtime.lastLivePreviewUpdateMs[cameraIndex] = nowMs", preview_block)
        self.assertIn("updateFullFrameRoiOverlay(cameraIndex)", preview_block)
        self.assertLess(preview_block.find("if (targetCanvas && canUpdateFullFramePreview && shouldRefreshPreview)"),
                        preview_block.find("updateFullFrameRoiOverlay(cameraIndex)"))
        self.assertEqual(preview_block.rfind("updateFullFrameRoiOverlay(cameraIndex)"),
                         preview_block.find("updateFullFrameRoiOverlay(cameraIndex)"))

    def test_live_locator_ignores_stale_roi_processor_signals(self):
        source = read("src/DIMM.cpp")
        centroid_body = source.split("&ImageProcessor::centroidReady", 1)[1].split(
            "connect(m_imageProcessor,\n            &ImageProcessor::differentialSampleReady",
            1,
        )[0]
        processed_body = source.split("&ImageProcessor::frameProcessed", 1)[1].split(
            "connect(m_imageProcessor, &ImageProcessor::syncSampleReady",
            1,
        )[0]

        self.assertIn("m_captureState == CaptureState::Live &&", centroid_body)
        self.assertIn("m_liveStartupPhase != LiveStartupPhase::Tracking", centroid_body)
        self.assertLess(centroid_body.find("m_liveStartupPhase != LiveStartupPhase::Tracking"),
                        centroid_body.find("runtime.hasValidCentroid[camIdx] = true"))
        self.assertIn("m_captureState == CaptureState::Live &&", processed_body)
        self.assertIn("m_liveStartupPhase != LiveStartupPhase::Tracking", processed_body)
        self.assertLess(processed_body.find("m_liveStartupPhase != LiveStartupPhase::Tracking"),
                        processed_body.find("++runtime.processedFrameCount"))

    def test_live_locator_does_not_block_on_stale_valid_centroid_flag(self):
        source = read("src/DIMM.LiveRoi.cpp")
        body = source.split("bool DIMM::maybeSeedRoiFromFrame", 1)[1].split(
            "void DIMM::updateFullFrameRoiOverlay", 1
        )[0]

        self.assertIn("const bool liveLocatePhase", body)
        self.assertIn("if (!liveLocatePhase && runtime.hasValidCentroid[cameraIndex])", body)
        self.assertNotIn("if (runtime.hasValidCentroid[cameraIndex])", body)

    def test_live_relocalization_uses_dedicated_automatic_locator(self):
        header = read("src/DIMM.h")
        source = read("src/DIMM.LiveRoi.cpp")
        self.assertIn("bool DIMM::selectLiveRelocalizationCentroid", source)
        body = source.split("bool DIMM::maybeSeedRoiFromFrame", 1)[1].split(
            "void DIMM::updateFullFrameRoiOverlay", 1
        )[0]
        relocalization_body = source.split("bool DIMM::selectLiveRelocalizationCentroid", 1)[1].split(
            "bool DIMM::maybeSeedRoiFromFrame", 1
        )[0]

        self.assertIn("bool selectLiveRelocalizationCentroid", header)
        self.assertIn("selectLiveRelocalizationCentroid(cameraIndex, grayscale, &centroid, &peakValue)", body)
        self.assertIn("if (liveLocatePhase)", body)
        self.assertLess(body.find("selectLiveRelocalizationCentroid"),
                        body.find("runtime.pendingInitialCandidateSelectionRequired"))
        self.assertIn("detectInitialStarCandidates(fullFrame, peakValue)", relocalization_body)
        self.assertIn("runtime.hasLastTargetPosition[cameraIndex]", relocalization_body)
        self.assertIn("PolarisDetectionPipeline::selectInitialStarCandidate", relocalization_body)
        self.assertNotIn("pendingInitialCandidateSelectionRequired", relocalization_body)
        self.assertIn("chooseAutomaticInitialStarCandidate", relocalization_body)

    def test_failed_live_relocalization_commit_does_not_leave_pending_roi_stuck(self):
        source = read("src/DIMM.LiveRoi.cpp")
        commit_body = source.split("bool DIMM::commitPairedInitialRoisIfReady", 1)[1].split(
            "bool DIMM::startHardwarePulseStage", 1
        )[0]

        self.assertIn("clearPendingLiveRelocalizationRois()", source)
        self.assertIn("clearPendingLiveRelocalizationRois();", commit_body)
        self.assertLess(commit_body.find("!applyLiveHardwareRois"),
                        commit_body.find("clearPendingLiveRelocalizationRois();"))
        self.assertLess(commit_body.find("!switchToRoiTrackingPulse"),
                        commit_body.rfind("clearPendingLiveRelocalizationRois();"))

    def test_live_relocalization_has_whole_stage_timeout_not_only_detector_attempts(self):
        header = read("src/DIMM.h")
        source = read("src/DIMM.CommCamera.cpp")
        live_source = read("src/DIMM.LiveRoi.cpp")
        request_body = live_source.split("void DIMM::requestLiveFullFrameRelocalization", 1)[1].split(
            "void DIMM::handleLiveRoiCentroidLoss", 1
        )[0]
        frame_body = source.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "void DIMM::scheduleHardwareTriggerStartupCheck", 1
        )[0]
        self.assertIn("void DIMM::handleLiveRelocalizationWatchdog", live_source)
        watchdog_body = live_source.split("void DIMM::handleLiveRelocalizationWatchdog", 1)[1].split(
            "void DIMM::updateFullFrameRoiOverlay", 1
        )[0]

        self.assertIn("qint64 liveRelocalizationStartedMs = -1", header)
        self.assertIn("void handleLiveRelocalizationWatchdog", header)
        self.assertIn("kLiveRelocalizationMaxDurationMs", live_source)
        self.assertIn("runtime.liveRelocalizationStartedMs = QDateTime::currentMSecsSinceEpoch()", request_body)
        self.assertIn("handleLiveRelocalizationWatchdog(nowMs)", frame_body)
        self.assertLess(frame_body.find("handleLiveRelocalizationWatchdog(nowMs)"),
                        frame_body.find("if (packet.receivedMs > 0 && packet.receivedMs < m_liveFrameAcceptAfterMs)"))
        self.assertIn("const bool relocalizationActive", watchdog_body)
        self.assertIn("m_liveStartupPhase = LiveStartupPhase::LocatePair", watchdog_body)
        self.assertIn("m_liveHardwareRoiActive = false", watchdog_body)
        self.assertIn("clearPendingLiveRelocalizationRois();", watchdog_body)
        self.assertIn("applyLiveFullFrameForRelocalization(&switchReason)", watchdog_body)
        self.assertIn("resetLiveFrameAcceptanceGates();", watchdog_body)
        self.assertIn("全画幅重定位超时", watchdog_body)


if __name__ == "__main__":
    unittest.main()

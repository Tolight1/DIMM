from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


def function_body(source: str, signature: str, next_marker: str) -> str:
    return source.split(signature, 1)[1].split(next_marker, 1)[0]


class PolarisCandidateRoiStaticTest(unittest.TestCase):
    def test_initial_star_candidate_type_records_selection_metadata(self):
        pipeline_h = read("src/PolarisDetectionPipeline.h")

        self.assertIn("struct InitialStarCandidate", pipeline_h)
        for field in [
            "int index",
            "QPointF center",
            "int area",
            "double peak",
            "double signal",
            "QRect bbox",
            "double distanceToPreference",
        ]:
            self.assertIn(field, pipeline_h)

    def test_candidate_detector_uses_connected_components_and_signal_sorting(self):
        detector_cpp = read("src/FullFrameStarDetector.cpp")
        body = function_body(
            detector_cpp,
            "QVector<InitialStarCandidate> detectInitialStarCandidates",
            "bool detectInitialStarCentroid",
        )

        self.assertIn("cv::connectedComponentsWithStats", body)
        self.assertIn("cv::CC_STAT_AREA", body)
        self.assertIn("cv::CC_STAT_WIDTH", body)
        self.assertIn("cv::CC_STAT_HEIGHT", body)
        self.assertIn("componentSignal", body)
        self.assertIn("std::sort", body)
        self.assertIn("signal > b.signal", body)

    def test_live_seed_uses_candidate_selection_before_roi_commit(self):
        dimm_cpp = read("src/DIMM.LiveRoi.cpp")
        body = function_body(
            dimm_cpp,
            "bool DIMM::maybeSeedRoiFromFrame",
            "void DIMM::updateFullFrameRoiOverlay",
        )

        self.assertIn("detectInitialStarCandidates", body)
        self.assertIn("selectInitialStarCandidate", body)
        self.assertIn("runtime.pendingInitialCandidateSelectionRequired", body)
        self.assertLess(body.find("selectInitialStarCandidate"), body.find("commitPairedInitialRoisIfReady"))

    def test_live_candidate_selection_is_automatic_without_dialog(self):
        dimm_cpp = read("src/DIMM.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        body = function_body(
            live_cpp,
            "bool DIMM::maybeSeedRoiFromFrame",
            "void DIMM::updateFullFrameRoiOverlay",
        )

        self.assertIn("#include <QInputDialog>", dimm_cpp)
        self.assertNotIn("QInputDialog::getInt", body)
        self.assertIn("selectInitialStarCandidate", body)
        self.assertIn("candidates.first()", body)
        self.assertIn("自动选择信号最强候选星", body)

    def test_full_frame_canvas_draws_candidate_overlay(self):
        canvas_h = read("src/CanvasWidgets.h")
        canvas_cpp = read("src/CanvasWidgets.cpp")

        self.assertIn("struct StarCandidateOverlay", canvas_h)
        self.assertIn("void setStarCandidateOverlays", canvas_h)
        self.assertIn("void clearStarCandidateOverlays", canvas_h)
        self.assertIn("drawStarCandidateOverlays", canvas_cpp)
        self.assertIn("candidate.index", canvas_cpp)
        self.assertIn("candidate.selected", canvas_cpp)

    def test_relocalization_records_and_uses_last_absolute_target(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        centroid_slot = dimm_cpp.split("&ImageProcessor::centroidReady", 1)[1].split(
            "connect(m_imageProcessor,",
            1,
        )[0]

        self.assertIn("lastTargetPosition", dimm_h)
        self.assertIn("hasLastTargetPosition", dimm_h)
        self.assertIn("runtime.lastTargetPosition[camIdx]", centroid_slot)
        self.assertIn("runtime.hasLastTargetPosition[camIdx] = true", centroid_slot)
        body = function_body(
            live_cpp,
            "bool DIMM::maybeSeedRoiFromFrame",
            "void DIMM::updateFullFrameRoiOverlay",
        )

        self.assertRegex(
            body,
            re.compile(
                r"selectInitialStarCandidate\([^;]*"
                r"usePreferenceGate[^;]*"
                r"preferredInitialTarget",
                re.S,
            ),
        )

    def test_live_seed_uses_alignment_confirmed_polaris_as_fallback(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.LiveRoi.cpp")
        body = function_body(
            dimm_cpp,
            "bool DIMM::maybeSeedRoiFromFrame",
            "void DIMM::updateFullFrameRoiOverlay",
        )

        self.assertIn("confirmedPolarisPosition", dimm_h)
        self.assertIn("hasConfirmedPolarisPosition", dimm_h)
        self.assertIn("hasTrackedTargetPreference", body)
        self.assertIn("hasAlignmentPolarisPreference", body)
        self.assertIn("runtime.hasLastTargetPosition[camIdx]", body)
        self.assertIn("runtime.hasConfirmedPolarisPosition[camIdx]", body)
        self.assertLess(body.find("runtime.hasLastTargetPosition[camIdx]"),
                        body.find("runtime.hasConfirmedPolarisPosition[camIdx]"))
        self.assertLess(body.find("runtime.lastTargetPosition[camIdx]"),
                        body.find("runtime.confirmedPolarisPosition[camIdx]"))
        self.assertIn("runtime.confirmedPolarisPosition[camIdx]", body)
        self.assertLess(body.find("runtime.hasConfirmedPolarisPosition[camIdx]"),
                        body.find("selectInitialStarCandidate"))

    def test_single_candidate_far_from_preference_can_relock_after_motion(self):
        pipeline_cpp = read("src/PolarisDetectionPipeline.cpp")
        body = function_body(
            pipeline_cpp,
            "InitialStarSelection PolarisDetectionPipeline::selectInitialStarCandidate",
            "bool chooseAutomaticInitialStarCandidate",
        )

        self.assertIn("candidates.size() == 1", body)
        self.assertIn("selection.candidate = candidates.first()", body)
        self.assertLess(body.find("candidates.size() == 1"),
                        body.find("Nearest candidate is too far from the last target position"))

    def test_far_preference_can_use_dominant_candidate_before_manual_selection(self):
        pipeline_cpp = read("src/PolarisDetectionPipeline.cpp")
        body = function_body(
            pipeline_cpp,
            "InitialStarSelection PolarisDetectionPipeline::selectInitialStarCandidate",
            "bool chooseAutomaticInitialStarCandidate",
        )

        self.assertIn("nextSignal", body)
        self.assertIn("strongest.signal >= nextSignal * 2.0", body)
        self.assertLess(body.find("strongest.signal >= nextSignal * 2.0"),
                        body.find("Nearest candidate is too far from the last target position"))

    def test_measurement_reset_preserves_alignment_confirmed_polaris(self):
        dimm_cpp = read("src/DIMM.cpp")
        body = function_body(
            dimm_cpp,
            "void DIMM::resetMeasurementState",
            "void DIMM::updateCaptureState",
        )

        self.assertIn("preservedConfirmedPolarisPosition", body)
        self.assertIn("preservedHasConfirmedPolarisPosition", body)
        self.assertLess(body.find("preservedConfirmedPolarisPosition"), body.find("runtime = CaptureRuntimeContext()"))
        self.assertGreater(body.rfind("runtime.confirmedPolarisPosition"), body.find("runtime = CaptureRuntimeContext()"))

    def test_start_capture_does_not_block_for_alignment_confirmation(self):
        dimm_cpp = read("src/DIMM.cpp")
        body = function_body(
            dimm_cpp,
            "void DIMM::onStartCapture()",
            "void DIMM::onStopCapture()",
        )

        self.assertIn("resetMeasurementState()", body)
        self.assertNotIn("hasConfirmedPolarisForLiveStart", body)
        self.assertNotIn("建议先进入对准模式确认北极星", body)
        self.assertNotIn("进入对准模式", body)
        self.assertNotIn("startAlignmentMode(&alignmentReason)", body)
        self.assertNotIn("继续直接采集", body)
        self.assertNotIn("DestructiveRole", body)


    def test_live_direct_start_does_not_confirm_unverified_polaris(self):
        dimm_cpp = read("src/DIMM.LiveRoi.cpp")
        body = function_body(
            dimm_cpp,
            "bool DIMM::maybeSeedRoiFromFrame",
            "void DIMM::updateFullFrameRoiOverlay",
        )

        self.assertIn("chooseAutomaticInitialStarCandidate", body)
        self.assertIn("const bool hasAlignmentPolarisPreference", body)
        self.assertIn("if (hasAlignmentPolarisPreference)", body)
        self.assertNotIn("selection.candidate = candidates.first()", body)
        self.assertNotRegex(
            body,
            re.compile(
                r"centroid = selection\.candidate\.center;\s*"
                r"runtime\.confirmedPolarisPosition\[cameraIndex\] = centroid;\s*"
                r"runtime\.hasConfirmedPolarisPosition\[cameraIndex\] = true;",
                re.S,
            ),
        )


if __name__ == "__main__":
    unittest.main()

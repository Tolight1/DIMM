from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentModeStaticTest(unittest.TestCase):
    def test_full_frame_canvas_exposes_alignment_overlay(self):
        canvas_h = read("src/CanvasWidgets.h")
        canvas_cpp = read("src/CanvasWidgets.cpp")

        self.assertIn("struct AlignmentOverlay", canvas_h)
        self.assertIn("setAlignmentOverlay", canvas_h)
        self.assertIn("clearAlignmentOverlay", canvas_h)
        self.assertIn("drawAlignmentOverlay", canvas_cpp)

    def test_dimm_has_isolated_alignment_mode_controls(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        runtime_helpers_h = read("src/DimmRuntimeHelpers.h")

        for token in [
            "Alignment",
            "onToggleAlignmentMode",
            "onConfirmCamera1PolarisCandidate",
            "onConfirmCamera2PolarisCandidate",
            "m_actionAlignmentMode",
            "m_actionConfirmCamera1Polaris",
            "m_actionConfirmCamera2Polaris",
        ]:
            self.assertIn(token, dimm_h)
        self.assertIn("startAlignmentMode", alignment_cpp)
        self.assertIn("stopAlignmentMode", alignment_cpp)
        self.assertIn("kAlignmentPreviewIntervalMs", runtime_helpers_h)
        self.assertIn("setAlignmentOverlay", alignment_cpp)

    def test_alignment_mode_uses_candidate_selection_for_polaris_confirmation(self):
        dimm_cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        dimm_h = read("src/DIMM.h")
        clear_body = alignment_cpp.split("void DIMM::clearAlignmentCanvasesForStart", 1)[1].split(
            "void DIMM::clearAlignmentCanvasesForStop", 1
        )[0]
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "bool DIMM::handleManualAlignmentFrameTracking", 1
        )[0]
        overlay_body = alignment_cpp.split("void DIMM::updateAlignmentOverlay", 1)[1]
        collect_body = alignment_cpp.split("QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1)[1].split(
            "bool DIMM::handleAlignmentCandidateSelection", 1
        )[0]
        candidate_apply_body = dimm_cpp.split("void DIMM::applyAlignmentSelectedCandidate", 1)[1].split(
            "void DIMM::applyManualAlignmentConfirmation", 1
        )[0]

        self.assertIn("confirmedPolarisPosition", dimm_h)
        self.assertIn("hasConfirmedPolarisPosition", dimm_h)
        self.assertIn("clearStarCandidateOverlays", clear_body)
        self.assertNotIn("clearStarCandidateOverlays", packet_body)
        self.assertIn("detectInitialStarCandidates", collect_body)
        self.assertIn("m_alignmentSession.camera(cameraIndex).selectionRequested", overlay_body)
        self.assertIn("QInputDialog::getInt", alignment_cpp)
        self.assertIn("runtime.confirmedPolarisPosition[cameraIndex]", candidate_apply_body)
        self.assertIn("runtime.hasConfirmedPolarisPosition[cameraIndex]", candidate_apply_body)

    def test_alignment_confirmation_actions_request_manual_selection_per_camera(self):
        dimm_cpp = read("src/DIMM.Alignment.cpp")
        cam1_body = dimm_cpp.split("void DIMM::onConfirmCamera1PolarisCandidate", 1)[1].split(
            "void DIMM::onConfirmCamera2PolarisCandidate", 1
        )[0]
        cam2_body = dimm_cpp.split("void DIMM::onConfirmCamera2PolarisCandidate", 1)[1].split(
            "void DIMM::requestAlignmentPolarisSelection", 1
        )[0]
        helper_body = dimm_cpp.split("void DIMM::requestAlignmentPolarisSelection", 1)[1].split(
            "bool DIMM::startAlignmentMode", 1
        )[0]

        self.assertIn("requestAlignmentPolarisSelection(0)", cam1_body)
        self.assertNotIn("selectionRequested = true", cam1_body)
        self.assertIn("requestAlignmentPolarisSelection(1)", cam2_body)
        self.assertNotIn("selectionRequested = true", cam2_body)
        self.assertIn("cameraState.selectionRequested = true", helper_body)
        self.assertIn("runtime.selectedInitialCandidateIndex[cameraIndex] = -1", helper_body)

    def test_alignment_candidate_prompt_is_single_shot_and_informative(self):
        dimm_cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        prompt_body = alignment_cpp.split("bool DIMM::promptAlignmentCandidateSelection", 1)[1].split(
            "void DIMM::updateAlignmentOverlay", 1
        )[0]
        manual_prompt_body = dimm_cpp.split("bool DIMM::handleManualAlignmentCandidatePrompt", 1)[1].split(
            "void DIMM::applyAlignmentSelectedCandidate", 1
        )[0]

        self.assertIn("candidatePromptLines(candidates)", prompt_body)
        self.assertIn("QInputDialog::getInt", prompt_body)
        self.assertIn("m_alignmentSession.camera(cameraIndex).selectionRequested = false", manual_prompt_body)
        self.assertLess(
            manual_prompt_body.find("m_alignmentSession.camera(cameraIndex).selectionRequested = false"),
            manual_prompt_body.find("promptAlignmentCandidateSelection"),
        )
        self.assertNotIn("Polaris alignment selection", prompt_body)
        self.assertNotIn("Camera %1 candidates", prompt_body)

    def test_start_capture_is_guarded_while_alignment_is_active(self):
        dimm_cpp = read("src/DIMM.cpp")
        start_body = dimm_cpp.split("void DIMM::onStartCapture()", 1)[1].split(
            "void DIMM::onStartSimulation()", 1
        )[0]

        self.assertIn("CaptureState::Alignment", start_body)
        self.assertNotIn("startDualCameraLocalization(&reason)", start_body.split("CaptureState::Alignment", 1)[0])

    def test_single_alignment_candidate_confirmation_also_uses_prompt(self):
        dimm_cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        helper_body = alignment_cpp.split("void DIMM::requestAlignmentPolarisSelection", 1)[1].split(
            "bool DIMM::startAlignmentMode", 1
        )[0]
        selection_body = alignment_cpp.split("bool DIMM::handleAlignmentCandidateSelection", 1)[1].split(
            "bool DIMM::promptAlignmentCandidateSelection", 1
        )[0]
        candidate_apply_body = dimm_cpp.split("void DIMM::applyAlignmentSelectedCandidate", 1)[1].split(
            "void DIMM::applyManualAlignmentConfirmation", 1
        )[0]

        self.assertIn("cameraState.lastPreviewMs = -1", helper_body)
        self.assertIn("if (manualSelectionRequested &&", selection_body)
        self.assertNotIn("single candidate auto", selection_body)
        self.assertIn("refreshActionStates()", candidate_apply_body)
        self.assertLess(
            candidate_apply_body.find("AlignmentSession::recordSelectedCandidate"),
            candidate_apply_body.find("refreshActionStates()"),
        )

    def test_manual_confirmation_uses_cached_candidates_without_rescanning_frame(self):
        dimm_cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        dimm_h = read("src/DIMM.h")
        helper_body = alignment_cpp.split("void DIMM::requestAlignmentPolarisSelection", 1)[1].split(
            "bool DIMM::startAlignmentMode", 1
        )[0]
        overlay_body = alignment_cpp.split("void DIMM::updateAlignmentOverlay", 1)[1]

        self.assertIn("m_alignmentCachedCandidates[kCameraCount]", dimm_h)
        self.assertIn("m_alignmentCachedCandidates[cameraIndex]", helper_body)
        self.assertIn("promptAlignmentCandidateSelection", helper_body)
        self.assertIn("applyAlignmentSelectedCandidate", helper_body)
        self.assertNotIn("updateAlignmentOverlay(cameraIndex", helper_body)
        self.assertIn("m_alignmentCachedCandidates[cameraIndex] = candidates", overlay_body)

    def test_alignment_mode_starts_unconfirmed_and_does_not_auto_confirm_candidates(self):
        dimm_cpp = read("src/DIMM.Alignment.cpp")
        reset_body = dimm_cpp.split("void DIMM::resetAlignmentRuntimeForStart", 1)[1].split(
            "void DIMM::resetAlignmentRuntimeForStop", 1
        )[0]
        selection_body = dimm_cpp.split("bool DIMM::handleAlignmentCandidateSelection", 1)[1].split(
            "bool DIMM::promptAlignmentCandidateSelection", 1
        )[0]

        self.assertIn("resetCameraForStart", reset_body)
        self.assertIn("const bool canApplyAlignmentSelection", selection_body)
        self.assertIn("if (selection.selected && canApplyAlignmentSelection)", selection_body)

    def test_manual_confirmation_reports_when_no_candidates_are_detected(self):
        dimm_cpp = read("src/DIMM.Alignment.cpp")
        overlay_body = dimm_cpp.split("void DIMM::updateAlignmentOverlay", 1)[1]

        self.assertIn("manualSelectionRequested", overlay_body)
        self.assertIn("未检测到候选星点", overlay_body)
        self.assertIn("lastInitialCandidatePromptMs", overlay_body)

    def test_candidate_detector_does_not_hide_reject_wide_alignment_stars(self):
        detector_cpp = read("src/FullFrameStarDetector.cpp")
        detector_body = detector_cpp.split("QVector<InitialStarCandidate> detectInitialStarCandidates", 1)[1].split(
            "bool detectInitialStarCentroid",
            1,
        )[0]

        self.assertIn("area < config.minArea || area > config.maxArea", detector_body)
        self.assertNotIn("width > kInitialStarMaxWidth", detector_body)
        self.assertNotIn("height > kInitialStarMaxHeight", detector_body)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverDimmIntegrationStaticTest(unittest.TestCase):
    def test_dimm_owns_solver_controller_and_generation(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")

        self.assertIn('#include "PolarisSolver.h"', cpp)
        self.assertIn("PolarisSolverController* m_polarisSolverController", header)
        self.assertIn("AlignmentSession m_alignmentSession", header)
        self.assertIn("AlignmentSession m_alignmentSession", header)
        self.assertIn("onPolarisSolveFinished(PolarisSolveResult result)", header)
        self.assertIn("onPolarisSolveStatusChanged", header)
        self.assertIn("new PolarisSolverController(this)", cpp)
        self.assertIn("&PolarisSolverController::solveFinished", cpp)
        self.assertIn("&PolarisSolverController::solveStatusChanged", cpp)

    def test_alignment_mode_submits_and_cancels_solver_work(self):
        cpp = read("src/DIMM.Alignment.cpp")
        start_body = cpp.split("bool DIMM::startAlignmentMode", 1)[1].split(
            "void DIMM::stopAlignmentMode", 1
        )[0]
        stop_body = cpp.split("void DIMM::stopAlignmentMode", 1)[1].split(
            "bool DIMM::prepareAlignmentCamerasForPreview", 1
        )[0]
        reset_start_body = cpp.split("void DIMM::resetAlignmentRuntimeForStart", 1)[1].split(
            "void DIMM::resetAlignmentRuntimeForStop", 1
        )[0]
        reset_stop_body = cpp.split("void DIMM::resetAlignmentRuntimeForStop", 1)[1].split(
            "void DIMM::clearAlignmentCanvasesForStart", 1
        )[0]
        packet_body = cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::updateAlignmentOverlay", 1
        )[0]

        # Starting and stopping alignment delegate solver-lifecycle work to the reset helpers.
        self.assertIn("resetAlignmentRuntimeForStart()", start_body)
        self.assertIn("resetAlignmentRuntimeForStop()", stop_body)

        # The reset helpers invalidate in-flight solver work on both start and stop:
        # advance the generation and cancel queued/tracked tasks.
        self.assertIn("m_alignmentSession.advanceSolveGeneration()", reset_start_body)
        self.assertIn("m_polarisSolverController->cancelAll(solveGeneration)", reset_start_body)
        self.assertIn("m_alignmentSession.advanceSolveGeneration()", reset_stop_body)
        self.assertIn("m_polarisSolverController->cancelAll(solveGeneration)", reset_stop_body)

        # Solver submission on the frame-packet path is preserved.
        self.assertIn("buildPolarisSolverConfig()", packet_body)
        self.assertIn("AlignmentTaskManager::submitFullSolve", packet_body)
        self.assertLess(packet_body.find("targetCanvas->setImage"),
                        packet_body.find("AlignmentTaskManager::submitFullSolve"))

    def test_solver_result_confirms_runtime_polaris_position(self):
        cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        result_body = alignment_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]

        self.assertIn("result.generation != m_alignmentSession.solveGeneration()", result_body)
        self.assertIn("cameraState.solveResult = result", result_body)
        self.assertIn("setStatusMessage", result_body)
        self.assertIn("result.valid && result.hasDetectedPolarisPixel", result_body)
        self.assertIn("runtime.confirmedPolarisPosition[result.cameraIndex] = result.detectedPolarisPixel", result_body)
        self.assertIn("runtime.hasConfirmedPolarisPosition[result.cameraIndex] = true", result_body)
        self.assertIn("runtime.lastTargetPosition[result.cameraIndex] = result.detectedPolarisPixel", result_body)
        self.assertIn("refreshActionStates()", result_body)

    def test_solved_result_drives_alignment_overlay(self):
        cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        overlay_body = alignment_cpp.split("void DIMM::updateAlignmentOverlay", 1)[1].split(
            "void DIMM::handleSavedFrame", 1
        )[0]
        result_body = alignment_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")

        self.assertIn("const PolarisSolveResult& solved = cameraState.solveResult", overlay_body)
        self.assertIn("AlignmentUiPresenter::OverlayBuildInput overlayInput", overlay_body)
        self.assertIn("overlayInput.solved = &solved", overlay_body)
        self.assertIn("overlayInput.hasCurrentSolverResult = hasCurrentSolverResult", overlay_body)
        self.assertIn("AlignmentUiPresenter::buildAlignmentOverlay(overlayInput)", overlay_body)
        self.assertIn("自动识别北极星", presenter_cpp)
        self.assertIn("AlignmentUiPresenter::formatSolvedStatusMessage", result_body)


if __name__ == "__main__":
    unittest.main()

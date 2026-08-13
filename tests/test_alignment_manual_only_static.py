from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentManualOnlyStaticTest(unittest.TestCase):
    def test_manual_confirmation_enters_manual_only_and_resets_retry_state(self):
        cpp = read("src/DIMM.cpp")
        selection_body = cpp.split("void DIMM::applyManualAlignmentConfirmation", 1)[1].split(
            "void DIMM::updateConfirmedPolarisFromFallbackCentroid", 1
        )[0]
        controller_h = read("src/PolarisSolver.h")
        controller_cpp = read("src/PolarisSolver.cpp")
        session_cpp = read("src/AlignmentSession.cpp")

        self.assertIn("void cancelCamera(int cameraIndex, quint64 generation)", controller_h)
        self.assertIn("void PolarisSolverController::cancelCamera", controller_cpp)
        self.assertIn("m_alignmentSession.applyManualConfirmation", selection_body)
        self.assertIn("runtime.state = AlignmentSolveState::ManualOnly", session_cpp)
        self.assertIn("runtime.nextRetryMs = -1", session_cpp)
        self.assertIn("runtime.consecutiveTrackFailures = 0", session_cpp)
        self.assertIn("runtime.consecutiveLowConfidenceResults = 0", session_cpp)
        self.assertIn("runtime.lastPolarisPosition = selectedPosition", session_cpp)
        self.assertIn("runtime.hasLastPolarisPosition = true", session_cpp)
        self.assertIn("m_polarisSolverController->cancelCamera(cameraIndex", selection_body)

    def test_manual_only_blocks_auto_retry_and_stale_auto_results(self):
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]
        request_body = alignment_cpp.split("void DIMM::requestAutomaticPolarisSolve", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolveBoth", 1
        )[0]
        finished_body = alignment_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]
        task_cpp = read("src/AlignmentTaskManager.cpp")
        controller_cpp = read("src/AlignmentController.cpp")
        frame_cpp = read("src/AlignmentFrameCoordinator.cpp")

        self.assertIn("AlignmentFrameCoordinator::FrameAction::ManualTrack", packet_body)
        self.assertIn("runtime.state == AlignmentSolveState::ManualOnly", frame_cpp)
        self.assertIn("return FrameAction::ManualTrack", frame_cpp)
        self.assertIn("AlignmentTaskManager::prepareFullSolveRequest", request_body)
        self.assertIn("runtime->state == AlignmentSolveState::ManualOnly && !force", task_cpp)
        self.assertIn("AlignmentController::shouldIgnoreSolverResult", finished_body)
        self.assertIn("runtime.state == AlignmentSolveState::ManualOnly", controller_cpp)
        self.assertLess(
            finished_body.find("AlignmentController::shouldIgnoreSolverResult"),
            finished_body.find("cameraState.solveResult = result"),
        )

    def test_forced_reidentify_exits_manual_only_before_full_solve(self):
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        request_body = alignment_cpp.split("void DIMM::requestAutomaticPolarisSolve", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolveBoth", 1
        )[0]
        task_cpp = read("src/AlignmentTaskManager.cpp")

        self.assertIn("AlignmentTaskManager::prepareFullSolveRequest", request_body)
        self.assertIn("force && runtime->state == AlignmentSolveState::ManualOnly", task_cpp)
        self.assertIn("runtime->state = AlignmentSolveState::WaitingFrame", task_cpp)
        self.assertIn("PolarisTracker::markFullSolveSubmitted", task_cpp)
        self.assertLess(
            task_cpp.find("runtime->state = AlignmentSolveState::WaitingFrame"),
            task_cpp.find("PolarisTracker::markFullSolveSubmitted"),
        )


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisAlignmentStateMachineStaticTest(unittest.TestCase):
    def test_alignment_declares_per_camera_solve_state(self):
        header = read("src/DIMM.h")
        types_header = read("src/AlignmentTypes.h")

        self.assertIn('#include "AlignmentTypes.h"', header)
        self.assertIn("enum class AlignmentSolveState", types_header)
        self.assertIn("struct AlignmentCameraSolveRuntime", types_header)
        self.assertIn("AlignmentSession m_alignmentSession", header)
        self.assertIn("AlignmentCameraSolveRuntime solveRuntime", types_header)
        self.assertIn("qint64 lastFullSolveMs", types_header)
        self.assertIn("qint64 nextRetryMs", types_header)
        self.assertIn("int consecutiveTrackFailures", types_header)
        self.assertIn("QRect trackingWindow", types_header)

    def test_alignment_frames_use_tracking_before_retrying_full_solve(self):
        cpp = read("src/DIMM.Alignment.cpp")
        header = read("src/DIMM.h")
        packet_body = cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]
        request_body = cpp.split("void DIMM::requestAutomaticPolarisSolve", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolveBoth", 1
        )[0]
        finished_body = cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]
        task_cpp = read("src/AlignmentTaskManager.cpp")
        controller_cpp = read("src/AlignmentController.cpp")
        frame_cpp = read("src/AlignmentFrameCoordinator.cpp")

        self.assertIn("bool trackAlignmentPolarisLocally", header)
        self.assertIn("trackAlignmentPolarisLocally(cameraIndex", packet_body)
        self.assertIn("FrameAction::AutomaticTrack", packet_body)
        self.assertIn("FrameAction::WaitRetry", packet_body)
        self.assertIn("AlignmentSolveState::Tracking", frame_cpp)
        self.assertIn("AlignmentSolveState::RetryWaiting", frame_cpp)
        self.assertIn("PolarisTracker::recordTrackFailure", packet_body)
        self.assertNotIn("AlignmentController::applyTrackingFailure", controller_cpp)
        self.assertIn("m_alignmentRetryIntervalMs", packet_body)
        self.assertIn("AlignmentTaskManager::prepareFullSolveRequest", request_body)
        self.assertIn("PolarisTracker::shouldHoldFullSolveRequest", task_cpp)
        self.assertIn("PolarisTracker::markFullSolveSubmitted", task_cpp)
        self.assertIn("AlignmentController::applyDetectedPolarisSolve", finished_body)
        self.assertIn("runtime->state = AlignmentSolveState::Tracking", controller_cpp)
        self.assertIn("runtime->consecutiveTrackFailures = 0", controller_cpp)
        self.assertIn("result.valid && !result.hasDetectedPolarisPixel", finished_body)
        self.assertIn("AlignmentTaskManager::applySolveFailureRetry", finished_body)


if __name__ == "__main__":
    unittest.main()

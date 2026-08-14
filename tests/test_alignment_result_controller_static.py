from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentResultControllerStaticTest(unittest.TestCase):
    def test_controller_owns_solved_result_runtime_transition(self):
        header = read("src/AlignmentController.h")
        cpp = read("src/AlignmentController.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        finished_body = alignment_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]

        self.assertIn("applyDetectedPolarisSolve", header)
        self.assertIn("AlignmentController::applyDetectedPolarisSolve", cpp)
        self.assertIn("runtime->state = AlignmentSolveState::Tracking", cpp)
        self.assertIn("runtime->lastPolarisPosition = result.detectedPolarisPixel", cpp)
        self.assertIn("AlignmentController::applyDetectedPolarisSolve(&solveRuntime, result)", finished_body)
        self.assertNotIn("solveRuntime.state = AlignmentSolveState::Tracking", finished_body)

    def test_controller_owns_predicted_only_and_error_transitions(self):
        header = read("src/AlignmentController.h")
        cpp = read("src/AlignmentController.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        finished_body = alignment_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]

        self.assertIn("applyPredictedOnlyRetry", header)
        self.assertIn("applySolveError", header)
        self.assertIn("runtime->state = AlignmentSolveState::RetryWaiting", cpp)
        self.assertIn("runtime->nextRetryMs = nowMs + retryIntervalMs", cpp)
        self.assertIn("runtime->state = AlignmentSolveState::Error", cpp)
        self.assertIn("AlignmentController::applyPredictedOnlyRetry(&solveRuntime", finished_body)
        self.assertIn("AlignmentController::applySolveError(&solveRuntime)", finished_body)
        self.assertNotIn("solveRuntime.state = AlignmentSolveState::RetryWaiting", finished_body)
        self.assertNotIn("solveRuntime.state = AlignmentSolveState::Error", finished_body)


if __name__ == "__main__":
    unittest.main()

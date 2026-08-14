from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentControllerSplitStaticTest(unittest.TestCase):
    def test_alignment_controller_modules_are_explicit_build_units(self):
        cmake = read("CMakeLists.txt")
        for path in (
            "src/AlignmentController.h",
            "src/AlignmentController.cpp",
            "src/AlignmentTaskManager.h",
            "src/AlignmentTaskManager.cpp",
            "src/AlignmentSession.h",
            "src/AlignmentSession.cpp",
        ):
            self.assertIn(path, cmake)

    def test_task_manager_owns_full_solve_and_retry_state_transitions(self):
        header = read("src/AlignmentTaskManager.h")
        cpp = read("src/AlignmentTaskManager.cpp")
        dimm = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")

        self.assertIn("class AlignmentTaskManager", header)
        self.assertIn("prepareFullSolveRequest", header)
        self.assertIn("applySolveFailureRetry", header)
        self.assertIn("AlignmentTaskManager::prepareFullSolveRequest", alignment_cpp)
        self.assertIn("AlignmentTaskManager::applySolveFailureRetry", alignment_cpp)
        self.assertIn("AlignmentSolveState::ManualOnly", cpp)
        self.assertIn("PolarisTracker::markFullSolveSubmitted", cpp)
        self.assertIn("lowConfidenceRetryMultiplier", cpp)

    def test_alignment_session_owns_manual_confirmation_state(self):
        header = read("src/AlignmentSession.h")
        cpp = read("src/AlignmentSession.cpp")
        dimm = read("src/DIMM.cpp")

        self.assertIn("class AlignmentSession", header)
        self.assertIn("applyManualConfirmation", header)
        self.assertIn("manualConfirmedMessage", header)
        self.assertIn("m_alignmentSession.applyManualConfirmation", dimm)
        self.assertIn("AlignmentSession::manualConfirmedMessage", dimm)
        self.assertIn("runtime.state = AlignmentSolveState::ManualOnly", cpp)
        self.assertIn("runtime.nextRetryMs = -1", cpp)
        self.assertIn("runtime.consecutiveLowConfidenceResults = 0", cpp)

    def test_alignment_controller_is_used_as_result_guard_facade(self):
        header = read("src/AlignmentController.h")
        cpp = read("src/AlignmentController.cpp")
        dimm = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")

        self.assertIn("class AlignmentController", header)
        self.assertIn("shouldIgnoreSolverResult", header)
        self.assertIn("AlignmentController::shouldIgnoreSolverResult", alignment_cpp)
        self.assertIn("runtime.state == AlignmentSolveState::ManualOnly", cpp)


if __name__ == "__main__":
    unittest.main()

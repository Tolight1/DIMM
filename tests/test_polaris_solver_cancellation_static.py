from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverCancellationStaticTest(unittest.TestCase):
    def test_solver_and_matcher_accept_cancellation_token(self):
        solver_h = read("src/PolarisSolver.h")
        solver_cpp = read("src/PolarisSolver.cpp")
        matcher_h = read("src/StarPatternMatcher.h")
        matcher_cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("#include <atomic>", solver_h)
        self.assertIn("std::shared_ptr<std::atomic_bool>", solver_h)
        self.assertIn("std::shared_ptr<std::atomic_bool>", matcher_h)
        self.assertIn("solveFrame(const cv::Mat& frame", solver_h)
        self.assertIn("const std::shared_ptr<std::atomic_bool>& cancelled", solver_h)
        self.assertIn("matcherConfig.cancelled = cancelled", solver_cpp)
        self.assertIn("if (cancelled && cancelled->load())", solver_cpp)
        self.assertIn("cancelCurrentSolveTasks()", solver_cpp)
        self.assertIn("m_cancelledTokens", solver_h)
        self.assertIn("token->store(true)", solver_cpp)
        self.assertIn("if (config.cancelled && config.cancelled->load())", matcher_cpp)

    def test_controller_gives_each_running_task_its_own_cancel_token(self):
        solver_h = read("src/PolarisSolver.h")
        solver_cpp = read("src/PolarisSolver.cpp")
        pending_body = solver_h.split("struct PendingSolveTask", 1)[1].split(
            "void startSolveTask", 1
        )[0]
        start_body = solver_cpp.split("void PolarisSolverController::startSolveTask", 1)[1].split(
            "void PolarisSolverController::cancelAll", 1
        )[0]

        self.assertIn("std::shared_ptr<std::atomic_bool> cancelled", pending_body)
        self.assertIn("std::make_shared<std::atomic_bool>(false)", start_body)
        self.assertIn("m_cancelledTokens[cameraIndex] = cancelled", start_body)
        self.assertIn("solveFrameWithProgress(frameForWorker, config, cancelled, progress)", start_body)
        self.assertIn("pendingTask.cancelled", start_body)


if __name__ == "__main__":
    unittest.main()

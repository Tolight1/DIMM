from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverTaskIdRaceStaticTest(unittest.TestCase):
    def test_solver_tasks_and_results_carry_task_id(self):
        header = read("src/PolarisSolver.h")
        result_body = header.split("struct PolarisSolveResult", 1)[1].split(
            "QVector<DetectedStar>", 1
        )[0]
        pending_body = header.split("struct PendingSolveTask", 1)[1].split(
            "void cancelCurrentSolveTasks", 1
        )[0]

        self.assertIn("quint64 taskId", result_body)
        self.assertIn("quint64 taskId", pending_body)
        self.assertIn("int cameraIndex", pending_body)
        self.assertIn("quint64 m_nextTaskId", header)
        self.assertIn("quint64 m_activeTaskId[kCameraCount]", header)

    def test_completion_callback_ignores_stale_task_before_state_cleanup(self):
        cpp = read("src/PolarisSolver.cpp")
        callback_body = cpp.split("[this, result]() mutable", 1)[1].split(
            "if (result.generation != m_generation)", 1
        )[0]

        self.assertIn("result.taskId != m_activeTaskId[result.cameraIndex]", callback_body)
        self.assertLess(
            callback_body.find("result.taskId != m_activeTaskId[result.cameraIndex]"),
            callback_body.find("m_taskRunning[result.cameraIndex] = false"),
        )
        self.assertLess(
            callback_body.find("result.taskId != m_activeTaskId[result.cameraIndex]"),
            callback_body.find("m_cancelledTokens[result.cameraIndex].reset()"),
        )

    def test_cancel_all_marks_tokens_but_leaves_running_state_for_real_completion(self):
        cpp = read("src/PolarisSolver.cpp")
        cancel_body = cpp.split("void PolarisSolverController::cancelAll", 1)[1]

        self.assertIn("cancelCurrentSolveTasks()", cancel_body)
        self.assertIn("m_pendingLatestTask[0] = PendingSolveTask()", cancel_body)
        self.assertIn("m_pendingLatestTask[1] = PendingSolveTask()", cancel_body)
        self.assertNotIn("m_taskRunning[0] = false", cancel_body)
        self.assertNotIn("m_taskRunning[1] = false", cancel_body)


if __name__ == "__main__":
    unittest.main()

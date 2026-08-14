from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentTaskSubmissionSplitStaticTest(unittest.TestCase):
    def test_task_manager_owns_solver_submission_call(self):
        header = read("src/AlignmentTaskManager.h")
        cpp = read("src/AlignmentTaskManager.cpp")
        dimm = read("src/DIMM.Alignment.cpp")
        request_body = dimm.split("void DIMM::requestAutomaticPolarisSolve", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolveBoth", 1
        )[0]

        self.assertIn("submitFullSolve", header)
        self.assertIn("PolarisSolverController* solverController", header)
        self.assertIn("AlignmentTaskManager::submitFullSolve", cpp)
        self.assertIn("solverController->submitFrame", cpp)
        self.assertIn("AlignmentTaskManager::submitFullSolve", request_body)
        self.assertNotIn("m_polarisSolverController->submitFrame", request_body)


if __name__ == "__main__":
    unittest.main()

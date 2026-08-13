from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverPhaseAStaticTest(unittest.TestCase):
    def test_solver_header_defines_phase_a_contract(self):
        header = read("src/PolarisSolver.h")

        self.assertIn("struct DetectedStar", header)
        self.assertIn("struct PolarisSolverConfig", header)
        self.assertIn("enum class PolarisSolveStatus", header)
        self.assertIn("struct CatalogImageMatch", header)
        self.assertIn("struct PolarisSolveResult", header)
        self.assertIn("class PolarisSolverController", header)
        self.assertIn("void submitFrame(int cameraIndex", header)
        self.assertIn("void cancelAll(quint64 newGeneration)", header)
        self.assertIn("Q_DECLARE_METATYPE(PolarisSolveResult)", header)

    def test_solver_runtime_integration_is_explicit(self):
        dimm_cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        solver_cpp = read("src/PolarisSolver.cpp")
        task_cpp = read("src/AlignmentTaskManager.cpp")

        self.assertIn('qRegisterMetaType<PolarisSolveResult>("PolarisSolveResult")', dimm_cpp)
        self.assertIn("PolarisSolverController::submitFrame", solver_cpp)
        self.assertIn("PolarisSolveStatus::Idle", solver_cpp)
        self.assertIn("new PolarisSolverController(this)", dimm_cpp)
        self.assertIn("&PolarisSolverController::solveFinished", dimm_cpp)
        self.assertIn("&PolarisSolverController::solveStatusChanged", dimm_cpp)
        self.assertIn("AlignmentTaskManager::submitFullSolve", alignment_cpp)
        self.assertIn("solverController->submitFrame", task_cpp)


if __name__ == "__main__":
    unittest.main()

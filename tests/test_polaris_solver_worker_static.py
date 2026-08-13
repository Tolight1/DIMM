from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverWorkerStaticTest(unittest.TestCase):
    def test_controller_owns_worker_thread_and_limits_tasks(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")

        self.assertIn("~PolarisSolverController()", header)
        self.assertIn("class PolarisSolverWorker", cpp)
        self.assertIn("m_workerThread", header)
        self.assertIn("m_worker", header)
        self.assertIn("m_taskRunning", header)
        self.assertIn("moveToThread", cpp)
        self.assertIn("QMetaObject::invokeMethod", cpp)
        self.assertIn("cv::Mat frameForWorker = frame", cpp)
        self.assertIn("solveFrameWithProgress(frameForWorker, config, cancelled, progress)", cpp)
        self.assertIn("m_taskRunning[cameraIndex] = true", cpp)
        self.assertIn("m_taskRunning[result.cameraIndex] = false", cpp)

    def test_controller_keeps_only_latest_pending_task_per_camera(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")
        submit_body = cpp.split("void PolarisSolverController::submitFrame", 1)[1].split(
            "void PolarisSolverController::cancelAll", 1
        )[0]

        self.assertIn("struct PendingSolveTask", header)
        self.assertIn("PendingSolveTask m_pendingLatestTask[kCameraCount]", header)
        self.assertIn("void startSolveTask", header)
        self.assertIn("if (m_taskRunning[cameraIndex])", submit_body)
        self.assertIn("m_pendingLatestTask[cameraIndex] =", submit_body)
        self.assertIn("PendingSolveTask{true", submit_body)
        self.assertIn("Queued latest Polaris solve request", submit_body)
        self.assertNotIn("m_taskRunning[cameraIndex] && !forceSolve", submit_body)
        self.assertIn("const PendingSolveTask pendingTask = m_pendingLatestTask[result.cameraIndex]", cpp)
        self.assertIn("m_pendingLatestTask[result.cameraIndex] = PendingSolveTask()", cpp)
        self.assertIn("startSolveTask(result.cameraIndex", cpp)

    def test_controller_cancels_generation_and_stops_thread(self):
        cpp = read("src/PolarisSolver.cpp")

        self.assertIn("cancelAll(quint64 newGeneration)", cpp)
        self.assertIn("m_generation = newGeneration", cpp)
        self.assertIn("result.generation != m_generation", cpp)
        self.assertIn("m_workerThread->quit()", cpp)
        self.assertIn("m_workerThread->wait()", cpp)

    def test_controller_destructor_does_not_emit_to_destroying_receivers(self):
        cpp = read("src/PolarisSolver.cpp")
        destructor_body = cpp.split("PolarisSolverController::~PolarisSolverController()", 1)[1].split(
            "void PolarisSolverController::submitFrame", 1
        )[0]

        self.assertNotIn("cancelAll(", destructor_body)
        self.assertIn("m_generation", destructor_body)
        self.assertIn("m_taskRunning[0] = false", destructor_body)
        self.assertIn("m_taskRunning[1] = false", destructor_body)

    def test_dimm_disconnects_solver_before_destruction_finishes(self):
        cpp = read("src/DIMM.cpp")
        destructor_body = cpp.split("DIMM::~DIMM()", 1)[1].split(
            "void DIMM::setupConnections", 1
        )[0]

        self.assertIn("disconnect(m_polarisSolverController, nullptr, this, nullptr)", destructor_body)
        self.assertLess(destructor_body.find("disconnect(m_polarisSolverController"),
                        destructor_body.find("delete ui"))


if __name__ == "__main__":
    unittest.main()

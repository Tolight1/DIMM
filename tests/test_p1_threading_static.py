from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class P1ThreadingStaticTest(unittest.TestCase):
    def test_pulse_generator_serial_io_runs_on_worker_thread(self):
        header = read("src/PulseGeneratorManager.h")
        source = read("src/PulseGeneratorManager.cpp")

        self.assertIn("QThread* m_workerThread = nullptr", header)
        self.assertIn("QObject* m_workerContext = nullptr", header)
        self.assertIn("bool runWorkerOperation", header)
        self.assertIn("void ensureWorkerThread()", header)

        self.assertIn("setObjectName(QStringLiteral(\"pulseGeneratorWorker\"))", source)
        self.assertIn("m_workerContext->moveToThread(m_workerThread)", source)
        self.assertIn("QEventLoop loop", source)
        self.assertIn("QMetaObject::invokeMethod(m_workerContext", source)

        apply_body = source.split("bool PulseGeneratorManager::applyConfig", 1)[1].split(
            "bool PulseGeneratorManager::configureAndStart",
            1,
        )[0]
        start_body = source.split("bool PulseGeneratorManager::configureAndStart", 1)[1].split(
            "bool PulseGeneratorManager::stop",
            1,
        )[0]
        stop_body = source.split("bool PulseGeneratorManager::stop(QString* errorMessage)", 1)[1].split(
            "bool PulseGeneratorManager::isRunning",
            1,
        )[0]

        self.assertIn("runWorkerOperation", apply_body)
        self.assertIn("runWorkerOperation", start_body)
        self.assertIn("runWorkerOperation", stop_body)
        self.assertNotIn("configureDevice(config", apply_body)
        self.assertNotIn("configureDevice(config", start_body)
        self.assertNotIn("openPort(", stop_body)

    def test_polaris_detection_accepts_cancellation_token_in_long_loops(self):
        source = read("src/PolarisSolver.cpp")

        self.assertIn("detectStarsFromFrame(frame, config, cancelled)", source)
        self.assertIn("applyPolarisHotPixelCorrection(frame, config, cancelled)", source)

        correction_body = source.split("cv::Mat applyPolarisHotPixelCorrection", 1)[1].split(
            "QVector<PatternCatalogPoint> buildPatternCatalogPoints",
            1,
        )[0]
        detection_body = source.split("QVector<DetectedStar> detectStarsFromFrame", 1)[1].split(
            "PolarisSolveResult solveFrame",
            1,
        )[0]

        self.assertIn("const std::shared_ptr<std::atomic_bool>& cancelled", correction_body)
        self.assertIn("const std::shared_ptr<std::atomic_bool>& cancelled", detection_body)
        self.assertIn("isSolveCancelled(cancelled)", correction_body)
        self.assertIn("isSolveCancelled(cancelled)", detection_body)
        self.assertLess(
            detection_body.find("isSolveCancelled(cancelled)"),
            detection_body.find("cv::connectedComponentsWithStats"),
        )
        self.assertIn("for (int y = 0; y < labels.rows; ++y)", detection_body)
        self.assertIn("if (isSolveCancelled(cancelled))", detection_body)

    def test_polaris_controller_documents_single_worker_serial_mode(self):
        header = read("src/PolarisSolver.h")
        source = read("src/PolarisSolver.cpp")

        self.assertIn("static constexpr int kSolverWorkerThreadCount = 1", header)
        self.assertIn("polarisSolverSingleWorker", source)
        self.assertIn("m_workerThread(new QThread(this))", source)
        self.assertNotIn("m_workerThreads[", header)
        self.assertNotIn("m_workers[", header)

    def test_polaris_submit_path_uses_shallow_mat_handoff(self):
        source = read("src/PolarisSolver.cpp")
        controller_body = source.split("void PolarisSolverController::submitFrame", 1)[1].split(
            "void PolarisSolverController::cancelCurrentSolveTasks",
            1,
        )[0]

        self.assertIn("PendingSolveTask{true", controller_body)
        self.assertIn("cv::Mat frameForWorker = frame", controller_body)
        self.assertNotIn("frame.clone()", controller_body)


if __name__ == "__main__":
    unittest.main()

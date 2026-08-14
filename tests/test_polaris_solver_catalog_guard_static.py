from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverCatalogGuardStaticTest(unittest.TestCase):
    def test_solver_checks_embedded_catalog_before_reporting_waiting(self):
        cpp = read("src/PolarisSolver.cpp")

        self.assertIn("PolarisCatalog::isValid()", cpp)
        self.assertIn("PolarisSolveStatus::Error", cpp)
        self.assertIn("Embedded Polaris catalog is invalid", cpp)
        self.assertLess(cpp.find("PolarisCatalog::isValid()"),
                        cpp.find("cv::Mat frameForWorker = frame"))

    def test_solver_worker_path_stays_ui_free(self):
        cpp = read("src/PolarisSolver.cpp")

        self.assertIn("solveFrameWithProgress(frameForWorker, config, cancelled, progress)", cpp)
        self.assertNotIn("QWidget", cpp)
        self.assertNotIn("QInputDialog", cpp)


if __name__ == "__main__":
    unittest.main()

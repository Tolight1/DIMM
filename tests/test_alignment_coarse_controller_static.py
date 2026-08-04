from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class AlignmentCoarseControllerStaticTest(unittest.TestCase):
    def test_controller_is_threaded_latest_only_queue(self):
        self.assertTrue((ROOT / "src/AlignmentCoarseController.h").exists())
        self.assertTrue((ROOT / "src/AlignmentCoarseController.cpp").exists())
        cmake = read("CMakeLists.txt")
        header = read("src/AlignmentCoarseController.h")
        cpp = read("src/AlignmentCoarseController.cpp")

        self.assertIn("src/AlignmentCoarseController.h", cmake)
        self.assertIn("src/AlignmentCoarseController.cpp", cmake)
        self.assertIn("class AlignmentCoarseController", header)
        self.assertIn("QThread", header)
        self.assertIn("submitFrame", header)
        self.assertIn("estimateReady", header)
        self.assertIn("PendingFrame", cpp)
        self.assertIn("m_pendingLatest", cpp)
        self.assertIn("m_taskRunning", cpp)
        self.assertIn("startCoarseTask", cpp)

    def test_controller_uses_mono12_raw_and_explicit_detection_config(self):
        cpp = read("src/AlignmentCoarseController.cpp")
        self.assertIn("ImageUtils::grayscaleDetectionFrame", cpp)
        self.assertIn("detectInitialStarCandidates(grayscale, task.starConfig", cpp)
        self.assertNotIn("normalizeMono8Frame", cpp)
        self.assertNotIn("PolarisSolver", cpp)

if __name__ == "__main__":
    unittest.main()

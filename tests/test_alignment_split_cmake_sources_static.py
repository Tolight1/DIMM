from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentSplitCMakeSourcesStaticTest(unittest.TestCase):
    def test_cmake_explicitly_links_alignment_split_modules(self):
        cmake = read("CMakeLists.txt")

        self.assertIn("set(ALIGNMENT_SUPPORT_SOURCES", cmake)
        for source in [
            "src/AlignmentUiPresenter.cpp",
            "src/PolarisDetectionPipeline.cpp",
            "src/PolarisTracker.cpp",
        ]:
            self.assertIn(source, cmake)
        self.assertNotIn("src/AlignmentFlowCoordinator.h", cmake)
        self.assertNotIn("src/AlignmentFlowCoordinator.cpp", cmake)
        self.assertIn("${ALIGNMENT_SUPPORT_SOURCES}", cmake)


if __name__ == "__main__":
    unittest.main()

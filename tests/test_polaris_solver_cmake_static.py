from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverCMakeStaticTest(unittest.TestCase):
    def test_polaris_solver_modules_are_explicit_target_sources(self):
        cmake = read("CMakeLists.txt")

        self.assertIn("set(POLARIS_SOLVER_SOURCES", cmake)
        self.assertIn("src/PolarisSolver.cpp", cmake)
        self.assertIn("src/PolarisCatalog.cpp", cmake)
        self.assertIn("src/AstronomyTransform.cpp", cmake)
        self.assertIn("src/StarPatternMatcher.cpp", cmake)
        self.assertIn("${POLARIS_SOLVER_SOURCES}", cmake)


if __name__ == "__main__":
    unittest.main()

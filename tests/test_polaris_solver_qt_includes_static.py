from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverQtIncludesStaticTest(unittest.TestCase):
    def test_q_unused_is_not_included_as_a_header(self):
        cpp = read("src/PolarisSolver.cpp")

        self.assertNotIn("#include <Q_UNUSED>", cpp)


if __name__ == "__main__":
    unittest.main()

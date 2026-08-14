from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolveTimingStaticTest(unittest.TestCase):
    def test_solver_result_carries_timing_breakdown(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")

        self.assertIn("struct PolarisSolveTiming", header)
        self.assertIn("double detectionMs", header)
        self.assertIn("double matchingMs", header)
        self.assertIn("double totalMs", header)
        self.assertIn("PolarisSolveTiming timing", header)
        self.assertIn("#include <QElapsedTimer>", cpp)
        self.assertIn("detectionTimer.start()", cpp)
        self.assertIn("result.timing.detectionMs", cpp)
        self.assertIn("matchingTimer.start()", cpp)
        self.assertIn("result.timing.matchingMs", cpp)
        self.assertIn("result.timing.totalMs", cpp)

    def test_presenter_status_and_overlay_show_solve_timing(self):
        canvas_header = read("src/CanvasWidgets.h")
        canvas_cpp = read("src/CanvasWidgets.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")
        finished_body = alignment_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]

        self.assertIn("double solveTotalMs", canvas_header)
        self.assertIn("m_alignmentOverlay.solveTotalMs", canvas_cpp)
        self.assertIn("AlignmentUiPresenter::formatSolvedSolveLabel", finished_body)
        self.assertIn("耗时 %", presenter_cpp)
        self.assertIn("result.timing.totalMs", presenter_cpp)
        self.assertIn("overlay.solveTotalMs = solved.timing.totalMs", presenter_cpp)


if __name__ == "__main__":
    unittest.main()

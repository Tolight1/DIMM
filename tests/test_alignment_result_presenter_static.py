from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentResultPresenterStaticTest(unittest.TestCase):
    def test_presenter_owns_solver_finished_result_text(self):
        header = read("src/AlignmentUiPresenter.h")
        cpp = read("src/AlignmentUiPresenter.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        finished_body = alignment_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]

        expected_functions = (
            "formatPredictedOnlySolveLabel",
            "formatPredictedOnlyStatusMessage",
            "formatSolvedSolveLabel",
            "formatSolvedStatusMessage",
            "formatRetrySolveLabel",
            "formatRetryStatusMessage",
            "formatErrorSolveLabel",
            "formatErrorStatusMessage",
        )
        for function_name in expected_functions:
            self.assertIn(function_name, header)
            self.assertIn(f"AlignmentUiPresenter::{function_name}", cpp)
            self.assertIn(f"AlignmentUiPresenter::{function_name}", finished_body)

        self.assertIn("自动识别: 匹配成功", cpp)
        self.assertIn("自动识别: 未匹配", cpp)
        self.assertIn("自动识别: 错误", cpp)
        self.assertNotIn("QStringLiteral(\"自动识别: 匹配成功", finished_body)
        self.assertNotIn("QStringLiteral(\"自动识别: 未匹配", finished_body)
        self.assertNotIn("QStringLiteral(\"自动识别: 错误", finished_body)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentStatusPresenterStaticTest(unittest.TestCase):
    def test_presenter_owns_solver_status_label_text(self):
        header = read("src/AlignmentUiPresenter.h")
        cpp = read("src/AlignmentUiPresenter.cpp")
        dimm = read("src/DIMM.cpp")
        status_body = dimm.split("void DIMM::onPolarisSolveStatusChanged", 1)[1].split(
            "void DIMM::updateAlignmentOverlay", 1
        )[0]

        self.assertIn("formatManualConfirmedSolveLabel", header)
        self.assertIn("formatManualConfirmedStatusMessage", header)
        self.assertIn("formatMatchingSolveLabel", header)
        self.assertIn("formatMatchingStatusMessage", header)
        self.assertIn("AlignmentUiPresenter::formatManualConfirmedSolveLabel", status_body)
        self.assertIn("AlignmentUiPresenter::formatManualConfirmedStatusMessage", status_body)
        self.assertIn("AlignmentUiPresenter::formatMatchingSolveLabel", status_body)
        self.assertIn("AlignmentUiPresenter::formatMatchingStatusMessage", status_body)
        self.assertIn("自动识别: 人工确认", cpp)
        self.assertIn("自动识别: 匹配中", cpp)
        self.assertNotIn("QStringLiteral(\"自动识别: 人工确认", status_body)
        self.assertNotIn("QStringLiteral(\"自动识别: 匹配中", status_body)


if __name__ == "__main__":
    unittest.main()

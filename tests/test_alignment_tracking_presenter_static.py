from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentTrackingPresenterStaticTest(unittest.TestCase):
    def test_presenter_owns_frame_tracking_label_text(self):
        header = read("src/AlignmentUiPresenter.h")
        cpp = read("src/AlignmentUiPresenter.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]

        expected_functions = (
            "formatManualTrackingSolveLabel",
            "formatManualTrackingLostSolveLabel",
            "formatTrackingSolveLabel",
            "formatTrackingLostSolveLabel",
            "formatRetryWaitingSolveLabel",
        )
        for function_name in expected_functions:
            self.assertIn(function_name, header)
            self.assertIn(f"AlignmentUiPresenter::{function_name}", cpp)
            self.assertIn(f"AlignmentUiPresenter::{function_name}", packet_body)

        self.assertNotIn("QStringLiteral(\"自动识别: 人工确认 | 跟踪中", packet_body)
        self.assertNotIn("QStringLiteral(\"自动识别: 人工确认 | 跟踪暂失", packet_body)
        self.assertNotIn("QStringLiteral(\"自动识别: 跟踪中", packet_body)
        self.assertNotIn("QStringLiteral(\"自动识别: 跟踪暂失", packet_body)
        self.assertNotIn("QStringLiteral(\"自动识别: 等待重试", packet_body)


if __name__ == "__main__":
    unittest.main()

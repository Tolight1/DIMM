from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentStructureSplitStaticTest(unittest.TestCase):
    def test_alignment_runtime_types_live_outside_dimm(self):
        dimm_header = read("src/DIMM.h")
        types_header = read("src/AlignmentTypes.h")

        self.assertIn('#include "AlignmentTypes.h"', dimm_header)
        self.assertNotIn("enum class AlignmentSolveState", dimm_header)
        self.assertNotIn("struct AlignmentCameraSolveRuntime", dimm_header)
        self.assertIn("enum class AlignmentSolveState", types_header)
        self.assertIn("struct AlignmentCameraSolveRuntime", types_header)
        self.assertIn("PolarisSolveResult lastFullSolve", types_header)

    def test_alignment_ui_text_and_diagnostics_live_in_presenter(self):
        dimm_cpp = read("src/DIMM.cpp")
        presenter_header = read("src/AlignmentUiPresenter.h")
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")

        self.assertIn('#include "AlignmentUiPresenter.h"', dimm_cpp)
        self.assertIn("namespace AlignmentUiPresenter", presenter_header)
        self.assertIn("QString solveStateText(AlignmentSolveState state)", presenter_header)
        self.assertIn("QString polarisSolveStatusText(PolarisSolveStatus status)", presenter_header)
        self.assertIn("QString formatPolarisSolveLogLine(const PolarisSolveResult& result)", presenter_header)
        self.assertIn("QString AlignmentUiPresenter::solveStateText", presenter_cpp)
        self.assertIn("QString AlignmentUiPresenter::polarisSolveStatusText", presenter_cpp)
        self.assertIn("QString AlignmentUiPresenter::formatPolarisSolveLogLine", presenter_cpp)
        self.assertNotIn("QString DIMM::alignmentSolveStateText", dimm_cpp)
        self.assertNotIn("QString DIMM::polarisSolveStatusText", dimm_cpp)

    def test_dimm_delegates_alignment_text_and_logging_to_presenter(self):
        dimm_cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        log_body = dimm_cpp.split("void DIMM::logPolarisSolveResult", 1)[1].split(
            "void DIMM::onPolarisSolveStatusChanged", 1
        )[0]
        overlay_body = alignment_cpp.split("void DIMM::updateAlignmentOverlay", 1)[1]

        self.assertIn("AlignmentUiPresenter::formatPolarisSolveLogLine(result)", log_body)
        self.assertIn("AlignmentUiPresenter::buildAlignmentOverlay", overlay_body)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentOverlayPresenterStaticTest(unittest.TestCase):
    def test_presenter_declares_overlay_builder(self):
        header = read("src/AlignmentUiPresenter.h")
        cpp = read("src/AlignmentUiPresenter.cpp")

        self.assertIn('#include "CanvasWidgets.h"', header)
        self.assertIn("struct OverlayBuildInput", header)
        self.assertIn("QSize frameSize", header)
        self.assertIn("const PolarisSolveResult* solved", header)
        self.assertIn("FullFrameCanvas::AlignmentOverlay buildAlignmentOverlay", header)
        self.assertIn("FullFrameCanvas::AlignmentOverlay AlignmentUiPresenter::buildAlignmentOverlay", cpp)

    def test_presenter_builds_solved_overlay_details(self):
        cpp = read("src/AlignmentUiPresenter.cpp")
        body = cpp.split("FullFrameCanvas::AlignmentOverlay AlignmentUiPresenter::buildAlignmentOverlay", 1)[1]

        self.assertIn("overlay.orbitSource = QStringLiteral(\"理论回退\")", body)
        self.assertIn("overlay.orbitSource = QStringLiteral(\"解算\")", body)
        self.assertIn("overlay.solveStateText = solveStateText(input.solveState)", body)
        self.assertIn("overlay.warningText = solved.message", body)
        self.assertIn("overlay.catalogMatches.reserve(solved.matches.size())", body)
        self.assertIn("overlay.catalogMatches.push_back(matchOverlay)", body)
        self.assertIn("overlay.hasDetectedPolaris = true", body)
        self.assertIn("overlay.hasPredictedPolaris = true", body)
        self.assertIn("overlay.polarisNcpDistancePx", body)
        self.assertIn("overlay.polarisNcpDistanceArcmin", body)

    def test_dimm_delegates_base_overlay_building_to_presenter(self):
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        overlay_body = alignment_cpp.split("void DIMM::updateAlignmentOverlay", 1)[1]

        self.assertIn("AlignmentUiPresenter::OverlayBuildInput overlayInput", overlay_body)
        self.assertIn("AlignmentUiPresenter::buildAlignmentOverlay(overlayInput)", overlay_body)
        self.assertNotIn("overlay.catalogMatches.push_back(matchOverlay)", overlay_body)
        self.assertNotIn("overlay.orbitSource = QStringLiteral(\"解算\")", overlay_body)


if __name__ == "__main__":
    unittest.main()

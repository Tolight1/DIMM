from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisOverlayP2DetailsStaticTest(unittest.TestCase):
    def test_canvas_overlay_carries_alignment_detail_fields(self):
        header = read("src/CanvasWidgets.h")

        self.assertIn("double polarisNcpDistancePx", header)
        self.assertIn("double polarisNcpDistanceArcmin", header)
        self.assertIn("QString orbitSource", header)
        self.assertIn("QString solveStateText", header)
        self.assertIn("QString warningText", header)
        self.assertIn("bool mirroredKnown", header)
        self.assertIn("bool mirrored", header)

    def test_canvas_draws_residual_line_and_detail_text(self):
        cpp = read("src/CanvasWidgets.cpp")
        draw_body = cpp.split("void FullFrameCanvas::drawAlignmentOverlay", 1)[1].split(
            "void FullFrameCanvas::drawScaleBar", 1
        )[0]

        self.assertIn("drawLine(predicted, detected)", draw_body)
        self.assertIn("m_alignmentOverlay.polarisNcpDistancePx", draw_body)
        self.assertIn("m_alignmentOverlay.polarisNcpDistanceArcmin", draw_body)
        self.assertIn("m_alignmentOverlay.orbitSource", draw_body)
        self.assertIn("m_alignmentOverlay.solveStateText", draw_body)
        self.assertIn("m_alignmentOverlay.warningText", draw_body)
        self.assertIn("m_alignmentOverlay.mirroredKnown", draw_body)

    def test_presenter_populates_overlay_detail_fields(self):
        cpp = read("src/AlignmentUiPresenter.cpp")
        overlay_body = cpp.split(
            "FullFrameCanvas::AlignmentOverlay AlignmentUiPresenter::buildAlignmentOverlay", 1
        )[1]

        self.assertIn("overlay.orbitSource = QStringLiteral(\"理论回退\")", overlay_body)
        self.assertIn("overlay.orbitSource = QStringLiteral(\"解算\")", overlay_body)
        self.assertIn("overlay.solveStateText = solveStateText", overlay_body)
        self.assertIn("overlay.warningText = solved.message", overlay_body)
        self.assertIn("overlay.mirroredKnown = true", overlay_body)
        self.assertIn("overlay.mirrored = solved.mirrored", overlay_body)
        self.assertIn("overlay.polarisNcpDistancePx", overlay_body)
        self.assertIn("overlay.polarisNcpDistanceArcmin", overlay_body)


if __name__ == "__main__":
    unittest.main()

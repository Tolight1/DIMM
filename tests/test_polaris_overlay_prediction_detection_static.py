from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisOverlayPredictionDetectionStaticTest(unittest.TestCase):
    def test_canvas_overlay_has_separate_predicted_and_detected_polaris_markers(self):
        header = read("src/CanvasWidgets.h")

        self.assertIn("bool hasPredictedPolaris", header)
        self.assertIn("QPointF predictedPolarisPosition", header)
        self.assertIn("bool hasDetectedPolaris", header)
        self.assertIn("QPointF detectedPolarisPosition", header)

    def test_canvas_draws_predicted_detected_and_ncp_to_polaris_line(self):
        cpp = read("src/CanvasWidgets.cpp")
        draw_body = cpp.split("void FullFrameCanvas::drawAlignmentOverlay", 1)[1].split(
            "void FullFrameCanvas::drawScaleBar", 1
        )[0]

        self.assertIn("m_alignmentOverlay.hasPredictedPolaris", draw_body)
        self.assertIn("m_alignmentOverlay.hasDetectedPolaris", draw_body)
        self.assertIn("m_alignmentOverlay.predictedPolarisPosition", draw_body)
        self.assertIn("m_alignmentOverlay.detectedPolarisPosition", draw_body)
        self.assertIn("drawLine(center, polarisForLine)", draw_body)
        self.assertIn("Qt::DashLine", draw_body)
        self.assertIn("Qt::SolidLine", draw_body)

    def test_presenter_maps_solver_predicted_and_detected_polaris_into_overlay(self):
        cpp = read("src/AlignmentUiPresenter.cpp")
        overlay_body = cpp.split(
            "FullFrameCanvas::AlignmentOverlay AlignmentUiPresenter::buildAlignmentOverlay", 1
        )[1]

        self.assertIn("overlay.hasDetectedPolaris = true", overlay_body)
        self.assertIn("overlay.detectedPolarisPosition = solved.detectedPolarisPixel", overlay_body)
        self.assertIn("overlay.hasPredictedPolaris = true", overlay_body)
        self.assertIn("overlay.predictedPolarisPosition = solved.predictedPolarisPixel", overlay_body)


if __name__ == "__main__":
    unittest.main()

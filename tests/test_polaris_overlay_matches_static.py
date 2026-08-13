from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisOverlayMatchesStaticTest(unittest.TestCase):
    def test_canvas_overlay_supports_catalog_match_markers(self):
        header = read("src/CanvasWidgets.h")
        cpp = read("src/CanvasWidgets.cpp")

        self.assertIn("struct CatalogMatchOverlay", header)
        self.assertIn("QVector<CatalogMatchOverlay> catalogMatches", header)
        self.assertIn("matchedStarCount", header)
        self.assertIn("rmsPx", header)
        self.assertIn("plateScaleArcsecPx", header)
        self.assertIn("m_alignmentOverlay.catalogMatches", cpp)
        self.assertIn("drawLine(detected, predicted)", cpp)
        self.assertIn("匹配", cpp)

    def test_presenter_maps_solver_matches_into_alignment_overlay(self):
        cpp = read("src/AlignmentUiPresenter.cpp")
        overlay_body = cpp.split(
            "FullFrameCanvas::AlignmentOverlay AlignmentUiPresenter::buildAlignmentOverlay", 1
        )[1]

        self.assertIn("for (const CatalogImageMatch& match : solved.matches)", overlay_body)
        self.assertIn("FullFrameCanvas::CatalogMatchOverlay matchOverlay", overlay_body)
        self.assertIn("matchOverlay.detectedPosition = match.detectedPixel", overlay_body)
        self.assertIn("matchOverlay.predictedPosition = match.predictedPixel", overlay_body)
        self.assertIn("matchOverlay.residualPx = match.residualPx", overlay_body)
        self.assertIn("matchOverlay.isPolaris = match.isPolaris", overlay_body)
        self.assertIn("overlay.catalogMatches.push_back(matchOverlay)", overlay_body)


if __name__ == "__main__":
    unittest.main()

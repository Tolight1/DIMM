from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class AlignmentCoarseCanvasStaticTest(unittest.TestCase):
    def test_canvas_exposes_and_draws_coarse_overlay(self):
        header = read("src/CanvasWidgets.h")
        cpp = read("src/CanvasWidgets.cpp")

        self.assertIn("struct CoarseDriftTrackOverlay", header)
        self.assertIn("struct CoarseDriftOverlay", header)
        self.assertIn("setCoarseDriftOverlay", header)
        self.assertIn("clearCoarseDriftOverlay", header)
        self.assertIn("drawCoarseDriftOverlay", header)
        self.assertIn("m_coarseDriftOverlay", header)

        self.assertIn("FullFrameCanvas::setCoarseDriftOverlay", cpp)
        self.assertIn("FullFrameCanvas::clearCoarseDriftOverlay", cpp)
        self.assertIn("FullFrameCanvas::drawCoarseDriftOverlay", cpp)
        self.assertIn("drawCoarseDriftOverlay(painter)", cpp)
        self.assertIn("northCelestialPolePx", cpp)
        self.assertIn("adjustmentVectorPx", cpp)
        self.assertIn("drawLine", cpp)
        self.assertIn("drawEllipse", cpp)

if __name__ == "__main__":
    unittest.main()

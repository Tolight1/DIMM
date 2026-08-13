from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class CanvasMono12DisplayStaticTest(unittest.TestCase):
    def test_full_frame_canvas_normalizes_16bit_grayscale_before_qimage(self):
        source = read("src/CanvasWidgets.cpp")
        self.assertIn("cv::Mat makeDisplayGray8", source)
        helper_body = source.split("cv::Mat makeDisplayGray8", 1)[1].split(
            "FullFrameCanvas::FullFrameCanvas",
            1,
        )[0]
        draw_body = source.split("void FullFrameCanvas::drawImage", 1)[1].split(
            "void FullFrameCanvas::drawRoiOverlays",
            1,
        )[0]

        self.assertIn("image.convertTo(gray64, CV_64F)", helper_body)
        self.assertIn("cv::minMaxLoc(gray64, &minVal, &maxVal)", helper_body)
        self.assertIn("gray64.convertTo(display, CV_8U", helper_body)
        self.assertIn("const cv::Mat display = makeDisplayGray8(m_image)", draw_body)
        self.assertIn("QImage(display.data, display.cols, display.rows", draw_body)
        self.assertNotIn("QImage(m_image.data, m_image.cols, m_image.rows", draw_body)

    def test_roi_canvas_tooltip_can_report_16bit_pixel_values(self):
        source = read("src/CanvasWidgets.cpp")
        mouse_body = source.split("void RoiStarCanvas::mouseMoveEvent", 1)[1].split(
            "ChartWidget::ChartWidget",
            1,
        )[0]

        self.assertIn("m_roiImage.type() == CV_16U", mouse_body)
        self.assertIn("m_roiImage.at<quint16>(py, px)", mouse_body)


if __name__ == "__main__":
    unittest.main()

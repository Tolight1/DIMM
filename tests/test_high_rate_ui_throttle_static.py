from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class HighRateUiThrottleStaticTest(unittest.TestCase):
    def test_roi_image_publish_is_throttled_before_ui_signal(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")
        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor", 1
        )[0]

        self.assertIn("ROI_IMAGE_PUBLISH_INTERVAL_MS", header)
        self.assertIn("m_lastRoiImagePublishMs[2]", header)
        self.assertIn("emitRoiPreviewIfDue", source)
        self.assertNotIn("emitRoiImageIfDue", source)
        self.assertIn("nowMs - m_lastRoiImagePublishMs[cameraIndex]", source)
        self.assertIn("calculateCentroid(cameraIndex, roi, correctedRoiImage)", process_body)
        self.assertIn("emitRoiPreviewIfDue(cameraIndex, roiImage, roi, centroid, frameId, nowMs)",
                      process_body)

    def test_atmosphere_calculation_is_after_publish_interval_gate(self):
        source = read("src/ImageProcessor.cpp")
        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor", 1
        )[0]

        gate_pos = process_body.find("m_lastAtmospherePublishMs > 0")
        calc_pos = process_body.find("calculateAtmosphere")
        self.assertGreater(gate_pos, -1)
        self.assertGreater(calc_pos, -1)
        self.assertLess(gate_pos, calc_pos)


if __name__ == "__main__":
    unittest.main()

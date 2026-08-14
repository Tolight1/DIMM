from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class Mono12RiskAuditStaticTest(unittest.TestCase):
    def test_camera_frame_records_pixel_format_and_intensity_scale(self):
        header = read("src/CameraManager.h")
        source = read("src/CameraManager.cpp")
        callback_body = source.split("void CameraManager::onFrameCaptured", 1)[1].split(
            "void CameraManager::handleFrameNotification",
            1,
        )[0]

        self.assertIn("GX_PIXEL_FORMAT_ENTRY pixelFormat", header)
        self.assertIn("int bitDepth", header)
        self.assertIn("double maxPixelValue", header)
        self.assertIn("cameraFrameBitDepth(pixelFormat)", source)
        self.assertIn("cameraFrameMaxPixelValue(pixelFormat)", source)
        self.assertIn("packet.pixelFormat = pixelFormat", callback_body)
        self.assertIn("packet.bitDepth = cameraFrameBitDepth(pixelFormat)", callback_body)
        self.assertIn("packet.maxPixelValue = cameraFrameMaxPixelValue(pixelFormat)", callback_body)

    def test_roi_centroid_config_remains_in_sensor_intensity_units(self):
        source = read("src/ImageProcessor.cpp")
        setter_body = source.split("void ImageProcessorWorker::setRoiCentroidConfig", 1)[1].split(
            "void ImageProcessorWorker::configureHotPixelTemplates",
            1,
        )[0]
        cog_body = source.split("CentroidResult ImageProcessorWorker::centerOfGravity", 1)[1].split(
            "CentroidResult ImageProcessorWorker::gaussianFit",
            1,
        )[0]

        self.assertIn("m_roiThresholdAbsolute = thresholdAbsolute >= 0.0 ? thresholdAbsolute : -1.0", setter_body)
        self.assertIn("std::lround(std::max(0.0, minimumIntensity))", setter_body)
        self.assertNotIn("normalizeThresholdToMono8", setter_body)
        self.assertIn("std::max(static_cast<double>(centroidMinimumIntensity)", cog_body)


if __name__ == "__main__":
    unittest.main()

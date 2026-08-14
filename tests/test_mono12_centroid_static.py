from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class Mono12CentroidStaticTest(unittest.TestCase):
    def test_camera_prefers_unpacked_mono12_and_keeps_it_as_16bit_mat(self):
        source = read("src/CameraManager.cpp")
        callback_body = source.split("void CameraManager::onFrameCaptured", 1)[1].split(
            "void CameraManager::handleFrameNotification",
            1,
        )[0]

        self.assertIn('setEnumIfWritable(camera.remoteFeatureControl, "PixelFormat", "Mono12")', source)
        self.assertIn("is16BitMonoPixelFormat(pixelFormat)", callback_body)
        self.assertIn("GX_PIXEL_FORMAT_MONO12", source)
        self.assertIn("CV_16UC1", callback_body)
        self.assertIn("width * height * sizeof(uint16_t)", callback_body)
        self.assertLess(callback_body.find("is16BitMonoPixelFormat(pixelFormat)"),
                        callback_body.find("ConvertToRaw8"))

    def test_roi_centroid_paths_read_source_bit_depth_without_mono8_downconversion(self):
        source = read("src/ImageProcessor.cpp")
        cog_body = source.split("CentroidResult ImageProcessorWorker::centerOfGravity", 1)[1].split(
            "CentroidResult ImageProcessorWorker::gaussianFit",
            1,
        )[0]
        edge_body = source.split("bool ImageProcessorWorker::hasThresholdSignalNearRoiEdge", 1)[1].split(
            "CentroidResult ImageProcessorWorker::centerOfGravity",
            1,
        )[0]
        gaussian_body = source.split("CentroidResult ImageProcessorWorker::gaussianFit", 1)[1].split(
            "AtmosphericParams ImageProcessorWorker::calculateAtmosphere",
            1,
        )[0]

        self.assertIn("makeCentroidIntensityImage(image)", cog_body)
        self.assertIn("filteredIntensity", cog_body)
        self.assertNotIn("convertTo(mono8, CV_8UC1)", cog_body)
        self.assertNotIn("ptr<uchar>", cog_body)
        self.assertNotIn("at<uchar>", edge_body)
        self.assertNotIn("ptr<uchar>", gaussian_body)
        self.assertIn("pixelValueAt(image, y, x)", gaussian_body)

    def test_hot_pixel_correction_preserves_16bit_roi_images(self):
        source = read("src/ImageProcessor.cpp")
        body = source.split("cv::Mat ImageProcessorWorker::applyHotPixelCorrection", 1)[1].split(
            "CentroidResult ImageProcessorWorker::calculateCentroid",
            1,
        )[0]

        self.assertIn("roiImage.type() == CV_16UC1", body)
        self.assertIn("quint16* row = corrected.ptr<quint16>(y)", body)
        self.assertIn("cache.excess[index]", body)
        self.assertIn("std::numeric_limits<quint16>::max()", body)


if __name__ == "__main__":
    unittest.main()

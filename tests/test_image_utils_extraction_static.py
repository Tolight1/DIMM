from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ImageUtilsExtractionStaticTest(unittest.TestCase):
    def test_mono8_helpers_are_extracted_to_image_utils(self):
        header = read("src/ImageUtils.h")
        cpp = read("src/ImageUtils.cpp")

        self.assertIn("#pragma once", header)
        self.assertIn("namespace ImageUtils", header)
        self.assertIn("double normalizeThresholdToMono8(double value)", header)
        self.assertIn("cv::Mat grayscaleDetectionFrame(const cv::Mat& frame)", header)
        self.assertIn("cv::Mat normalizeMono8Frame(const cv::Mat& grayscale)", header)
        self.assertIn("cv::Mat normalizeDetectionFrame(const cv::Mat& frame)", header)

        self.assertIn("value > 255.0 && value <= 4095.0", cpp)
        self.assertIn("value * 255.0 / 4095.0", cpp)
        self.assertIn("value * 255.0 / 65535.0", cpp)
        self.assertIn("cv::COLOR_BGR2GRAY", cpp)
        self.assertIn("cv::COLOR_BGRA2GRAY", cpp)
        self.assertIn("cv::minMaxLoc(grayscale, &minValue, &maxValue)", cpp)
        self.assertIn("const double scale = 255.0 / (maxValue - minValue)", cpp)

    def test_callers_use_shared_mono8_helpers_without_local_duplicates(self):
        dimm = read("src/DIMM.cpp")
        star_config = read("src/InitialStarDetectionConfig.cpp")
        detector = read("src/FullFrameStarDetector.cpp")
        processor = read("src/ImageProcessor.cpp")
        solver = read("src/PolarisSolver.cpp")

        for source in (dimm, processor, solver):
            self.assertIn('#include "ImageUtils.h"', source)

        self.assertNotIn("double normalizeThresholdToMono8(double value)", dimm)
        self.assertNotIn("double normalizeThresholdToMono8(double value)", processor)
        self.assertNotIn("ImageUtils::normalizeThresholdToMono8(number)", star_config)
        self.assertNotIn("cv::Mat normalizeDetectionFrame(const cv::Mat& frame)", solver)
        self.assertNotIn("cv::Mat grayscaleDetectionFrame(const cv::Mat& frame)", solver)

        self.assertIn("ImageUtils::normalizeThresholdToMono8(config.thresholdAbsolute)", detector)
        self.assertIn("ImageUtils::normalizeMono8Frame(grayscale)", detector)
        self.assertNotIn("ImageUtils::normalizeThresholdToMono8(thresholdAbsolute)", processor)
        self.assertIn("ImageUtils::grayscaleDetectionFrame(frame)", solver)
        self.assertIn("ImageUtils::grayscaleDetectionFrame(correctedFrame)", solver)
        self.assertNotIn("ImageUtils::normalizeDetectionFrame(grayscale)", solver)

    def test_protected_roi_and_capture_functions_stay_in_dimm_and_processor(self):
        comm_camera = read("src/DIMM.CommCamera.cpp")
        live = read("src/DIMM.LiveRoi.cpp")
        processor = read("src/ImageProcessor.cpp")

        for signature in (
            "void DIMM::updateMinuteRoi",
            "void DIMM::requestLiveFullFrameRelocalization",
            "bool DIMM::applyLiveHardwareRois",
            "bool DIMM::configureLiveCameras",
            "bool DIMM::startHardwarePulseStage",
            "bool DIMM::switchToRoiTrackingPulse",
        ):
            self.assertIn(signature, live)

        self.assertIn("void DIMM::onCapturedFramePacket", comm_camera)

        self.assertIn("bool ImageProcessorWorker::appendDifferentialSample", processor)
        self.assertIn("void ImageProcessorWorker::processFrame", processor)

    def test_cmake_explicitly_registers_image_utils_sources(self):
        cmake = read("CMakeLists.txt")

        self.assertIn("src/ImageUtils.h", cmake)
        self.assertIn("src/ImageUtils.cpp", cmake)


if __name__ == "__main__":
    unittest.main()

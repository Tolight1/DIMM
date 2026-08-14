from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class RoiCentroidPrecisionStaticTest(unittest.TestCase):
    def test_roi_centroid_defaults_match_reference_stability(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")

        self.assertIn("int m_centroidMinimumSignalPixels = 3;", header)
        self.assertIn("int minimumSignalPixels = 3;", source)
        self.assertIn("int centroidMinimumSignalPixels = 3;", source)

    def test_center_of_gravity_applies_gaussian_prefilter_before_noise_threshold(self):
        source = read("src/ImageProcessor.cpp")
        body = source.split("CentroidResult ImageProcessorWorker::centerOfGravity", 1)[1].split(
            "CentroidResult ImageProcessorWorker::gaussianFit", 1
        )[0]

        blur_pos = body.find("cv::GaussianBlur(")
        sort_pos = body.find("std::sort(sortedValues.begin(), sortedValues.end())")
        threshold_pos = body.find("result.threshold =")
        self.assertGreaterEqual(blur_pos, 0)
        self.assertGreater(sort_pos, blur_pos)
        self.assertGreater(threshold_pos, blur_pos)
        self.assertIn("filteredIntensity", body)

    def test_image_processing_ui_describes_roi_centroid_pipeline(self):
        source = read("src/SettingsDialog.cpp")

        self.assertIn("高斯加权精细化", source)
        self.assertIn("ROI质心预处理参数", source)
        self.assertIn("ROI质心流程: 热像素修正 -> 高斯滤波 -> 噪声阈值 -> 背景扣除重心。", source)
        self.assertNotIn("new QRadioButton(QStringLiteral(\"高斯拟合\"))", source)

    def test_gaussian_refinement_is_default_centroid_method(self):
        header = read("src/ImageProcessor.h")
        source = read("src/SettingsDialog.cpp")

        self.assertIn("int m_method = 1;", header)
        self.assertIn("procGaussian->setChecked(true);", source)
        self.assertNotIn("procGravity->setChecked(true);", source)

    def test_only_centroid_validity_gates_enqueue_for_pairing(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")
        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor",
            1,
        )[0]

        # 参数计算前的质心二次质量筛选已删除：只有 centroid.valid 决定是否进入配对队列。
        self.assertNotIn("isMeasurementUsableCentroid", header)
        self.assertNotIn("isMeasurementUsableCentroid", source)
        self.assertNotIn("measurementCentroidQuality", header)
        self.assertNotIn("measurementCentroidQuality", source)
        self.assertNotIn("if (!measurementUsable)", process_body)
        self.assertNotIn("emitRoiImageIfDue", process_body)
        self.assertIn("if (centroid.valid) {", process_body)
        self.assertIn("m_pendingCentroids[cameraIndex].append(pending);", process_body)
        self.assertLess(process_body.find("if (centroid.valid) {"),
                        process_body.find("m_pendingCentroids[cameraIndex].append"))
        self.assertIn("calculateCentroid(cameraIndex, roi, correctedRoiImage)", process_body)

    def test_edge_quality_filter_no_longer_gates_parameter_calculation(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")
        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor",
            1,
        )[0]

        # hasThresholdSignalNearRoiEdge 保留实现但不参与参数计算链路。
        self.assertIn("bool hasThresholdSignalNearRoiEdge", header)
        self.assertNotIn("hasThresholdSignalNearRoiEdge(roiImage, centroid.threshold)", process_body)
        edge_body = source.split("bool ImageProcessorWorker::hasThresholdSignalNearRoiEdge", 1)[1].split(
            "CentroidResult ImageProcessorWorker::centerOfGravity",
            1,
        )[0]
        self.assertIn("edgeBand", edge_body)
        self.assertIn("pixelValueAt(roiImage, y, x)", edge_body)


if __name__ == "__main__":
    unittest.main()

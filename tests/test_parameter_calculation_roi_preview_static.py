from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ParameterCalculationConfigStaticTest(unittest.TestCase):
    def test_config_struct_has_default_sample_frame_count(self):
        header = read("src/AppConfig.h")

        self.assertIn("struct ParameterCalculationConfig", header)
        self.assertIn("int sampleFrameCount = 12000;", header)
        self.assertIn("ParameterCalculationConfig parameterCalculation;", header)

    def test_persistence_uses_sample_frame_count_key_only(self):
        source = read("src/AppConfigPersistence.cpp")

        self.assertIn('QStringLiteral("parameterCalculation/sampleFrameCount")', source)
        self.assertNotIn("parameterCalculation/sampleWindowSec", source)
        self.assertNotIn("parameterCalculation/enablePeakAboveThreshold", source)

    def test_settings_dialog_wires_sample_frame_count(self):
        dialog_h = read("src/SettingsDialog.h")
        dialog_cpp = read("src/SettingsDialog.cpp")

        self.assertIn("onApplyParameterCalculation", dialog_h)
        self.assertIn("QLineEdit* parameterSampleFrameCountEdit = nullptr;", dialog_h)
        self.assertIn('QStringLiteral("参数计算")', dialog_cpp)
        self.assertIn('QStringLiteral("数据采样帧数:")', dialog_cpp)
        self.assertIn("parameterCalculationConfig.sampleFrameCount", dialog_cpp)
        self.assertIn('QStringLiteral("数据采样帧数必须在 2 到 200000 之间。")', dialog_cpp)

    def test_apply_controller_wires_parameter_calculation(self):
        ctrl_h = read("src/ConfigApplicationController.h")
        ctrl_cpp = read("src/ConfigApplicationController.cpp")

        self.assertIn("applyParameterCalculation", ctrl_h)
        self.assertIn("callbacks.applyParameterCalculation(config.parameterCalculation)", ctrl_cpp)

    def test_dimm_wires_save_load_apply(self):
        config_cpp = read("src/DIMM.Config.cpp")
        dimm_h = read("src/DIMM.h")

        self.assertIn("setupParameterCalculationSettingsCallbacks", dimm_h)
        self.assertIn("m_parameterCalculationConfig", dimm_h)
        self.assertIn("config.parameterCalculation = m_parameterCalculationConfig;", config_cpp)
        self.assertIn("m_parameterCalculationConfig = config.parameterCalculation;", config_cpp)
        self.assertIn("m_imageProcessor->setParameterCalculationConfig(m_parameterCalculationConfig);",
                      config_cpp)
        self.assertIn("parameterSampleFrameCountEdit", config_cpp)


class RoiSameFramePreviewStaticTest(unittest.TestCase):
    def test_roi_preview_result_struct_carries_frame_state(self):
        header = read("src/ImageProcessor.h")

        self.assertIn("struct RoiPreviewResult", header)
        self.assertIn("quint64 frameId = 0;", header)
        self.assertIn("RoiRect roi;", header)
        self.assertIn("cv::Mat image;", header)
        self.assertIn("bool centroidValid = false;", header)
        self.assertIn("double localX = 0.0;", header)
        self.assertIn("double absoluteX = 0.0;", header)
        self.assertIn("Q_DECLARE_METATYPE(RoiPreviewResult)", header)

    def test_roi_image_ready_signal_replaced(self):
        source = read("src/ImageProcessor.cpp")
        header = read("src/ImageProcessor.h")

        self.assertNotIn("roiImageReady", header)
        self.assertNotIn("roiImageReady", source)
        self.assertNotIn("emitRoiImageIfDue", header)
        self.assertNotIn("emitRoiImageIfDue", source)
        self.assertIn("void roiPreviewReady(RoiPreviewResult preview);", header)
        self.assertIn("roiPreviewReady(preview);", source)
        self.assertIn("ROI_IMAGE_PUBLISH_INTERVAL_MS = 100", header)

    def test_preview_publishes_absolute_coordinates(self):
        source = read("src/ImageProcessor.cpp")
        body = source.split("void ImageProcessorWorker::emitRoiPreviewIfDue", 1)[1].split(
            "void ImageProcessorWorker::processFrame", 1
        )[0]

        self.assertIn("preview.centroidValid = centroid.valid;", body)
        self.assertIn("preview.localX = centroid.x;", body)
        self.assertIn("preview.absoluteX = centroid.x + static_cast<double>(roi.x);", body)
        self.assertIn("ROI_IMAGE_PUBLISH_INTERVAL_MS", body)

    def test_no_quality_filter_blocks_pending_queue(self):
        source = read("src/ImageProcessor.cpp")
        header = read("src/ImageProcessor.h")

        self.assertNotIn("measurementCentroidQuality", header)
        self.assertNotIn("measurementCentroidQuality", source)
        self.assertNotIn("isMeasurementUsableCentroid", header)
        self.assertNotIn("isMeasurementUsableCentroid", source)

        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor", 1
        )[0]
        self.assertIn("if (centroid.valid) {", process_body)
        self.assertIn("m_pendingCentroids[cameraIndex].append(pending);", process_body)
        self.assertLess(
            process_body.find("if (centroid.valid) {"),
            process_body.find("m_pendingCentroids[cameraIndex].append(pending);"),
        )
        self.assertNotIn("!measurementUsable", process_body)

    def test_auto_exposure_independent_quality_kept(self):
        source = read("src/ImageProcessor.cpp")
        self.assertIn("autoExposureMeasurementUsable", source)
        self.assertIn("emit autoExposureSampleReady(", source)

    def test_dimm_ui_uses_same_frame_preview(self):
        dimm = read("src/DIMM.cpp")

        roi_body = dimm.split("void DIMM::setupRoiImageProcessorConnection()", 1)[1].split(
            "void DIMM::setupAtmosphereProcessorConnection()", 1
        )[0]

        self.assertIn("&ImageProcessor::roiPreviewReady", roi_body)
        self.assertIn("preview.image", roi_body)
        self.assertIn("preview.localX", roi_body)
        self.assertIn("preview.localY", roi_body)
        self.assertIn("preview.absoluteX", roi_body)
        self.assertIn("canvas->setCentroid(preview.localX, preview.localY);", roi_body)
        self.assertIn("canvas->clearCentroid();", roi_body)
        self.assertNotIn("runtime.centroidX", roi_body)
        self.assertNotIn("getCurrentRoi", roi_body)


if __name__ == "__main__":
    unittest.main()

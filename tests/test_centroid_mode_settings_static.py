from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class CentroidModeSettingsStaticTest(unittest.TestCase):
    def test_image_processor_preserves_selected_centroid_mode(self):
        source = read("src/ImageProcessor.cpp")

        self.assertIn("void ImageProcessorWorker::setCentroidMethod(int method)", source)
        self.assertIn("void ImageProcessor::setCentroidMethod(int method)", source)
        self.assertNotIn("setCentroidMode(method == 1 ? 0 : method)", source)
        self.assertIn("setCentroidMode(method);", source)

    def test_settings_dialog_applies_centroid_mode_and_mode_specific_parameters(self):
        settings_cpp = read("src/SettingsDialog.cpp")
        dimm_config_cpp = read("src/DIMM.Config.cpp")
        controller_cpp = read("src/ConfigApplicationController.cpp")

        apply_body = settings_cpp.split("bool SettingsDialog::applySettings()", 1)[1].split(
            "const RoiRecenteringConfig roiRecenteringConfig",
            1,
        )[0]
        self.assertIn("const int centroidMode = centroidModeCombo ? centroidModeCombo->currentData().toInt() : 0", apply_body)
        self.assertIn("const int peakKernelMethod = peakKernelMethodCombo ? peakKernelMethodCombo->currentData().toInt() : 1", apply_body)
        self.assertIn("peakKernelRadiusEdit ? peakKernelRadiusEdit->text().toInt(&ok) : 3", apply_body)
        self.assertIn("strongHotPixelExcessEdit ? strongHotPixelExcessEdit->text().toDouble(&ok) : 100.0", apply_body)
        self.assertIn("const ProcessingConfig processingConfig", apply_body)
        self.assertIn("centroidMode,", apply_body)
        self.assertIn("peakKernelMethod,", apply_body)
        self.assertIn("peakKernelRadius,", apply_body)
        self.assertIn("strongHotPixelExcess", apply_body)

        self.assertIn("callbacks.applyProcessing(config.processing.backgroundKernelSize", controller_cpp)
        self.assertIn("config.processing.centroidMode", controller_cpp)
        self.assertIn("config.processing.peakKernelMethod", controller_cpp)
        self.assertIn("m_imageProcessor->setCentroidMode(centroidMode)", dimm_config_cpp)
        self.assertIn("m_imageProcessor->setPeakKernelCentroidConfig(peakKernelMethod", dimm_config_cpp)


if __name__ == "__main__":
    unittest.main()

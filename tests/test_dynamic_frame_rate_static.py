from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class DynamicFrameRateStaticTest(unittest.TestCase):
    def test_image_processor_exposes_target_frame_rate(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")

        self.assertIn("void setTargetFrameRateHz(double frameRateHz);", header)
        self.assertIn("ImageProcessorWorker::setTargetFrameRateHz", source)
        self.assertIn("m_targetFrameRateHz", header)

    def test_history_window_uses_configured_frame_rate_not_fixed_200(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")

        self.assertNotIn("MAX_HISTORY = 200", header)
        self.assertNotIn("MIN_ATMOSPHERE_HISTORY = 200", header)
        self.assertIn("historyWindowSize()", source)
        self.assertIn("minimumAtmosphereSamples()", source)

    def test_dimm_applies_pulse_frequency_to_processing_window(self):
        source = read("src/DIMM.cpp")

        self.assertIn("m_imageProcessor->setTargetFrameRateHz(m_pulseGeneratorFrequencyHz)", source)

    def test_left_statistics_are_compact(self):
        ui_cpp = read("src/DIMM.Ui.cpp")
        body = ui_cpp.split("void DIMM::refreshMeasurementUi()", 1)[1].split(
            "void DIMM::refreshPanelUi()", 1
        )[0]

        # The raw and in-processing frame counts share one compact label.
        frames_text = body.split("ui->lblStatFrames->setText", 1)[1].split(
            "ui->lblStatValid->setText", 1
        )[0]
        self.assertIn("runtime.frameCount", frames_text)
        self.assertIn("runtime.processedFrameCount", frames_text)

        # The remaining left-panel statistics stay on their own compact lines.
        self.assertIn("ui->lblStatValid->setText", body)
        self.assertIn("runtime.validCentroidCount", body)
        self.assertIn("runtime.pairedSampleCount", body)
        self.assertIn("ui->lblStatLatency->setText", body)
        self.assertIn("ui->lblStatWindow->setText", body)
        self.assertIn("runtime.averageSyncJitterUs", body)

        # Legacy verbose per-camera statistics text is gone.
        self.assertNotIn("相机1:%2 / 相机2:%3", body)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ParameterCalculationWindowStaticTest(unittest.TestCase):
    def test_header_removes_seconds_window_and_raises_max_history(self):
        header = read("src/ImageProcessor.h")

        self.assertNotIn("ATMOSPHERE_HISTORY_WINDOW_SECONDS", header)
        self.assertNotIn("static constexpr double ATMOSPHERE_HISTORY_WINDOW_SECONDS = 60.0", header)
        self.assertIn("static constexpr int MAX_HISTORY_WINDOW = 200000", header)
        self.assertIn("static constexpr int MAX_PENDING_PAIR_QUEUE = 1000", header)
        self.assertIn("int historyWindowSize() const", header)
        self.assertIn("int minimumAtmosphereSamples() const", header)
        self.assertIn("int pendingCentroidQueueLimit() const", header)

    def test_history_window_is_sample_frame_count_not_seconds(self):
        source = read("src/ImageProcessor.cpp")
        history_body = source.split("int ImageProcessorWorker::historyWindowSize() const", 1)[1].split(
            "int ImageProcessorWorker::minimumAtmosphereSamples() const", 1
        )[0]
        minimum_body = source.split("int ImageProcessorWorker::minimumAtmosphereSamples() const", 1)[1].split(
            "int ImageProcessorWorker::pendingCentroidQueueLimit() const", 1
        )[0]

        self.assertIn("m_parameterCalculationConfig.sampleFrameCount", history_body)
        self.assertIn("MAX_HISTORY_WINDOW", history_body)
        self.assertNotIn("m_targetFrameRateHz * ATMOSPHERE_HISTORY_WINDOW_SECONDS", history_body)
        self.assertNotIn("ATMOSPHERE_HISTORY_WINDOW_SECONDS", source)
        self.assertIn("return historyWindowSize()", minimum_body)

    def test_atmosphere_publish_interval_stays_one_second(self):
        header = read("src/ImageProcessor.h")
        self.assertIn("ATMOSPHERE_PUBLISH_INTERVAL_MS = 1000", header)

    def test_pending_pair_queue_keeps_short_frame_rate_limit(self):
        source = read("src/ImageProcessor.cpp")
        pending_body = source.split("int ImageProcessorWorker::pendingCentroidQueueLimit() const", 1)[1].split(
            "void ImageProcessorWorker::resetRoiProcessingHistory()", 1
        )[0]
        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor", 1
        )[0]

        self.assertIn("std::lround(m_targetFrameRateHz)", pending_body)
        self.assertIn("MAX_PENDING_PAIR_QUEUE", pending_body)
        self.assertIn("while (m_pendingCentroids[cameraIndex].size() > pendingCentroidQueueLimit())",
                      process_body)
        self.assertNotIn("while (m_pendingCentroids[cameraIndex].size() > historyWindowSize())",
                         process_body)

    def test_history_trimmed_by_sample_frame_count(self):
        source = read("src/ImageProcessor.cpp")
        append_body = source.split("bool ImageProcessorWorker::appendDifferentialSample()", 1)[1].split(
            "void ImageProcessorWorker::emitRoiPreviewIfDue", 1
        )[0]

        self.assertIn("while (m_differentialHistory.size() > historyWindowSize())", append_body)
        self.assertIn("m_differentialHistory.removeFirst()", append_body)

    def test_set_parameter_calculation_config_resets_history(self):
        source = read("src/ImageProcessor.cpp")
        body = source.split("void ImageProcessorWorker::setParameterCalculationConfig", 1)[1].split(
            "int ImageProcessorWorker::historyWindowSize", 1
        )[0]

        self.assertIn("std::clamp(config.sampleFrameCount, 2, MAX_HISTORY_WINDOW)", body)
        self.assertIn("resetRoiProcessingHistory()", body)
        self.assertIn("QMutexLocker", body)


if __name__ == "__main__":
    unittest.main()

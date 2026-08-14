from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class FrameAlignedPairingStaticTest(unittest.TestCase):
    def test_pending_samples_keep_raw_frame_id_for_pairing(self):
        processor_h = read("src/ImageProcessor.h")
        processor_cpp = read("src/ImageProcessor.cpp")

        self.assertIn("quint64 frameId = 0", processor_h)
        self.assertIn("pending.frameId = frameId", processor_cpp)

    def test_worker_establishes_fixed_raw_frame_id_offset(self):
        processor_h = read("src/ImageProcessor.h")
        processor_cpp = read("src/ImageProcessor.cpp")

        self.assertIn("bool m_syncCalibrated = false", processor_h)
        self.assertIn("quint64 m_firstRawFrameId[2] = {0, 0}", processor_h)
        self.assertIn("qint64 m_frameIdOffset = 0", processor_h)
        self.assertIn("m_firstRawFrameId[cameraIndex] = frameId", processor_cpp)
        self.assertIn("m_frameIdOffset =", processor_cpp)
        self.assertIn("static_cast<qint64>(m_firstRawFrameId[1])", processor_cpp)
        self.assertIn("static_cast<qint64>(m_firstRawFrameId[0])", processor_cpp)
        self.assertIn("m_syncCalibrated = true", processor_cpp)

    def test_worker_establishes_timestamp_offset_for_residual_diagnostics(self):
        processor_h = read("src/ImageProcessor.h")
        processor_cpp = read("src/ImageProcessor.cpp")

        self.assertIn("bool m_timestampOffsetCalibrated = false", processor_h)
        self.assertIn("quint64 m_firstRawTimestamp[2] = {0, 0}", processor_h)
        self.assertIn("long double m_timestampOffsetTicks = 0.0L", processor_h)
        self.assertIn("static constexpr double MARS_GIGE_TIMESTAMP_TICK_US = 0.008", processor_h)
        self.assertIn("m_firstRawTimestamp[cameraIndex] = cameraTimestamp", processor_cpp)
        self.assertIn("m_timestampOffsetTicks =", processor_cpp)
        self.assertIn("m_timestampOffsetCalibrated = true", processor_cpp)

    def test_append_differential_sample_drops_unmatched_front_samples(self):
        processor_cpp = read("src/ImageProcessor.cpp")
        append_body = processor_cpp.split("bool ImageProcessorWorker::appendDifferentialSample()", 1)[1].split(
            "void ImageProcessorWorker::emitRoiPreviewIfDue", 1
        )[0]

        self.assertIn("if (!m_syncCalibrated)", append_body)
        self.assertIn("return false", append_body)
        self.assertIn("while (!m_pendingCentroids[0].isEmpty() && !m_pendingCentroids[1].isEmpty())", append_body)
        self.assertIn("const qint64 alignedFrameId0", append_body)
        self.assertIn("const qint64 alignedFrameId1", append_body)
        self.assertIn("- m_frameIdOffset", append_body)
        self.assertIn("alignedFrameId0 < alignedFrameId1", append_body)
        self.assertIn("alignedFrameId1 < alignedFrameId0", append_body)
        self.assertIn("m_pendingCentroids[0].removeFirst()", append_body)
        self.assertIn("m_pendingCentroids[1].removeFirst()", append_body)
        self.assertGreaterEqual(append_body.count("++m_droppedUnpairedSamples"), 2)
        self.assertNotIn("FIFO pairing", append_body)

    def test_fixed_offset_replaces_normalized_pairing(self):
        processor_cpp = read("src/ImageProcessor.cpp")
        append_body = processor_cpp.split("bool ImageProcessorWorker::appendDifferentialSample()", 1)[1].split(
            "void ImageProcessorWorker::emitRoiPreviewIfDue", 1
        )[0]

        self.assertNotIn("front0.normalizedFrameId", append_body)
        self.assertNotIn("front1.normalizedFrameId", append_body)

    def test_sync_delta_is_emitted_only_after_an_aligned_pair_is_taken(self):
        processor_cpp = read("src/ImageProcessor.cpp")
        append_body = processor_cpp.split("bool ImageProcessorWorker::appendDifferentialSample()", 1)[1].split(
            "void ImageProcessorWorker::emitRoiPreviewIfDue", 1
        )[0]

        take_pos = append_body.index("const PendingCentroidSample cam0 = m_pendingCentroids[0].takeFirst()")
        sync_pos = append_body.index("emit syncSampleReady")
        self.assertLess(take_pos, sync_pos)
        self.assertIn("residualTicks", append_body)
        self.assertIn("* MARS_GIGE_TIMESTAMP_TICK_US", append_body)
        self.assertNotIn("/ 1000.0", append_body)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class RoiUpdatePreservesRunCountersStaticTest(unittest.TestCase):
    def test_roi_update_does_not_reset_cumulative_pair_counters(self):
        source = read("src/ImageProcessor.cpp")
        self.assertIn("void ImageProcessorWorker::resetRoiProcessingHistory()", source)
        roi_reset_body = source.split("void ImageProcessorWorker::resetRoiProcessingHistory()", 1)[1].split(
            "\n}", 1
        )[0]

        self.assertNotIn("m_lastPairedSerial = 0", roi_reset_body)
        self.assertNotIn("m_droppedUnpairedSamples = 0", roi_reset_body)

        set_roi_body = source.split("void ImageProcessorWorker::setCurrentRoi", 1)[1].split(
            "cv::Mat ImageProcessorWorker::preprocess", 1
        )[0]
        self.assertIn("resetRoiProcessingHistory();", set_roi_body)
        self.assertNotIn("resetAtmosphereHistory();", set_roi_body)

    def test_explicit_run_reset_still_resets_cumulative_pair_counters(self):
        source = read("src/ImageProcessor.cpp")
        match = re.search(
            r"void ImageProcessorWorker::resetRunProcessingState\(\)\s*\{(?P<body>.*?)\n\}",
            source,
            re.S,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("m_lastPairedSerial = 0", body)
        self.assertIn("m_droppedUnpairedSamples = 0", body)

        dimm = read("src/DIMM.cpp")
        reset_body = dimm.split("void DIMM::resetMeasurementState()", 1)[1].split(
            "void DIMM::closeResultFile()", 1
        )[0]
        self.assertIn("m_imageProcessor->resetProcessingState();", reset_body)

    def test_pending_centroid_overflow_increments_dropped_unpaired_counter(self):
        source = read("src/ImageProcessor.cpp")
        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "\n}\n\nImageProcessor::ImageProcessor", 1
        )[0]
        self.assertIn("while (m_pendingCentroids[cameraIndex].size() > pendingCentroidQueueLimit())", process_body)
        self.assertIn("m_pendingCentroids[cameraIndex].removeFirst();", process_body)
        self.assertIn("++m_droppedUnpairedSamples;", process_body)


if __name__ == "__main__":
    unittest.main()

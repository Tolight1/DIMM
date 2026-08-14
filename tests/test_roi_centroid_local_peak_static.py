from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class RoiCentroidLocalPeakStaticTest(unittest.TestCase):
    def test_roi_centroid_uses_global_noise_threshold_without_local_peak_fallback(self):
        source = read("src/ImageProcessor.cpp")
        body = source.split("CentroidResult ImageProcessorWorker::centerOfGravity", 1)[1].split(
            "CentroidResult ImageProcessorWorker::gaussianFit", 1
        )[0]

        self.assertIn("result.background + centroidSigmaThreshold * sigma", body)
        self.assertIn("if (peakValue <= result.threshold)", body)
        self.assertIn("result.signalPixelCount < static_cast<quint64>(centroidMinimumSignalPixels)", body)
        self.assertNotIn("tryLocalPeakCentroid", body)
        self.assertNotIn("localPeakThreshold", body)

    def test_corrected_roi_centroid_does_not_fallback_to_raw_roi_noise(self):
        source = read("src/ImageProcessor.cpp")
        body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor", 1
        )[0]

        self.assertIn("calculateCentroid(correctedRoiImage)", body)
        self.assertNotIn("rawFallbackCentroid", body)
        self.assertNotIn("isReliableRawFallbackCentroid", body)
        self.assertNotIn("calculateCentroid(roiImage)", body)

    def test_dimm_rejects_roi_edge_centroids_for_live_tracking_loss(self):
        source = read("src/DIMM.LiveRoi.cpp")
        dimm_cpp = read("src/DIMM.cpp")
        body = source.split("bool DIMM::isUsableCentroidSample", 1)[1].split(
            "RoiRect DIMM::sanitizeRoi", 1
        )[0]
        edge_body = source.split("bool DIMM::isCentroidNearCurrentRoiEdge", 1)[1].split(
            "bool DIMM::isCentroidTooFarFromCurrentRoiCenter",
            1,
        )[0]
        centroid_callback = dimm_cpp.split("&ImageProcessor::centroidReady", 1)[1].split(
            "connect(m_imageProcessor,\n            &ImageProcessor::differentialSampleReady",
            1,
        )[0]

        self.assertIn("x < 0.0 || y < 0.0", body)
        self.assertIn("x >= frameSize.width()", body)
        self.assertIn("getCurrentRoi(cameraIndex)", edge_body)
        self.assertIn("kRoiEdgeUpdateMarginPx", edge_body)
        self.assertIn("isCentroidNearCurrentRoiEdge(camIdx, x, y)", centroid_callback)
        self.assertIn("handleLiveRoiCentroidLoss(camIdx)", centroid_callback)
        self.assertNotIn("runtime.lostCentroidFrameCount[camIdx] = 0", centroid_callback)
        self.assertNotIn("runtime.lostCentroidSinceMs[camIdx] = -1", centroid_callback)


if __name__ == "__main__":
    unittest.main()

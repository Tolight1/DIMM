from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ProcessingDiagnosticsStaticTest(unittest.TestCase):
    def test_image_processor_emits_measurement_diagnostics(self):
        header = read("src/ImageProcessor.h")
        cpp = read("src/ImageProcessor.cpp")

        self.assertIn("enum class MeasurementRejectReason", header)
        self.assertIn("measurementDiagnosticReady", header)
        self.assertIn("MeasurementRejectReason::EdgeSignal", cpp)
        self.assertIn("MeasurementRejectReason::QualityGate", cpp)
        self.assertIn("MeasurementRejectReason::RoiInvalid", cpp)

    def test_runtime_tracks_per_camera_detection_and_reject_reasons(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")

        self.assertIn("detectedCentroidCountPerCamera[2]", header)
        self.assertIn("usableCentroidCountPerCamera[2]", header)
        self.assertIn("qualityRejectedCountPerCamera[2]", header)
        self.assertIn("edgeRejectedCountPerCamera[2]", header)
        self.assertIn("roiInvalidRejectedCountPerCamera[2]", header)
        self.assertIn("检测质心/可用", cpp)
        self.assertIn("可用 相机1/相机2", cpp)
        self.assertIn("拒绝 边缘/质量/ROI", cpp)

    def test_edge_bright_pixels_do_not_block_centered_centroid_pairing(self):
        cpp = read("src/ImageProcessor.cpp")
        quality_body = cpp.split("MeasurementRejectReason ImageProcessorWorker::measurementRejectReason", 1)[1].split(
            "CentroidResult ImageProcessorWorker::centerOfGravity",
            1,
        )[0]
        process_body = cpp.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor",
            1,
        )[0]

        self.assertIn("constexpr double roiEdgeMargin = 6.0", quality_body)
        self.assertNotIn("hasThresholdSignalNearRoiEdge(roiImage, centroid.threshold)", quality_body)
        self.assertNotIn("rawEdgeSignal", process_body)


if __name__ == "__main__":
    unittest.main()

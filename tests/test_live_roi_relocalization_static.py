from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class LiveRoiRelocalizationStaticTest(unittest.TestCase):
    def test_lost_roi_centroid_triggers_full_frame_relocalization(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        runtime_helpers_h = read("src/DimmRuntimeHelpers.h")

        self.assertIn("lostCentroidFrameCount", dimm_h)
        self.assertIn("lostCentroidSinceMs", dimm_h)
        self.assertIn("kLostCentroidRelocalizeTimeoutMs = 1500", runtime_helpers_h)
        self.assertIn("handleLiveRoiCentroidLoss", live_cpp)
        self.assertIn("requestLiveFullFrameRelocalization", live_cpp)

    def test_live_centroid_gate_matches_roi_processor_tolerance(self):
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        usable_body = live_cpp.split("bool DIMM::isUsableCentroidSample", 1)[1].split(
            "RoiRect DIMM::sanitizeRoi", 1
        )[0]

        self.assertNotIn("kLostCentroidRelocalizeFrames = 600", live_cpp)
        self.assertIn("background + 4.0", usable_body)
        self.assertIn("signalPixelCount < 2", usable_body)
        self.assertNotIn("background + 8.0", usable_body)
        self.assertNotIn("signalPixelCount < 6", usable_body)

    def test_lost_roi_centroid_relocalization_is_time_based_not_frame_count_based(self):
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        loss_body = live_cpp.split("void DIMM::handleLiveRoiCentroidLoss", 1)[1].split(
            "bool DIMM::isUsableCentroidSample",
            1,
        )[0]

        self.assertIn("QDateTime::currentMSecsSinceEpoch()", loss_body)
        self.assertIn("lostCentroidSinceMs[cameraIndex]", loss_body)
        self.assertIn("kLostCentroidRelocalizeTimeoutMs", loss_body)
        self.assertNotIn("< kLostCentroidRelocalizeFrames", loss_body)

    def test_live_roi_recenters_before_star_reaches_edge(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        runtime_helpers_h = read("src/DimmRuntimeHelpers.h")
        helper_body = live_cpp.split("bool DIMM::isCentroidTooFarFromCurrentRoiCenter", 1)[1].split(
            "bool DIMM::shouldUpdateRoiForRecentering",
            1,
        )[0]
        callback_body = dimm_cpp.split("&ImageProcessor::centroidReady", 1)[1].split(
            "connect(m_imageProcessor,\n            &ImageProcessor::differentialSampleReady",
            1,
        )[0]

        self.assertIn("isCentroidTooFarFromCurrentRoiCenter", dimm_h)
        self.assertIn("shouldUpdateRoiForRecentering", dimm_h)
        self.assertIn("kRoiEdgeUpdateMarginPx = 8", runtime_helpers_h)
        self.assertIn("m_roiRecenteringThresholdPx", dimm_h)
        self.assertIn("m_roiRecenteringThresholdPx", helper_body)
        self.assertIn("localX <=", helper_body)
        self.assertIn("localY <=", helper_body)
        self.assertIn("static_cast<double>(roi.w - 1) - m_roiRecenteringThresholdPx", helper_body)
        self.assertIn("static_cast<double>(roi.h - 1) - m_roiRecenteringThresholdPx", helper_body)
        self.assertNotIn("std::hypot(localX - static_cast<double>(roi.w - 1) * 0.5", helper_body)
        self.assertIn("shouldUpdateRoiForRecentering()", callback_body)
        self.assertNotIn("shouldUpdateRoiForEdge()", callback_body)


if __name__ == "__main__":
    unittest.main()

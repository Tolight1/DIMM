from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class FullFrameLocalizationFastStaticTest(unittest.TestCase):
    def test_initial_star_detectors_handle_high_bit_depth_frames_without_8bit_truncation(self):
        source = read("src/FullFrameStarDetector.cpp")
        image_utils = read("src/ImageUtils.cpp")
        helper_body = image_utils.split("cv::Mat normalizeMono8Frame", 1)[1].split(
            "cv::Mat normalizeDetectionFrame",
            1,
        )[0]
        candidate_body = source.split("QVector<InitialStarCandidate> detectInitialStarCandidates", 1)[1].split(
            "bool detectInitialStarCentroid",
            1,
        )[0]
        raw_peak_body = source.split("bool detectRawInitialStarPeakCandidate", 1)[1].split(
            "bool detectInitialStarCentroid",
            1,
        )[0]
        fast_body = source.split("bool detectInitialStarCentroidFast", 1)[1]

        self.assertIn("cv::minMaxLoc(grayscale, &minValue, &maxValue)", helper_body)
        self.assertIn("255.0 / (maxValue - minValue)", helper_body)
        self.assertIn("case CV_16U", source)
        self.assertIn("rawPixelValueAt(grayscale, y, x)", candidate_body)
        self.assertIn("rawPixelValueAt(grayscale, y, x)", raw_peak_body)
        self.assertIn("ImageUtils::normalizeMono8Frame(grayscale)", fast_body)
        self.assertNotIn("grayscale.convertTo(mono8, CV_8UC1);", candidate_body)
        self.assertNotIn("grayscale.convertTo(mono8, CV_8UC1);", raw_peak_body)

    def test_live_seed_passes_original_grayscale_into_detectors(self):
        source = read("src/DIMM.LiveRoi.cpp")
        seed_body = source.split("bool DIMM::maybeSeedRoiFromFrame", 1)[1].split(
            "void DIMM::handleLiveRelocalizationWatchdog",
            1,
        )[0]

        self.assertNotIn("cv::Mat mono8 = ImageUtils::normalizeMono8Frame(grayscale)", seed_body)
        self.assertIn("selectLiveRelocalizationCentroid(cameraIndex, grayscale, &centroid, &peakValue)", seed_body)
        self.assertIn("detectInitialStarCandidates(grayscale, &peakValue)", seed_body)
        self.assertNotIn("detectRawInitialStarPeakCandidate(grayscale", seed_body)
        self.assertNotIn("detectInitialStarCentroid(grayscale, &centroid, &peakValue)", seed_body)
        self.assertNotIn("detectInitialStarCentroidFast(grayscale, &centroid, &peakValue)", seed_body)

    def test_live_relocalization_uses_filtered_candidate_detector(self):
        source = read("src/DIMM.LiveRoi.cpp")
        relocalization_body = source.split("bool DIMM::selectLiveRelocalizationCentroid", 1)[1].split(
            "bool DIMM::maybeSeedRoiFromFrame", 1
        )[0]

        self.assertIn("detectInitialStarCandidates(fullFrame, peakValue)", relocalization_body)
        self.assertIn("PolarisDetectionPipeline::selectInitialStarCandidate", relocalization_body)
        self.assertIn("PolarisDetectionPipeline::chooseAutomaticInitialStarCandidate", relocalization_body)
        self.assertNotIn("detectRawInitialStarPeakCandidate(fullFrame, &peakCandidate, peakValue)", relocalization_body)
        self.assertNotIn("detectInitialStarCentroid(", relocalization_body)
        self.assertNotIn("detectInitialStarCentroidFast", relocalization_body)

    def test_full_frame_seed_does_not_accumulate_large_float_frames(self):
        source = read("src/DIMM.LiveRoi.cpp")
        seed_body = source.split("bool DIMM::maybeSeedRoiFromFrame", 1)[1].split(
            "void DIMM::updateFullFrameRoiOverlay", 1
        )[0]

        self.assertNotIn("CV_32FC1", seed_body)
        self.assertNotIn("cv::accumulate", seed_body)

    def test_full_frame_candidate_detector_uses_configured_area_bounds(self):
        source = read("src/FullFrameStarDetector.cpp")
        config_header = read("src/InitialStarDetectionConfig.h")
        config_body = config_header.split("struct InitialStarDetectionConfig", 1)[1].split(
            "};",
            1,
        )[0]
        detector_body = source.split("QVector<InitialStarCandidate> detectInitialStarCandidates", 1)[1].split(
            "bool detectInitialStarCentroid",
            1,
        )[0]

        self.assertIn("int minArea = 8", config_body)
        self.assertIn("area < config.minArea", detector_body)

    def test_live_relocalization_rejects_single_pixel_peak_bypass(self):
        dimm_cpp = read("src/DIMM.Alignment.cpp")
        live_cpp = read("src/DIMM.LiveRoi.cpp")
        relocalization_body = live_cpp.split("bool DIMM::selectLiveRelocalizationCentroid", 1)[1].split(
            "bool DIMM::maybeSeedRoiFromFrame", 1
        )[0]

        self.assertIn("detectRawInitialStarPeakCandidate", dimm_cpp)
        self.assertNotIn("InitialStarCandidate peakCandidate", relocalization_body)
        self.assertNotIn("*centroid = peakCandidate.center", relocalization_body)

    def test_raw_peak_candidate_fallback_uses_runtime_config_and_area_limits(self):
        source = read("src/FullFrameStarDetector.cpp")
        body = source.split("bool detectRawInitialStarPeakCandidate", 1)[1].split(
            "bool detectInitialStarCentroid", 1
        )[0]

        self.assertIn("currentInitialStarDetectionConfig()", body)
        self.assertIn("supportCount < config.minArea || supportCount > config.maxArea", body)
        self.assertIn("rawPixelValueAt(grayscale, y, x)", body)
        self.assertNotIn("localStd", body)

    def test_live_relocalization_does_not_use_alignment_preference_gate(self):
        source = read("src/DIMM.LiveRoi.cpp")
        seed_body = source.split("bool DIMM::maybeSeedRoiFromFrame", 1)[1].split(
            "void DIMM::updateFullFrameRoiOverlay", 1
        )[0]

        self.assertIn("const bool usePreferenceGate = !liveLocatePhase && hasPreferredInitialTarget", seed_body)
        self.assertIn("PolarisDetectionPipeline::selectInitialStarCandidate", seed_body)
        self.assertIn("candidates,", seed_body)
        self.assertIn("usePreferenceGate,", seed_body)


if __name__ == "__main__":
    unittest.main()

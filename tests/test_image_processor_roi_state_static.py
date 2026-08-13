from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ImageProcessorRoiStateStaticTest(unittest.TestCase):
    def test_pair_roi_update_is_atomic_and_resets_history_once(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")
        worker_body = source.split("void ImageProcessorWorker::setPairRois", 1)[1].split(
            "cv::Mat ImageProcessorWorker::preprocess",
            1,
        )[0]
        public_body = source.split("void ImageProcessor::setPairRois", 1)[1].split(
            "void ImageProcessor::resetProcessingState",
            1,
        )[0]

        self.assertIn("void setPairRois(const RoiRect rois[2])", header)
        self.assertIn("void ImageProcessorWorker::setPairRois", source)
        self.assertEqual(worker_body.count("resetRoiProcessingHistory();"), 1)
        self.assertIn("m_currentRoi[0] = sanitized[0]", worker_body)
        self.assertIn("m_currentRoi[1] = sanitized[1]", worker_body)
        self.assertIn("Q_ARG(RoiRect, rois[0])", public_body)
        self.assertIn("Q_ARG(RoiRect, rois[1])", public_body)

    def test_dimm_uses_pair_roi_updates_for_dual_camera_live_roi_changes(self):
        source = read("src/DIMM.LiveRoi.cpp")
        commit_body = source.split("bool DIMM::commitPairedInitialRoisIfReady", 1)[1].split(
            "bool DIMM::startHardwarePulseStage",
            1,
        )[0]
        update_body = source.split("void DIMM::updateMinuteRoi", 1)[1].split(
            "void DIMM::hideLegacyRoiScheduleUi",
            1,
        )[0]
        live_success_block = update_body.split("if (applyLiveHardwareRois(liveRois, &reason, actualRois))", 1)[1].split(
            "} else {",
            1,
        )[0]

        self.assertIn("m_imageProcessor->setPairRois(actualRois)", commit_body)
        self.assertIn("m_imageProcessor->setPairRois(actualRois)", live_success_block)
        self.assertNotIn("m_imageProcessor->setCurrentRoi(0, actualRoi0)", live_success_block)
        self.assertNotIn("m_imageProcessor->setCurrentRoi(1, actualRoi1)", live_success_block)

    def test_only_valid_centroid_is_enqueued_for_pairing(self):
        source = read("src/ImageProcessor.cpp")
        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "\n}\n\nImageProcessor::ImageProcessor",
            1,
        )[0]

        # 参数计算前的质心质量筛选已删除：只有 centroid.valid 决定是否进入配对队列。
        self.assertNotIn("if (!measurementUsable)", process_body)
        self.assertIn("if (centroid.valid) {", process_body)
        self.assertIn("emit centroidReady", process_body)
        self.assertIn("m_pendingCentroids[cameraIndex].append(pending)", process_body)
        self.assertLess(process_body.find("if (centroid.valid) {"),
                        process_body.find("m_pendingCentroids[cameraIndex].append"))


if __name__ == "__main__":
    unittest.main()

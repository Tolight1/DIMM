from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AcquisitionGenerationStaticTest(unittest.TestCase):
    def test_image_processor_rejects_stale_generation_frames(self):
        header = read("src/ImageProcessor.h")
        source = read("src/ImageProcessor.cpp")

        self.assertIn("void advanceAcquisitionGeneration()", header)
        self.assertIn("quint64 currentAcquisitionGeneration() const", header)
        self.assertIn("quint64 acquisitionGeneration", header)
        self.assertIn("std::shared_ptr<std::atomic<quint64>> m_acquisitionGeneration", header)
        self.assertIn("std::make_shared<std::atomic<quint64>>(1)", header)

        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "\n}\n\nImageProcessor::ImageProcessor",
            1,
        )[0]
        self.assertIn("quint64 acquisitionGeneration", process_body)
        self.assertIn("acquisitionGeneration != m_acquisitionGeneration->load()", process_body)
        self.assertLess(
            process_body.find("acquisitionGeneration != m_acquisitionGeneration->load()"),
            process_body.find("calculateCentroid(cameraIndex, roi, correctedRoiImage)"),
        )

    def test_dimm_advances_generation_on_acquisition_mode_boundaries(self):
        header = read("src/DIMM.h")
        source = read("src/DIMM.LiveRoi.cpp")

        self.assertIn("void advanceLiveAcquisitionGeneration()", header)
        self.assertIn("quint64 m_liveAcquisitionGeneration = 1", header)

        advance_body = source.split("void DIMM::advanceLiveAcquisitionGeneration", 1)[1].split(
            "\n}\n\n",
            1,
        )[0]
        self.assertIn("++m_liveAcquisitionGeneration", advance_body)
        self.assertIn("m_imageProcessor->advanceAcquisitionGeneration()", advance_body)
        self.assertIn("resetLiveFrameAcceptanceGates();", advance_body)

        full_frame_body = source.split("bool DIMM::applyLiveFullFrameForRelocalization", 1)[1].split(
            "void DIMM::clearPendingLiveRelocalizationRois",
            1,
        )[0]
        self.assertIn("advanceLiveAcquisitionGeneration();", full_frame_body)

        hardware_roi_body = source.split("bool DIMM::applyLiveHardwareRois", 1)[1].split(
            "bool DIMM::applyLiveFullFrameForRelocalization",
            1,
        )[0]
        self.assertIn("advanceLiveAcquisitionGeneration();", hardware_roi_body)

    def test_live_roi_processing_passes_generation_with_each_frame(self):
        source = read("src/DIMM.CommCamera.cpp")
        live_body = source.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "void DIMM::scheduleHardwareTriggerStartupCheck",
            1,
        )[0]
        process_call = live_body.split("m_imageProcessor->processFrame(cameraIndex,", 1)[1].split(
            ");",
            1,
        )[0]

        self.assertIn("m_liveAcquisitionGeneration", process_call)
        self.assertIn("packet.frameId", process_call)
        self.assertIn("packet.cameraTimestamp", process_call)


if __name__ == "__main__":
    unittest.main()

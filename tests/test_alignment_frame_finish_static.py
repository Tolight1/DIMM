from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentFrameFinishStaticTest(unittest.TestCase):
    def test_dimm_uses_single_frame_finish_helper(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::finishAlignmentFramePreview", 1
        )[0]
        self.assertIn("void DIMM::finishAlignmentFramePreview", alignment_cpp)
        finish_body = alignment_cpp.split("void DIMM::finishAlignmentFramePreview", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]

        self.assertIn("void finishAlignmentFramePreview", header)
        self.assertIn("updateAlignmentOverlay(cameraIndex, packet)", finish_body)
        self.assertIn("m_alignmentSession.camera(cameraIndex).lastPreviewMs = nowMs", finish_body)
        self.assertGreaterEqual(packet_body.count("finishAlignmentFramePreview(cameraIndex, packet, nowMs)"), 5)
        self.assertNotIn("updateAlignmentOverlay(cameraIndex, packet.image)", packet_body)
        self.assertNotIn("lastPreviewMs = nowMs", packet_body)


if __name__ == "__main__":
    unittest.main()

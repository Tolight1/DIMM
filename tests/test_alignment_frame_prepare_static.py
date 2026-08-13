from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentFramePrepareStaticTest(unittest.TestCase):
    def test_dimm_uses_single_frame_prepare_helper(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "bool DIMM::prepareAlignmentFramePreview", 1
        )[0]
        self.assertIn("bool DIMM::prepareAlignmentFramePreview", alignment_cpp)
        prepare_body = alignment_cpp.split("bool DIMM::prepareAlignmentFramePreview", 1)[1].split(
            "void DIMM::finishAlignmentFramePreview", 1
        )[0]

        self.assertIn("bool prepareAlignmentFramePreview", header)
        self.assertIn("targetCanvas->setImage(packet.image)", prepare_body)
        self.assertIn("targetCanvas->setRoiList({})", prepare_body)
        self.assertIn("targetCanvas->setCurrentRoi(-1)", prepare_body)
        self.assertIn("auto& cameraState = m_alignmentSession.camera(cameraIndex)", prepare_body)
        self.assertIn("cameraState.lastFrame = packet.image.clone()", prepare_body)
        self.assertIn("cameraState.lastFrameId = packet.frameId", prepare_body)
        self.assertIn("prepareAlignmentFramePreview(cameraIndex, packet)", packet_body)
        self.assertNotIn("targetCanvas->setImage(packet.image)", packet_body)
        self.assertNotIn("packet.image.clone()", packet_body)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class CameraFrameIdWrapStaticTest(unittest.TestCase):
    def test_camera_manager_extends_wrapping_sdk_frame_ids_before_emitting_packets(self):
        header = read("src/CameraManager.h")
        source = read("src/CameraManager.cpp")

        self.assertIn("lastRawFrameId", header)
        self.assertIn("frameIdWrapOffset", header)
        self.assertIn("hasFrameIdTracking", header)
        self.assertIn("extendWrappingFrameId", source)
        self.assertIn("kFrameIdWrapModulo = 65536", source)
        self.assertIn("rawFrameId", source)
        self.assertIn("rawFrameId + 1", source)
        self.assertIn("packet.frameId = frameId", source)

        callback_body = source.split("void CameraManager::onFrameCaptured", 1)[1].split(
            "void CameraManager::",
            1,
        )[0]
        self.assertLess(callback_body.find("extendWrappingFrameId"),
                        callback_body.find("packet.frameId = frameId"))

    def test_camera_manager_resets_frame_id_tracking_when_acquisition_restarts(self):
        source = read("src/CameraManager.cpp")

        self.assertIn("resetFrameIdTracking", source)
        start_body = source.split("bool CameraManager::startAcquisition", 1)[1].split(
            "bool CameraManager::stopAcquisition",
            1,
        )[0]
        self.assertIn("resetFrameIdTracking(camera);", start_body)


if __name__ == "__main__":
    unittest.main()

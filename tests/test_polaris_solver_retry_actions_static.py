from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverRetryActionsStaticTest(unittest.TestCase):
    def test_dimm_declares_retry_actions_and_force_request_slots(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")

        self.assertIn("void requestAutomaticPolarisSolve(int cameraIndex, bool force)", header)
        self.assertIn("void requestAutomaticPolarisSolveBoth()", header)
        self.assertIn("QAction* m_actionRetryCamera1PolarisSolve", header)
        self.assertIn("QAction* m_actionRetryCamera2PolarisSolve", header)
        self.assertIn("QAction* m_actionRetryBothPolarisSolve", header)
        self.assertIn("new QAction(QStringLiteral(\"重新自动识别相机1\")", cpp)
        self.assertIn("new QAction(QStringLiteral(\"重新自动识别相机2\")", cpp)
        self.assertIn("new QAction(QStringLiteral(\"重新自动识别双相机\")", cpp)

    def test_alignment_frames_are_cached_for_forced_retry(self):
        header = read("src/DIMM.h")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "PolarisSolverConfig DIMM::buildPolarisSolverConfig", 1
        )[0]
        request_body = alignment_cpp.split("void DIMM::requestAutomaticPolarisSolve", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolveBoth", 1
        )[0]

        self.assertIn("AlignmentSession m_alignmentSession", header)
        self.assertIn("cameraState.lastFrame = packet.image.clone()", packet_body)
        self.assertIn("cameraState.lastFrame.empty()", request_body)
        self.assertIn("AlignmentTaskManager::submitFullSolve", request_body)
        self.assertIn("force", request_body)

    def test_retry_actions_are_connected_and_alignment_only(self):
        cpp = read("src/DIMM.cpp")
        ui_cpp = read("src/DIMM.Ui.cpp")
        setup_body = cpp.split("void DIMM::setupConnections", 1)[1].split(
            "void DIMM::updateParams", 1
        )[0]
        refresh_body = ui_cpp.split("void DIMM::refreshActionStates", 1)[1].split(
            "void DIMM::syncCameraSelectionUi", 1
        )[0]

        self.assertIn("&DIMM::requestAutomaticPolarisSolveBoth", setup_body)
        self.assertIn("requestAutomaticPolarisSolve(0, true)", setup_body)
        self.assertIn("requestAutomaticPolarisSolve(1, true)", setup_body)
        self.assertIn("m_actionRetryCamera1PolarisSolve->setEnabled(m_captureState == CaptureState::Alignment", refresh_body)
        self.assertIn("m_actionRetryCamera2PolarisSolve->setEnabled(m_captureState == CaptureState::Alignment", refresh_body)
        self.assertIn("m_actionRetryBothPolarisSolve->setEnabled(m_captureState == CaptureState::Alignment", refresh_body)


if __name__ == "__main__":
    unittest.main()

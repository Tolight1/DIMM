from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentFlowCoordinatorSplitStaticTest(unittest.TestCase):
    def test_alignment_flow_coordinator_module_is_merged_away(self):
        cmake = read("CMakeLists.txt")

        self.assertFalse((ROOT / "src/AlignmentFlowCoordinator.h").exists())
        self.assertFalse((ROOT / "src/AlignmentFlowCoordinator.cpp").exists())
        self.assertNotIn("AlignmentFlowCoordinator", cmake)

    def test_dimm_keeps_alignment_entry_decisions_local(self):
        cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        toggle_body = alignment_cpp.split("void DIMM::onToggleAlignmentMode", 1)[1].split(
            "void DIMM::onConfirmCamera1PolarisCandidate", 1
        )[0]
        start_body = alignment_cpp.split("bool DIMM::startAlignmentMode", 1)[1].split(
            "void DIMM::stopAlignmentMode", 1
        )[0]
        stop_body = alignment_cpp.split("void DIMM::stopAlignmentMode", 1)[1].split(
            "double DIMM::fallbackAlignmentOrbitRadiusPx", 1
        )[0]
        session_cpp = read("src/AlignmentSession.cpp")
        started_view_body = alignment_cpp.split("void DIMM::showAlignmentModeStarted", 1)[1].split(
            "void DIMM::showAlignmentModeStopped", 1
        )[0]
        stopped_view_body = alignment_cpp.split("void DIMM::showAlignmentModeStopped", 1)[1].split(
            "void DIMM::resetAlignmentRuntimeForStart", 1
        )[0]

        self.assertNotIn('#include "AlignmentFlowCoordinator.h"', cpp)
        self.assertIn("struct AlignmentStartReadiness", alignment_cpp)
        self.assertIn("validateAlignmentStartReadiness", alignment_cpp)
        self.assertIn("m_captureState == CaptureState::Alignment", toggle_body)
        self.assertIn("stopAlignmentMode();", toggle_body)
        self.assertNotIn("AlignmentFlowCoordinator::actionForToggle", toggle_body)
        self.assertIn("validateAlignmentStartReadiness", start_body)
        self.assertNotIn("AlignmentFlowCoordinator::validateStartReadiness", start_body)
        self.assertFalse((ROOT / "src/AlignmentRuntimeCoordinator.cpp").exists())
        self.assertNotIn("AlignmentRuntimeCoordinator", cpp)
        self.assertIn("autoSolveEnabled ? AlignmentSolveState::WaitingFrame : AlignmentSolveState::Disabled", session_cpp)
        self.assertIn("AlignmentUiPresenter::waitingAlignmentLabelText", started_view_body)
        self.assertIn("AlignmentUiPresenter::startedAlignmentStatusText", started_view_body)
        self.assertIn("AlignmentUiPresenter::stoppedAlignmentStatusText", stopped_view_body)

    def test_alignment_mode_text_lives_in_ui_presenter(self):
        header = read("src/AlignmentUiPresenter.h")
        cpp = read("src/AlignmentUiPresenter.cpp")

        for function_name in [
            "waitingAlignmentLabelText",
            "stoppedAlignmentLabelText",
            "startedAlignmentStatusText",
            "stoppedAlignmentStatusText",
        ]:
            self.assertIn(f"QString {function_name}", header)
            self.assertIn(f"AlignmentUiPresenter::{function_name}", cpp)


if __name__ == "__main__":
    unittest.main()

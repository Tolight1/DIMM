from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class AlignmentCoarseUiStaticTest(unittest.TestCase):
    def test_dimm_owns_coarse_runtime_and_controller(self):
        header = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")

        self.assertIn('#include "AlignmentCoarseEstimator.h"', header)
        self.assertIn("class AlignmentCoarseController", header)
        self.assertIn("void onToggleCoarseAlignment()", header)
        self.assertIn("void onCoarseAlignmentEstimateReady", header)
        self.assertIn("bool m_alignmentCoarseActive", header)
        self.assertIn("AlignmentCoarseController* m_alignmentCoarseController", header)
        self.assertIn("CoarseAlignmentEstimate m_alignmentCoarseEstimates", header)

        self.assertIn('qRegisterMetaType<CoarseAlignmentEstimate>("CoarseAlignmentEstimate")', dimm_cpp)
        self.assertIn("new AlignmentCoarseController(this)", dimm_cpp)
        self.assertIn("AlignmentCoarseController::estimateReady", dimm_cpp)
        self.assertIn('#include "AlignmentCoarseController.h"', alignment_cpp)

    def test_ui_adds_coarse_button_and_action(self):
        dimm_cpp = read("src/DIMM.cpp")
        ui_cpp = read("src/DIMM.Ui.cpp")

        self.assertIn("m_actionToggleCoarseAlignment", dimm_cpp)
        self.assertIn("btnToggleCoarseAlignment", dimm_cpp)
        self.assertIn("onToggleCoarseAlignment", dimm_cpp)
        self.assertIn("m_btnToggleCoarseAlignment", ui_cpp)
        self.assertIn("开始粗对准", ui_cpp)
        self.assertIn("停止粗对准", ui_cpp)
        self.assertIn("m_actionToggleCoarseAlignment->trigger()", ui_cpp)

    def test_alignment_frame_packet_routes_to_coarse_before_polaris_solver(self):
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "bool DIMM::handleManualAlignmentFrameTracking",
            1,
        )[0]

        self.assertIn("m_alignmentCoarseActive", packet_body)
        self.assertIn("submitCoarseAlignmentFrame(cameraIndex, packet", packet_body)
        self.assertIn("finishAlignmentFramePreview(cameraIndex, packet, nowMs)", packet_body)
        self.assertLess(
            packet_body.index("m_alignmentCoarseActive"),
            packet_body.index("AlignmentFrameCoordinator::nextFrameAction"),
        )
        self.assertNotIn("PolarisSolver", packet_body.split("m_alignmentCoarseActive", 1)[1].split(
            "AlignmentFrameCoordinator::nextFrameAction",
            1,
        )[0])

    def test_coarse_start_cancels_polaris_solves_and_resets_tracking(self):
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        body = alignment_cpp.split("void DIMM::onToggleCoarseAlignment", 1)[1].split(
            "void DIMM::resetCoarseAlignmentRuntime",
            1,
        )[0]
        self.assertIn("m_polarisSolverController->cancelAll", body)
        self.assertIn("resetCoarseAlignmentRuntime()", body)
        self.assertIn("m_alignmentCoarseActive = true", body)

if __name__ == "__main__":
    unittest.main()

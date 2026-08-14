from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentFrameActionStaticTest(unittest.TestCase):
    def test_frame_coordinator_owns_next_frame_action_decision(self):
        header = read("src/AlignmentFrameCoordinator.h")
        cpp = read("src/AlignmentFrameCoordinator.cpp")
        dimm = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "void DIMM::requestAutomaticPolarisSolve", 1
        )[0]

        self.assertIn("enum class FrameAction", header)
        self.assertIn("ManualTrack", header)
        self.assertIn("AutomaticTrack", header)
        self.assertIn("WaitRetry", header)
        self.assertIn("nextFrameAction", header)
        self.assertIn("AlignmentSolveState::ManualOnly", cpp)
        self.assertIn("AlignmentSolveState::Tracking", cpp)
        self.assertIn("AlignmentSolveState::RetryWaiting", cpp)
        self.assertLess(
            cpp.find("runtime.state == AlignmentSolveState::ManualOnly"),
            cpp.find("!autoSolveEnabled"),
        )
        self.assertIn("AlignmentFrameCoordinator::nextFrameAction", packet_body)
        self.assertIn("case AlignmentFrameCoordinator::FrameAction::ManualTrack", packet_body)
        self.assertIn("case AlignmentFrameCoordinator::FrameAction::AutomaticTrack", packet_body)
        self.assertIn("case AlignmentFrameCoordinator::FrameAction::WaitRetry", packet_body)
        self.assertNotIn("if (solveRuntime.state == AlignmentSolveState::ManualOnly", packet_body)
        self.assertNotIn("if (solveRuntime.state == AlignmentSolveState::Tracking", packet_body)
        self.assertNotIn("solveRuntime.state == AlignmentSolveState::RetryWaiting", packet_body)


if __name__ == "__main__":
    unittest.main()

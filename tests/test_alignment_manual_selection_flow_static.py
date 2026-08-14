from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentManualSelectionFlowStaticTest(unittest.TestCase):
    def test_alignment_session_owns_selection_apply_state_writeback(self):
        header = read("src/AlignmentSession.h")
        cpp = read("src/AlignmentSession.cpp")
        dimm = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        selection_body = alignment_cpp.split("bool DIMM::handleAlignmentCandidateSelection", 1)[1].split(
            "bool DIMM::promptAlignmentCandidateSelection", 1
        )[0]
        candidate_apply_body = dimm.split("void DIMM::applyAlignmentSelectedCandidate", 1)[1].split(
            "void DIMM::applyManualAlignmentConfirmation", 1
        )[0]

        self.assertIn("canApplyCandidateSelection", header)
        self.assertNotIn("applyConfirmedCandidate", header)
        self.assertIn("AlignmentSession::canApplyCandidateSelection", selection_body)
        self.assertIn("AlignmentSession::recordSelectedCandidate", candidate_apply_body)
        self.assertIn("manualSelectionRequested || hadConfirmedPolarisBeforeSelection", cpp)
        self.assertIn("*runtime.confirmedPolarisPosition = star", cpp)
        self.assertIn("*runtime.selectionRequested = false", cpp)
        self.assertNotIn("runtime.confirmedPolarisPosition[cameraIndex] = star", candidate_apply_body)
        self.assertNotIn("m_alignmentSession.camera(cameraIndex).selectionRequested = false", candidate_apply_body)

    def test_alignment_session_owns_prompt_bookkeeping(self):
        header = read("src/AlignmentSession.h")
        cpp = read("src/AlignmentSession.cpp")
        dimm = read("src/DIMM.cpp")
        prompt_body = dimm.split("bool DIMM::handleManualAlignmentCandidatePrompt", 1)[1].split(
            "void DIMM::applyAlignmentSelectedCandidate", 1
        )[0]

        self.assertIn("shouldShowCandidatePrompt", header)
        self.assertIn("recordCandidatePromptCancelled", header)
        self.assertIn("recordCandidatePromptAccepted", header)
        self.assertIn("AlignmentSession::shouldShowCandidatePrompt", prompt_body)
        self.assertIn("AlignmentSession::recordCandidatePromptCancelled", prompt_body)
        self.assertIn("AlignmentSession::recordCandidatePromptAccepted", prompt_body)
        self.assertIn("*lastPromptMs = nowMs", cpp)
        self.assertIn("*lastPromptMs = -1", cpp)
        self.assertIn("*selectedCandidateIndex = chosenCandidateIndex", cpp)


if __name__ == "__main__":
    unittest.main()

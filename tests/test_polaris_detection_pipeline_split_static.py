from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisDetectionPipelineSplitStaticTest(unittest.TestCase):
    def test_candidate_types_and_selection_live_in_pipeline_module(self):
        header = read("src/PolarisDetectionPipeline.h")
        cpp = read("src/PolarisDetectionPipeline.cpp")

        self.assertIn("namespace PolarisDetectionPipeline", header)
        self.assertIn("struct InitialStarCandidate", header)
        self.assertIn("struct InitialStarSelection", header)
        self.assertIn("InitialStarSelection selectInitialStarCandidate", header)
        self.assertIn("bool chooseAutomaticInitialStarCandidate", header)
        self.assertIn("QVector<FullFrameCanvas::StarCandidateOverlay> buildCandidateOverlays", header)
        self.assertIn("QVector<InitialStarCandidate> initialCandidatesFromPolarisDetections", header)
        self.assertIn("InitialStarSelection PolarisDetectionPipeline::selectInitialStarCandidate", cpp)
        self.assertIn("bool PolarisDetectionPipeline::chooseAutomaticInitialStarCandidate", cpp)

    def test_dimm_uses_pipeline_for_candidate_selection_helpers(self):
        dimm = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        session_cpp = read("src/AlignmentSession.cpp")

        self.assertIn('#include "PolarisDetectionPipeline.h"', dimm)
        self.assertIn("using PolarisDetectionPipeline::InitialStarCandidate", dimm)
        self.assertIn("using PolarisDetectionPipeline::InitialStarSelection", dimm)

        # DIMM collects alignment candidates through AlignmentSession, which converts
        # the solver detections via the pipeline helper.
        collect_body = alignment_cpp.split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[1].split("bool DIMM::handleAlignmentCandidateSelection", 1)[0]
        self.assertIn("AlignmentSession::collectCandidates", collect_body)
        self.assertIn("AlignmentSession::collectCandidates", session_cpp)
        self.assertIn("PolarisDetectionPipeline::initialCandidatesFromPolarisDetections",
                      session_cpp)

        # Overlay and selection helpers are still driven from the alignment flow.
        self.assertIn("PolarisDetectionPipeline::buildCandidateOverlays", alignment_cpp)
        self.assertIn("PolarisDetectionPipeline::selectInitialStarCandidate", alignment_cpp)

    def test_dimm_no_longer_defines_extracted_candidate_helpers(self):
        cpp = read("src/DIMM.cpp")

        self.assertNotIn("struct InitialStarCandidate {", cpp)
        self.assertNotIn("struct InitialStarSelection {", cpp)
        self.assertNotIn("InitialStarSelection selectInitialStarCandidate", cpp)
        self.assertNotIn("bool chooseAutomaticInitialStarCandidate", cpp)
        self.assertNotIn("QVector<FullFrameCanvas::StarCandidateOverlay> buildCandidateOverlays", cpp)
        self.assertNotIn("QVector<InitialStarCandidate> initialCandidatesFromPolarisDetections", cpp)


if __name__ == "__main__":
    unittest.main()

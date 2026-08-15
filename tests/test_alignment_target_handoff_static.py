from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8-sig")


def function_block(source: str, start_token: str, end_token: str) -> str:
    start = source.find(start_token)
    if start < 0:
        raise AssertionError(f"missing function start: {start_token}")
    end = source.find(end_token, start)
    if end < 0:
        raise AssertionError(f"missing function end: {end_token}")
    return source[start:end]


class AlignmentTargetHandoffStaticTest(unittest.TestCase):
    def test_header_exposes_selection_diagnostics(self):
        header = read("src/DIMM.h")

        self.assertIn("bool selectLiveRelocalizationCentroid(", header)
        self.assertIn("QString* selectionSource = nullptr", header)
        self.assertIn("QString* failureReason = nullptr", header)

    def test_live_selection_priority_is_correct(self):
        block = function_block(
            read("src/DIMM.LiveRoi.cpp"),
            "bool DIMM::selectLiveRelocalizationCentroid(",
            "bool DIMM::maybeSeedRoiFromFrame(",
        )

        last_target = block.find("runtime.hasLastTargetPosition")
        alignment_target = block.find("runtime.hasConfirmedPolarisPosition")
        automatic_target = block.find("selectFullFrameStarCandidate")

        self.assertGreaterEqual(last_target, 0)
        self.assertGreaterEqual(alignment_target, 0)
        self.assertGreaterEqual(automatic_target, 0)
        self.assertLess(last_target, alignment_target)
        self.assertLess(alignment_target, automatic_target)

    def test_preferred_target_failure_does_not_switch_star(self):
        block = function_block(
            read("src/DIMM.LiveRoi.cpp"),
            "bool DIMM::selectLiveRelocalizationCentroid(",
            "bool DIMM::maybeSeedRoiFromFrame(",
        )

        self.assertIn("selectNearestCandidate", block)
        self.assertIn(
            "runtime.pendingInitialCandidateSelectionRequired[cameraIndex] = false;",
            block,
        )
        self.assertIn("return false;", block)

        confirmed_start = block.find("if (runtime.hasConfirmedPolarisPosition")
        preferred_failure_start = block.find(
            "if (!preferredTargetLabel.isEmpty() && !selection.selected)"
        )
        self.assertGreaterEqual(confirmed_start, 0)
        self.assertGreaterEqual(preferred_failure_start, 0)
        self.assertIn("return false;", block[preferred_failure_start:])

    def test_live_localization_records_selected_target(self):
        block = function_block(
            read("src/DIMM.LiveRoi.cpp"),
            "bool DIMM::maybeSeedRoiFromFrame(",
            "void DIMM::handleLiveRelocalizationWatchdog(",
        )

        self.assertIn("QString selectionSource;", block)
        self.assertIn("QString selectionFailureReason;", block)
        self.assertIn("runtime.lastTargetPosition[cameraIndex] = centroid;", block)
        self.assertIn("runtime.hasLastTargetPosition[cameraIndex] = true;", block)
        self.assertIn("runtime.confirmedPolarisPosition[cameraIndex] =", block)
        self.assertIn("selectionSource", block)

    def test_measurement_reset_preserves_alignment_target_only(self):
        block = function_block(
            read("src/DIMM.cpp"),
            "void DIMM::resetMeasurementState()",
            "void DIMM::updateCaptureState(",
        )

        self.assertIn("preservedConfirmedPolarisPosition", block)
        self.assertIn("preservedHasConfirmedPolarisPosition", block)
        self.assertIn("runtime.hasLastTargetPosition[0] = false;", block)
        self.assertIn("runtime.hasLastTargetPosition[1] = false;", block)
        self.assertIn("lastTargetPosition belongs to the previous live tracking session", block)

    def test_source_labels_exist(self):
        source = read("src/DIMM.LiveRoi.cpp")
        self.assertIn("preferredTargetLabel", source)
        self.assertIn("selectNearestCandidate", source)

    def test_no_build_system_change_is_required(self):
        cmake = read("CMakeLists.txt")
        self.assertNotIn("test_alignment_target_handoff_static.py", cmake)


if __name__ == "__main__":
    unittest.main()

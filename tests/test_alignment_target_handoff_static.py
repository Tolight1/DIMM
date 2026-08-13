from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(
        encoding="utf-8-sig"
    )


def function_block(
    text: str,
    start_token: str,
    end_token: str,
) -> str:
    start = text.find(start_token)
    if start < 0:
        raise AssertionError(
            f"missing function start: {start_token}"
        )

    end = text.find(end_token, start)
    if end < 0:
        raise AssertionError(
            f"missing function end token: {end_token}"
        )

    return text[start:end]


class AlignmentTargetHandoffStaticTest(
    unittest.TestCase
):
    def test_header_exposes_selection_diagnostics(self):
        header = read("src/DIMM.h")

        self.assertIn(
            "bool selectLiveRelocalizationCentroid(",
            header,
        )
        self.assertIn(
            "QString* selectionSource = nullptr",
            header,
        )
        self.assertIn(
            "QString* failureReason = nullptr",
            header,
        )

    def test_live_selection_priority_is_correct(self):
        cpp = read("src/DIMM.LiveRoi.cpp")
        block = function_block(
            cpp,
            "bool DIMM::selectLiveRelocalizationCentroid(",
            "bool DIMM::maybeSeedRoiFromFrame(",
        )

        last_target = block.find(
            "runtime.hasLastTargetPosition"
        )
        alignment_target = block.find(
            "runtime.hasConfirmedPolarisPosition"
        )
        automatic_target = block.find(
            "chooseAutomaticInitialStarCandidate"
        )

        self.assertGreaterEqual(last_target, 0)
        self.assertGreaterEqual(alignment_target, 0)
        self.assertGreaterEqual(automatic_target, 0)

        self.assertLess(
            last_target,
            alignment_target,
        )
        self.assertLess(
            alignment_target,
            automatic_target,
        )

    def test_preferred_target_failure_does_not_switch_star(self):
        cpp = read("src/DIMM.LiveRoi.cpp")
        block = function_block(
            cpp,
            "bool DIMM::selectLiveRelocalizationCentroid(",
            "bool DIMM::maybeSeedRoiFromFrame(",
        )

        self.assertIn(
            "上次实时跟踪目标附近未找到候选星",
            block,
        )
        self.assertIn(
            "人工确认星点附近未找到候选星",
            block,
        )
        self.assertIn(
            "不切换到其他亮星",
            block,
        )

        confirmed_start = block.find(
            "if (runtime.hasConfirmedPolarisPosition"
        )
        automatic_start = block.find(
            "chooseAutomaticInitialStarCandidate"
        )
        confirmed_block = block[
            confirmed_start:automatic_start
        ]

        self.assertIn(
            "return false;",
            confirmed_block,
        )

    def test_live_localization_records_selected_target(self):
        cpp = read("src/DIMM.LiveRoi.cpp")
        block = function_block(
            cpp,
            "bool DIMM::maybeSeedRoiFromFrame(",
            "void DIMM::handleLiveRelocalizationWatchdog(",
        )

        self.assertIn(
            "QString selectionSource;",
            block,
        )
        self.assertIn(
            "QString selectionFailureReason;",
            block,
        )
        self.assertIn(
            "runtime.lastTargetPosition[cameraIndex] = "
            "centroid;",
            block,
        )
        self.assertIn(
            "runtime.hasLastTargetPosition[cameraIndex] = "
            "true;",
            block,
        )
        self.assertIn(
            "runtime.confirmedPolarisPosition[cameraIndex] =",
            block,
        )
        self.assertIn(
            "来源: %5",
            block,
        )

    def test_measurement_reset_preserves_alignment_target_only(self):
        cpp = read("src/DIMM.cpp")
        block = function_block(
            cpp,
            "void DIMM::resetMeasurementState()",
            "void DIMM::updateCaptureState(",
        )

        self.assertIn(
            "preservedConfirmedPolarisPosition",
            block,
        )
        self.assertIn(
            "preservedHasConfirmedPolarisPosition",
            block,
        )
        self.assertIn(
            "runtime.hasLastTargetPosition[0] = false;",
            block,
        )
        self.assertIn(
            "runtime.hasLastTargetPosition[1] = false;",
            block,
        )
        self.assertIn(
            "lastTargetPosition belongs to the previous "
            "live tracking session",
            block,
        )

    def test_source_labels_exist(self):
        cpp = read("src/DIMM.LiveRoi.cpp")

        self.assertIn(
            'QStringLiteral("上次实时跟踪目标")',
            cpp,
        )
        self.assertIn(
            'QStringLiteral("对准模式人工确认目标")',
            cpp,
        )
        self.assertIn(
            'QStringLiteral("自动最强候选")',
            cpp,
        )

    def test_no_build_system_change_is_required(self):
        cmake = read("CMakeLists.txt")

        self.assertNotIn(
            "test_alignment_target_handoff_static.py",
            cmake,
        )


if __name__ == "__main__":
    unittest.main()

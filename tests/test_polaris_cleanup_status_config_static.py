from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisCleanupStatusConfigStaticTest(unittest.TestCase):
    def test_initial_and_refined_match_tolerances_have_separate_roles(self):
        header = read("src/StarPatternMatcher.h")
        solver_cpp = read("src/PolarisSolver.cpp")
        matcher_cpp = read("src/StarPatternMatcher.cpp")

        self.assertIn("double initialMaxResidualPx", header)
        self.assertIn("matcherConfig.initialMaxResidualPx = config.initialMatchTolerancePx", solver_cpp)
        self.assertIn("matcherConfig.maxResidualPx = config.refinedMatchTolerancePx", solver_cpp)

        match_points_body = matcher_cpp.split("PatternMatchResult StarPatternMatcher::matchPoints", 1)[1].split(
            "QVector<PatternMatchPair> StarPatternMatcher::collectNearestOneToOneMatches", 1
        )[0]
        self.assertIn("initialMaxResidualPx", match_points_body)
        self.assertIn("refinedMaxResidualPx", match_points_body)
        self.assertLess(
            match_points_body.find("initialMaxResidualPx"),
            match_points_body.find("refinedMaxResidualPx"),
        )

    def test_solver_emits_matching_catalog_between_detection_and_matching(self):
        solver_cpp = read("src/PolarisSolver.cpp")
        solve_frame_body = solver_cpp.split("PolarisSolveResult solveFrameWithProgress", 1)[1].split(
            "QVector<DetectedStar> detectStarsFromFrame", 1
        )[0]
        self.assertIn("progress(PolarisSolveStatus::MatchingCatalog", solve_frame_body)
        self.assertLess(
            solve_frame_body.find("detectStarsFromFrame"),
            solve_frame_body.find("progress(PolarisSolveStatus::MatchingCatalog"),
        )
        self.assertLess(
            solve_frame_body.find("progress(PolarisSolveStatus::MatchingCatalog"),
            solve_frame_body.find("solveDetectedStars"),
        )

        controller_body = solver_cpp.split("void PolarisSolverController::startSolveTask", 1)[1].split(
            "void PolarisSolverController::cancelCurrentSolveTasks", 1
        )[0]
        self.assertIn("PolarisSolveStatus::MatchingCatalog", controller_body)
        self.assertIn("emit solveStatusChanged", controller_body)

    def test_manual_confirmation_sends_manual_confirmed_status(self):
        dimm_cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        overlay_body = (alignment_cpp.split("void DIMM::updateAlignmentOverlay", 1)[1] +
                        dimm_cpp.split("void DIMM::applyManualAlignmentConfirmation", 1)[1].split(
                            "void DIMM::updateConfirmedPolarisFromFallbackCentroid", 1)[0])
        self.assertIn("PolarisSolveStatus::ManualConfirmed", overlay_body)
        self.assertIn("onPolarisSolveStatusChanged(cameraIndex", overlay_body)

        status_body = dimm_cpp.split("void DIMM::onPolarisSolveStatusChanged", 1)[1].split(
            "InitialStarSelection DIMM::selectAlignmentInitialCandidate", 1
        )[0]
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")
        self.assertIn("status == PolarisSolveStatus::ManualConfirmed", status_body)
        self.assertIn("AlignmentUiPresenter::formatManualConfirmedSolveLabel", status_body)
        self.assertIn("自动识别: 人工确认", presenter_cpp)

    def test_low_confidence_count_changes_retry_delay_and_user_prompt(self):
        dimm_cpp = read("src/DIMM.Alignment.cpp")
        task_cpp = read("src/AlignmentTaskManager.cpp")
        finished_body = dimm_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]
        low_confidence_body = finished_body.split("result.status == PolarisSolveStatus::LowConfidence", 1)[1]

        self.assertIn("AlignmentTaskManager::applySolveFailureRetry", low_confidence_body)
        self.assertIn("++runtime->consecutiveLowConfidenceResults", task_cpp)
        self.assertIn("lowConfidenceRetryMultiplier", task_cpp)
        self.assertIn("baseRetryIntervalMs * lowConfidenceRetryMultiplier", task_cpp)
        self.assertIn("请检查焦距、星点数量和阈值", task_cpp)


if __name__ == "__main__":
    unittest.main()

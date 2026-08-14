from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolveDebugLoggingStaticTest(unittest.TestCase):
    def test_dimm_declares_solve_result_logging_helpers(self):
        header = read("src/DIMM.h")
        presenter_header = read("src/AlignmentUiPresenter.h")

        self.assertIn("void logPolarisSolveResult(const PolarisSolveResult& result) const", header)
        self.assertIn("QString polarisSolveStatusText(PolarisSolveStatus status)", presenter_header)
        self.assertIn("QString formatPolarisSolveLogLine(const PolarisSolveResult& result)", presenter_header)

    def test_finished_solver_results_are_logged_with_diagnostics(self):
        cpp = read("src/DIMM.Alignment.cpp")
        finished_body = cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]

        self.assertIn("logPolarisSolveResult(result);", finished_body)
        self.assertLess(
            finished_body.index("logPolarisSolveResult(result);"),
            finished_body.index("cameraState.solveResult = result;"),
        )

    def test_solve_result_log_contains_key_quality_and_timing_fields(self):
        dimm_cpp = read("src/DIMM.cpp")
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")
        dimm_log_body = dimm_cpp.split("void DIMM::logPolarisSolveResult", 1)[1].split(
            "void DIMM::onPolarisSolveFinished", 1
        )[0]
        log_body = presenter_cpp.split("QString AlignmentUiPresenter::formatPolarisSolveLogLine", 1)[1]

        self.assertIn("qInfo().noquote()", dimm_log_body)
        self.assertIn("AlignmentUiPresenter::formatPolarisSolveLogLine(result)", dimm_log_body)
        self.assertIn("Polaris solve", log_body)
        self.assertIn("result.cameraIndex + 1", log_body)
        self.assertIn("polarisSolveStatusText(result.status)", log_body)
        self.assertIn("result.valid", log_body)
        self.assertIn("result.detectedStarCount", log_body)
        self.assertIn("result.matchedStarCount", log_body)
        self.assertIn("result.rmsPx", log_body)
        self.assertIn("result.maxResidualPx", log_body)
        self.assertIn("result.scoreMargin", log_body)
        self.assertIn("result.matchedSpatialSpreadPx", log_body)
        self.assertIn("result.timing.detectionMs", log_body)
        self.assertIn("result.timing.matchingMs", log_body)
        self.assertIn("result.timing.totalMs", log_body)
        self.assertIn("result.mirrored", log_body)
        self.assertIn("result.message", log_body)


if __name__ == "__main__":
    unittest.main()

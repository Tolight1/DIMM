from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisCoordinateModelMetadataStaticTest(unittest.TestCase):
    def test_solve_result_reports_epoch_and_coordinate_model(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")
        solve_body = cpp.split("PolarisSolveResult solveDetectedStars", 1)[1].split(
            "PolarisSolverController::PolarisSolverController", 1
        )[0]

        self.assertIn("double observationEpochYear", header)
        self.assertIn("QString coordinateModel", header)
        self.assertIn("result.observationEpochYear = config.observationEpochYear", solve_body)
        self.assertIn("目录近似北天极", solve_body)
        self.assertIn("不含岁差章动极移折射", solve_body)

    def test_success_presenter_labels_include_epoch_metadata(self):
        dimm_cpp = read("src/DIMM.Alignment.cpp")
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")
        finished_body = dimm_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]

        self.assertIn("AlignmentUiPresenter::formatSolvedSolveLabel", finished_body)
        self.assertIn("历元 %", presenter_cpp)
        self.assertIn("result.observationEpochYear", presenter_cpp)
        self.assertIn("result.coordinateModel", presenter_cpp)


if __name__ == "__main__":
    unittest.main()

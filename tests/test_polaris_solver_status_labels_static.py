from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisSolverStatusLabelsStaticTest(unittest.TestCase):
    def test_dimm_declares_and_creates_per_camera_solver_status_labels(self):
        header = read("src/DIMM.h")
        ui_cpp = read("src/DIMM.Ui.cpp")

        self.assertIn("QLabel* m_lblAlignmentSolveCam1", header)
        self.assertIn("QLabel* m_lblAlignmentSolveCam2", header)
        self.assertIn("m_lblAlignmentSolveCam1 = new QLabel", ui_cpp)
        self.assertIn("m_lblAlignmentSolveCam2 = new QLabel", ui_cpp)
        self.assertIn("cam1PanelLayout->addWidget(m_lblAlignmentSolveCam1)", ui_cpp)
        self.assertIn("cam2PanelLayout->addWidget(m_lblAlignmentSolveCam2)", ui_cpp)

    def test_solver_status_callbacks_update_per_camera_labels(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")
        finished_body = alignment_cpp.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]
        status_body = cpp.split("void DIMM::onPolarisSolveStatusChanged", 1)[1].split(
            "InitialStarSelection DIMM::selectAlignmentInitialCandidate", 1
        )[0]

        self.assertIn("void setAlignmentSolveLabel", header)
        self.assertIn("setAlignmentSolveLabel(result.cameraIndex", finished_body)
        self.assertIn("AlignmentUiPresenter::formatSolvedSolveLabel", finished_body)
        self.assertIn("匹配成功", presenter_cpp)
        self.assertIn("setAlignmentSolveLabel(cameraIndex", status_body)
        self.assertIn("AlignmentUiPresenter::formatMatchingSolveLabel", status_body)
        self.assertIn("自动识别: 匹配中", presenter_cpp)


if __name__ == "__main__":
    unittest.main()

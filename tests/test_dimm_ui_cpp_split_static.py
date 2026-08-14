from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmUiCppSplitStaticTest(unittest.TestCase):
    def test_ui_members_live_in_dimm_ui_cpp(self):
        dimm = read("src/DIMM.cpp")
        ui_cpp = read("src/DIMM.Ui.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "void DIMM::setupStatusBarUi()",
            "void DIMM::setupMainWindowUi()",
            "void DIMM::setupPreviewCanvases()",
            "void DIMM::setupFullFramePreviewCanvases()",
            "void DIMM::setupRoiPreviewCanvases()",
            "void DIMM::setupChartCanvases()",
            "void DIMM::setupCanvasMouseStatusConnections()",
            "void DIMM::refreshUi()",
            "void DIMM::refreshStatusUi()",
            "void DIMM::refreshCameraUi()",
            "void DIMM::refreshMeasurementUi()",
            "void DIMM::refreshPanelUi()",
            "void DIMM::refreshActionStates()",
            "void DIMM::syncCameraSelectionUi()",
            "QString DIMM::currentPreviewModeText() const",
            "void DIMM::setStatusMessage(const QString& text, const QString& color)",
            "void DIMM::setStatusMessage(const QString& text, UiStatusLevel level)",
            "void DIMM::setAlignmentSolveLabel(int cameraIndex",
            "void DIMM::setDetailViewMode(DetailViewMode mode)",
        ]:
            self.assertIn(token, ui_cpp)
            self.assertNotIn(token, dimm)

        self.assertIn("src/DIMM.Ui.cpp", cmake)

if __name__ == "__main__":
    unittest.main()

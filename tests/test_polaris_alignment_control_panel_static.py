from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisAlignmentControlPanelStaticTest(unittest.TestCase):
    def test_alignment_controls_are_embedded_in_preview_panels(self):
        header = read("src/DIMM.h")
        ui_cpp = read("src/DIMM.Ui.cpp")

        for field in [
            "QPushButton* m_btnConfirmCamera1Polaris",
            "QPushButton* m_btnConfirmCamera2Polaris",
            "QPushButton* m_btnRetryCamera1PolarisSolve",
            "QPushButton* m_btnRetryCamera2PolarisSolve",
            "QPushButton* m_btnRetryBothPolarisSolve",
        ]:
            self.assertIn(field, header)

        self.assertIn("m_btnRetryCamera1PolarisSolve = new QPushButton", ui_cpp)
        self.assertIn("m_btnConfirmCamera1Polaris = new QPushButton", ui_cpp)
        self.assertIn("m_btnRetryCamera2PolarisSolve = new QPushButton", ui_cpp)
        self.assertIn("m_btnConfirmCamera2Polaris = new QPushButton", ui_cpp)
        self.assertIn("m_btnRetryBothPolarisSolve = new QPushButton", ui_cpp)
        self.assertIn("cam1AlignmentControlsLayout->addWidget", ui_cpp)
        self.assertIn("cam2AlignmentControlsLayout->addWidget", ui_cpp)

    def test_toolbar_keeps_only_alignment_mode_entry(self):
        cpp = read("src/DIMM.cpp")
        constructor_body = cpp.split("DIMM::DIMM", 1)[1].split(
            "void DIMM::setupConnections", 1
        )[0]

        self.assertIn("ui->toolbar->insertAction(ui->btnSettings, m_actionAlignmentMode)", constructor_body)
        self.assertNotIn("ui->toolbar->insertAction(ui->btnSettings, m_actionConfirmCamera", constructor_body)
        self.assertNotIn("ui->toolbar->insertAction(m_actionConfirmCamera", constructor_body)
        self.assertNotIn("ui->toolbar->insertAction(m_actionRetry", constructor_body)

    def test_embedded_confirm_buttons_call_selection_slots_after_buttons_are_created(self):
        cpp = read("src/DIMM.cpp")
        ui_cpp = read("src/DIMM.Ui.cpp")
        preview_body = ui_cpp.split("void DIMM::setupFullFramePreviewCanvases", 1)[1].split(
            "void DIMM::setupRoiPreviewCanvases", 1
        )[0]
        setup_body = cpp.split("void DIMM::setupConnections", 1)[1].split(
            "void DIMM::updateParams", 1
        )[0]

        self.assertLess(
            preview_body.find("m_btnConfirmCamera1Polaris = new QPushButton"),
            preview_body.find("connect(m_btnConfirmCamera1Polaris"),
        )
        self.assertLess(
            preview_body.find("m_btnConfirmCamera2Polaris = new QPushButton"),
            preview_body.find("connect(m_btnConfirmCamera2Polaris"),
        )
        self.assertIn("&DIMM::onConfirmCamera1PolarisCandidate", preview_body)
        self.assertIn("&DIMM::onConfirmCamera2PolarisCandidate", preview_body)
        self.assertNotIn("m_actionConfirmCamera1Polaris->trigger()", setup_body)
        self.assertNotIn("m_actionConfirmCamera2Polaris->trigger()", setup_body)

    def test_embedded_buttons_reuse_existing_state(self):
        cpp = read("src/DIMM.cpp")
        ui_cpp = read("src/DIMM.Ui.cpp")
        preview_body = ui_cpp.split("void DIMM::setupFullFramePreviewCanvases", 1)[1].split(
            "void DIMM::setupRoiPreviewCanvases", 1
        )[0]
        refresh_body = ui_cpp.split("void DIMM::refreshActionStates", 1)[1].split(
            "void DIMM::syncCameraSelectionUi", 1
        )[0]

        self.assertIn("connect(m_btnConfirmCamera1Polaris", preview_body)
        self.assertIn("connect(m_btnRetryBothPolarisSolve", preview_body)
        self.assertIn("m_actionRetryBothPolarisSolve->trigger()", preview_body)
        self.assertIn("m_btnConfirmCamera1Polaris->setEnabled", refresh_body)
        self.assertIn("m_btnRetryBothPolarisSolve->setVisible", refresh_body)


if __name__ == "__main__":
    unittest.main()

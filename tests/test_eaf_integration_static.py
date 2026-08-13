from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class EafIntegrationStaticTest(unittest.TestCase):
    def test_cmake_contains_eaf_sources_and_sdk_path(self):
        cmake = read("CMakeLists.txt")
        for source in [
            "src/EafSdkLoader.cpp",
            "src/EafFocuserManager.cpp",
            "src/FocuserControlWidget.cpp",
        ]:
            self.assertIn(source, cmake)
        self.assertIn("EAF_SDK_DIR", cmake)
        self.assertIn("EAF_focuser.dll", cmake)

    def test_settings_dialog_supports_external_pages(self):
        header = read("src/SettingsDialog.h")
        cpp = read("src/SettingsDialog.cpp")
        self.assertIn("void addSettingsPage(QWidget* page, const QString& title)", header)
        self.assertIn("QTabWidget* m_tabWidget", header)
        self.assertIn("SettingsDialog::addSettingsPage", cpp)

    def test_dimm_owns_focuser_manager_and_widget(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        self.assertIn("EafFocuserManager* m_focuserManager", header)
        self.assertIn("FocuserControlWidget* m_focuserControlWidget", header)
        self.assertIn("m_focuserManager->initialize()", cpp)
        self.assertIn("m_focuserManager->shutdown()", cpp)

    def test_capture_state_interlock_exists(self):
        cpp = read("src/DIMM.cpp")
        body = cpp.split("void DIMM::updateCaptureState(CaptureState state)")[1]
        self.assertIn("focuserMotionAllowed", body)
        self.assertIn("setMotionAllowed", body)

    def test_eaf_sdk_loader_interface_complete(self):
        loader_h = read("src/EafSdkLoader.h")
        for required_type in [
            "EAFGetNumFn",
            "EAFGetIDFn",
            "EAFOpenFn",
            "EAFGetPropertyFn",
            "EAFMoveFn",
            "EAFStopFn",
            "EAFIsMovingFn",
            "EAFGetPositionFn",
            "EAFResetPostionFn",
            "EAFGetTempFn",
            "EAFSetMaxStepFn",
            "EAFGetMaxStepFn",
            "EAFStepRangeFn",
            "EAFSetReverseFn",
            "EAFGetReverseFn",
            "EAFSetBacklashFn",
            "EAFGetBacklashFn",
            "EAFCloseFn",
            "EAFGetSDKVersionFn",
        ]:
            self.assertIn(required_type, loader_h)

    def test_focuser_manager_has_required_slots_and_signals(self):
        manager_h = read("src/EafFocuserManager.h")
        for slot_sig in [
            "void refreshDevices()",
            "void assignDevice(TelescopeSlot slot, QString serialHex)",
            "void openAssignedDevice(TelescopeSlot slot)",
            "void closeAssignedDevice(TelescopeSlot slot)",
            "void moveAbsolute(TelescopeSlot slot, int target)",
            "void stopMotion(TelescopeSlot slot)",
            "void stateChanged(TelescopeSlot slot, EafDeviceState state)",
            "void commandFailed(TelescopeSlot slot, QString command, QString error)",
        ]:
            self.assertIn(slot_sig, manager_h)

    def test_focuser_data_models_complete(self):
        manager_h = read("src/EafFocuserManager.h")
        for struct_name in ["EafDeviceDescriptor", "EafDeviceState", "FocuserAssignment"]:
            self.assertIn(struct_name + " {", manager_h)


if __name__ == "__main__":
    unittest.main()

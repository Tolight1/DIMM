from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmConfigCppSplitStaticTest(unittest.TestCase):
    def test_config_members_live_in_dimm_config_cpp(self):
        dimm = read("src/DIMM.cpp")
        config = read("src/DIMM.Config.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "void DIMM::setupSettingsCallbacks()",
            "void DIMM::setupCameraSettingsCallbacks()",
            "void DIMM::setupAutoExposureSettingsCallbacks()",
            "void DIMM::setupTriggerSettingsCallbacks()",
            "void DIMM::setupEnvironmentSettingsCallbacks()",
            "void DIMM::setupPulseGeneratorSettingsCallbacks()",
            "void DIMM::setupProcessingSettingsCallbacks()",
            "void DIMM::setupOpticsSettingsCallbacks()",
            "void DIMM::setupAlignmentSettingsCallbacks()",
            "void DIMM::setupStorageSettingsCallbacks()",
            "void DIMM::setupNetworkSettingsCallbacks()",
            "AppConfig DIMM::currentAppConfig() const",
            "void DIMM::applyStartupConfig(const AppConfig& config)",
            "void DIMM::savePersistentSettings()",
            "QVector<int> DIMM::scanHotPixelExposureTemplates() const",
            "bool DIMM::applyExposureAndHotPixelTemplate(int cameraIndex",
            "void DIMM::refreshHotPixelTemplates()",
        ]:
            self.assertIn(token, config)
            self.assertNotIn(token, dimm)

        self.assertIn("src/DIMM.Config.cpp", cmake)

if __name__ == "__main__":
    unittest.main()

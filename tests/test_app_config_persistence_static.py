from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AppConfigPersistenceStaticTest(unittest.TestCase):
    def test_qsettings_persistence_module_is_registered(self):
        header = read("src/AppConfigPersistence.h")
        cpp = read("src/AppConfigPersistence.cpp")
        cmake = read("CMakeLists.txt")

        self.assertIn('#include "AppConfig.h"', header)
        self.assertIn("namespace AppConfigPersistence", header)
        self.assertIn("AppConfig load(const AppConfig& defaults)", header)
        self.assertIn("void save(const AppConfig& config)", header)
        self.assertIn("#include <QSettings>", cpp)
        self.assertIn("settings.beginGroup(QStringLiteral(\"settings\"))", cpp)
        self.assertIn("settings.setValue(QStringLiteral(\"camera/exposureUs\")", cpp)
        self.assertIn("settings.value(QStringLiteral(\"camera/exposureUs\")", cpp)
        self.assertIn("settings.sync()", cpp)
        self.assertIn("src/AppConfigPersistence.h", cmake)
        self.assertIn("src/AppConfigPersistence.cpp", cmake)

    def test_dimm_loads_saved_settings_on_start_and_saves_after_apply(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        config = read("src/DIMM.Config.cpp")

        self.assertIn('#include "AppConfigPersistence.h"', cpp)
        self.assertIn("AppConfig DIMM::currentAppConfig() const", config)
        self.assertIn("void DIMM::applyStartupConfig(const AppConfig& config)", config)
        self.assertIn("void DIMM::savePersistentSettings()", config)
        self.assertIn("applyStartupConfig(AppConfigPersistence::load(currentAppConfig()))", cpp)
        self.assertIn("AppConfigPersistence::save(currentAppConfig())", config)
        self.assertIn("savePersistentSettings()", config.split("m_settingsDialog->onAfterApply", 1)[1])
        self.assertIn("AppConfig currentAppConfig() const", header)
        self.assertIn("void applyStartupConfig(const AppConfig& config)", header)
        self.assertIn("void savePersistentSettings()", header)


if __name__ == "__main__":
    unittest.main()

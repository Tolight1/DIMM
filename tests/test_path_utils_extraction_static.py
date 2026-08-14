from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PathUtilsExtractionStaticTest(unittest.TestCase):
    def test_path_helpers_are_extracted_to_path_utils(self):
        header = read("src/PathUtils.h")
        cpp = read("src/PathUtils.cpp")

        self.assertIn("#pragma once", header)
        self.assertIn("namespace PathUtils", header)
        self.assertIn("QString appDeploymentDirPath()", header)
        self.assertIn("QString resolvePathFromAppDir(const QString& rawPath)", header)
        self.assertIn("QString relativizePathToAppDir(const QString& rawPath)", header)
        self.assertIn("int exposureUsFromTemplatePath(const QString& path)", header)
        self.assertIn("int exposureUsFromTemplateDirName(const QString& name)", header)
        self.assertIn("QString replaceTemplateExposurePath(const QString& path, int exposureUs)", header)

        self.assertIn("QApplication::applicationDirPath()", cpp)
        self.assertIn("QFileInfo candidate(trimmed)", cpp)
        self.assertIn("candidate.isAbsolute()", cpp)
        self.assertIn("QDir(appDeploymentDirPath()).filePath(trimmed)", cpp)
        self.assertIn('QStringLiteral("exposure_")', cpp)
        self.assertIn('QStringLiteral("exposure_%1us")', cpp)
        self.assertIn("arg(exposureUs, 7, 10, QLatin1Char('0'))", cpp)

    def test_dimm_uses_shared_path_helpers_without_local_duplicates(self):
        dimm = read("src/DIMM.cpp")
        dimm_alignment = read("src/DIMM.Alignment.cpp")
        dimm_config = read("src/DIMM.Config.cpp")
        settings = read("src/SettingsDialog.cpp")

        self.assertIn('#include "PathUtils.h"', dimm)
        self.assertIn('#include "PathUtils.h"', settings)
        self.assertNotIn("QString appDeploymentDirPath()", dimm)
        self.assertNotIn("QString resolvePathFromAppDir(const QString& rawPath)", dimm)
        self.assertNotIn("QString relativizePathToAppDir(const QString& rawPath)", dimm)
        self.assertNotIn("int exposureUsFromTemplatePath(const QString& path)", dimm)
        self.assertNotIn("int exposureUsFromTemplateDirName(const QString& name)", dimm)
        self.assertNotIn("QString replaceTemplateExposurePath(const QString& path, int exposureUs)", dimm)

        self.assertIn("PathUtils::relativizePathToAppDir(file)", settings)
        self.assertIn("PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath)", dimm_alignment)
        self.assertIn("PathUtils::exposureUsFromTemplatePath(m_hotPixelCamera0MaskPath)", dimm)
        self.assertIn("PathUtils::exposureUsFromTemplateDirName(entry.fileName())", dimm_config)
        self.assertIn("PathUtils::replaceTemplateExposurePath(m_hotPixelCamera0MaskPath, exposureUs)", dimm_config)

    def test_cmake_explicitly_registers_path_utils_sources(self):
        cmake = read("CMakeLists.txt")

        self.assertIn("src/PathUtils.h", cmake)
        self.assertIn("src/PathUtils.cpp", cmake)


if __name__ == "__main__":
    unittest.main()

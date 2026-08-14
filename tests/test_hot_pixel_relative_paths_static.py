from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class HotPixelRelativePathsStaticTest(unittest.TestCase):
    def test_hot_pixel_paths_are_stored_relative_to_app_dir(self):
        dimm = read("src/DIMM.cpp")
        dimm_config = read("src/DIMM.Config.cpp")
        settings = read("src/SettingsDialog.cpp")

        self.assertIn("QString appDeploymentDirPath()", read("src/PathUtils.cpp"))
        self.assertIn("QString resolvePathFromAppDir(const QString& rawPath)", read("src/PathUtils.h"))
        self.assertIn("QString relativizePathToAppDir(const QString& rawPath)", read("src/PathUtils.h"))
        self.assertIn("(*edit)->setText(PathUtils::relativizePathToAppDir(file));", settings)
        self.assertIn("const QString resolvedHotPixelFile = PathUtils::resolvePathFromAppDir(hotPixelFiles[i]);", settings)
        self.assertIn("m_hotPixelCamera0MaskPath = PathUtils::relativizePathToAppDir(hotSettings.camera0Mask);", dimm)
        self.assertIn("m_hotPixelCamera0MaskPath = enabled ? PathUtils::relativizePathToAppDir(camera0MaskPath) : QString();", dimm_config)
        self.assertIn("m_hotPixelCamera0MaskPath = PathUtils::relativizePathToAppDir(camera0Mask);", dimm)

    def test_runtime_loads_hot_pixel_paths_from_app_dir(self):
        source = read("src/DIMM.cpp")
        dimm_config = read("src/DIMM.Config.cpp")

        self.assertIn("QFileInfo(PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath)).absoluteDir();", source)
        self.assertIn("QFileInfo::exists(PathUtils::resolvePathFromAppDir(cam0Mask))", dimm_config)
        self.assertIn("m_imageProcessor->configureHotPixelTemplates(PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath),", dimm_config)


if __name__ == "__main__":
    unittest.main()

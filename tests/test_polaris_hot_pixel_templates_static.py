from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisHotPixelTemplatesStaticTest(unittest.TestCase):
    def test_solver_config_carries_hot_pixel_templates(self):
        header = read("src/PolarisSolver.h")

        self.assertIn("struct PolarisHotPixelTemplateConfig", header)
        self.assertIn("PolarisHotPixelTemplateConfig hotPixelTemplates[2]", header)
        self.assertIn("QString maskPath", header)
        self.assertIn("QString excessPath", header)
        self.assertIn("int templateWidth", header)
        self.assertIn("int templateHeight", header)

    def test_polaris_detection_applies_hot_pixel_correction_before_normalization(self):
        cpp = read("src/PolarisSolver.cpp")
        detect_body = cpp.split("QVector<DetectedStar> detectStarsFromFrame", 1)[1].split(
            "PolarisSolveResult solveFrame", 1
        )[0]

        self.assertIn("applyPolarisHotPixelCorrection", cpp)
        self.assertIn("const cv::Mat correctedFrame = applyPolarisHotPixelCorrection", detect_body)
        self.assertLess(
            detect_body.find("applyPolarisHotPixelCorrection"),
            detect_body.find("normalizeDetectionFrame"),
        )

    def test_polaris_hot_pixel_template_reads_are_cached_by_file_identity(self):
        cpp = read("src/PolarisSolver.cpp")
        cache_body = cpp.split("struct PolarisHotPixelTemplateCache", 1)[1].split(
            "cv::Mat applyPolarisHotPixelCorrection", 1
        )[0]

        self.assertIn("QDateTime maskLastModified", cache_body)
        self.assertIn("QDateTime excessLastModified", cache_body)
        self.assertIn("static std::array<PolarisHotPixelTemplateCache, 2> caches", cache_body)
        self.assertIn("maskLastModified == maskInfo.lastModified()", cache_body)
        self.assertIn("excessLastModified == excessInfo.lastModified()", cache_body)
        self.assertIn("templateWidth == hot.templateWidth", cache_body)
        self.assertIn("templateHeight == hot.templateHeight", cache_body)
        self.assertIn("maskFile.readAll()", cache_body)
        self.assertIn("cache.maskBytes = loadedMaskBytes", cache_body)
        self.assertIn("cache.excessBytes = loadedExcessBytes", cache_body)

    def test_dimm_passes_hot_pixel_templates_to_polaris_solver_config(self):
        cpp = read("src/DIMM.Alignment.cpp")
        config_body = cpp.split("PolarisSolverConfig DIMM::buildPolarisSolverConfig", 1)[1].split(
            "void DIMM::onPolarisSolveFinished", 1
        )[0]

        self.assertIn("config.hotPixelTemplates[0].enabled = m_hotPixelTemplatesEnabled", config_body)
        self.assertIn("config.hotPixelTemplates[1].enabled = m_hotPixelTemplatesEnabled", config_body)
        self.assertIn("config.hotPixelTemplates[0].maskPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath)", config_body)
        self.assertIn("config.hotPixelTemplates[1].maskPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera1MaskPath)", config_body)
        self.assertIn("config.hotPixelTemplates[0].excessPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera0ExcessPath)", config_body)
        self.assertIn("config.hotPixelTemplates[1].excessPath = PathUtils::resolvePathFromAppDir(m_hotPixelCamera1ExcessPath)", config_body)


if __name__ == "__main__":
    unittest.main()

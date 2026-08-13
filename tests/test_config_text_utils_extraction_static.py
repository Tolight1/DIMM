from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ConfigTextUtilsExtractionStaticTest(unittest.TestCase):
    def test_comment_stripping_is_extracted_to_config_text_utils(self):
        header = read("src/ConfigTextUtils.h")
        cpp = read("src/ConfigTextUtils.cpp")

        self.assertIn("#pragma once", header)
        self.assertIn("namespace ConfigTextUtils", header)
        self.assertIn("QString stripInlineComment(QString line)", header)
        self.assertIn("line.indexOf(QLatin1Char('#'))", cpp)
        self.assertIn("line.indexOf(QLatin1Char(';'))", cpp)
        self.assertIn("line.left(cutPos).trimmed()", cpp)
        self.assertIn("line.trimmed()", cpp)

    def test_callers_use_shared_config_text_helper_without_local_duplicates(self):
        dimm = read("src/DIMM.cpp")
        hot_pixel_templates = read("src/HotPixelTemplateSettings.cpp")
        star_config = read("src/InitialStarDetectionConfig.cpp")
        processor = read("src/ImageProcessor.cpp")

        self.assertNotIn('#include "ConfigTextUtils.h"', dimm)
        self.assertIn('#include "ConfigTextUtils.h"', hot_pixel_templates)
        self.assertIn('#include "ConfigTextUtils.h"', star_config)
        self.assertIn('#include "ConfigTextUtils.h"', processor)
        self.assertNotIn("QString stripConfigComment(QString line)", dimm)
        self.assertNotIn("QString stripConfigComment(QString line)", hot_pixel_templates)
        self.assertNotIn("QString stripInlineComment(QString line)", processor)
        self.assertIn("ConfigTextUtils::stripInlineComment(input.readLine())", hot_pixel_templates)
        self.assertIn("ConfigTextUtils::stripInlineComment(input.readLine())", star_config)
        self.assertIn("ConfigTextUtils::stripInlineComment(input.readLine())", processor)

    def test_cmake_explicitly_registers_config_text_utils_sources(self):
        cmake = read("CMakeLists.txt")

        self.assertIn("src/ConfigTextUtils.h", cmake)
        self.assertIn("src/ConfigTextUtils.cpp", cmake)
        self.assertIn("src/HotPixelTemplateSettings.h", cmake)
        self.assertIn("src/HotPixelTemplateSettings.cpp", cmake)


if __name__ == "__main__":
    unittest.main()

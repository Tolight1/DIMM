from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmRuntimeHelpersSplitStaticTest(unittest.TestCase):
    def test_runtime_helpers_are_extracted_from_dimm(self):
        dimm = read("src/DIMM.cpp")
        header = read("src/DimmRuntimeHelpers.h")
        cpp = read("src/DimmRuntimeHelpers.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "kFixedRoiSize",
            "kLiveFullFramePreviewIntervalMs",
            "kAlignmentCandidateDetectionRefreshMs",
            "kFullFrameLocalizationPulseHz",
            "kHardwareTriggerLine",
            "kRoiUpdateGateLine",
        ]:
            self.assertIn(token, header)

        for token in [
            "medianOfSamples",
            "decimalYearFromUtc",
            "alignRoiValue",
            "toggleButtonStyle",
            "uiStatusColor",
            "cameraStatusText",
            "cameraStatusLevel",
            "statusLabelStyle",
            "pulseConfigsMatch",
        ]:
            self.assertIn(token, header)
            self.assertIn(token, cpp)

        self.assertIn('#include "DimmRuntimeHelpers.h"', dimm)
        self.assertNotIn("QString toggleButtonStyle(bool active)", dimm)
        self.assertNotIn("bool pulseConfigsMatch(const PulseGeneratorManager::Config& lhs", dimm)
        self.assertIn("src/DimmRuntimeHelpers.h", cmake)
        self.assertIn("src/DimmRuntimeHelpers.cpp", cmake)

if __name__ == "__main__":
    unittest.main()

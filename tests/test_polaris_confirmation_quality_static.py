from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisConfirmationQualityStaticTest(unittest.TestCase):
    def test_solver_config_has_polaris_confirmation_quality_thresholds(self):
        header = read("src/PolarisSolver.h")

        self.assertIn("double minPolarisSnr", header)
        self.assertIn("bool allowSaturatedPolarisConfirmation", header)
        self.assertIn("bool polarisDetectionQualityRejected", header)

    def test_solver_only_confirms_detected_polaris_when_quality_passes(self):
        cpp = read("src/PolarisSolver.cpp")
        solve_body = cpp.split("PolarisSolveResult solveDetectedStars", 1)[1].split(
            "PolarisSolverController::PolarisSolverController", 1
        )[0]

        self.assertIn("const bool polarisDetectionQualityOk", solve_body)
        self.assertIn("matchedDetected.snr >= config.minPolarisSnr", solve_body)
        self.assertIn("config.allowSaturatedPolarisConfirmation || !matchedDetected.saturated", solve_body)
        self.assertLess(
            solve_body.find("const bool polarisDetectionQualityOk"),
            solve_body.find("result.hasDetectedPolarisPixel = true"),
        )
        self.assertIn("if (polarisDetectionQualityOk)", solve_body)
        self.assertIn("result.polarisDetectionQualityRejected = true", solve_body)

    def test_dimm_polaris_solver_config_sets_quality_defaults(self):
        cpp = read("src/DIMM.Alignment.cpp")
        config_body = cpp.split("PolarisSolverConfig DIMM::buildPolarisSolverConfig", 1)[1].split(
            "void DIMM::onPolarisSolveFinished", 1
        )[0]

        self.assertIn("config.minPolarisSnr = m_alignmentMinPolarisSnr", config_body)
        self.assertIn(
            "config.allowSaturatedPolarisConfirmation = m_alignmentAllowSaturatedPolarisConfirmation",
            config_body,
        )


if __name__ == "__main__":
    unittest.main()

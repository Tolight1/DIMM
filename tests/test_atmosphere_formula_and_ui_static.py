from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AtmosphereFormulaAndUiStaticTest(unittest.TestCase):
    def test_theta0_uses_document_longitudinal_variance_formula(self):
        cpp = read("src/ImageProcessor.cpp")
        calculate_body = cpp.split(
            "AtmosphericParams ImageProcessorWorker::calculateAtmosphere", 1
        )[1].split(
            "double ImageProcessorWorker::", 1
        )[0]

        self.assertIn("constexpr double kRadToArcsec = 206265.0", calculate_body)
        self.assertIn(
            "const double sigmaLongitudinalArcsec2 = "
            "sigmaLongitudinal2 * kRadToArcsec * kRadToArcsec",
            calculate_body,
        )
        self.assertIn(
            "params.theta0 = 0.64 * (4.0 / std::pow(sigmaLongitudinalArcsec2, 0.65)) * "
            "std::pow(cosZenith, 8.0 / 5.0)",
            calculate_body,
        )
        self.assertNotIn("EFFECTIVE_TURBULENCE_HEIGHT_M", calculate_body)

    def test_tau0_uses_differential_autocorrelation_one_over_e(self):
        header = read("src/ImageProcessor.h")
        cpp = read("src/ImageProcessor.cpp")
        calculate_body = cpp.split(
            "AtmosphericParams ImageProcessorWorker::calculateAtmosphere", 1
        )[1].split(
            "double ImageProcessorWorker::", 1
        )[0]

        self.assertIn("estimateDifferentialAutocorrelationTimeMs", header)
        self.assertIn("estimateScalarAutocorrelationCrossingMs", header)
        self.assertIn("params.tau0 = estimateDifferentialAutocorrelationTimeMs(samples)", calculate_body)
        self.assertIn("const double kOneOverE = 1.0 / std::exp(1.0)", cpp)
        self.assertIn("sample.longitudinal", cpp)
        self.assertIn("sample.transverse", cpp)
        self.assertNotIn("estimateCorrelationPeakLagMs", cpp)
        self.assertNotIn("effectiveWindSpeed", calculate_body)

    def test_roddier_longitudinal_coefficient_matches_document(self):
        cpp = read("src/ImageProcessor.cpp")

        self.assertIn("0.0968 * invBaseline", cpp)
        self.assertNotIn("0.097 * invBaseline", cpp)

    def test_main_ui_shows_all_four_atmosphere_parameter_cards(self):
        dimm_ui_cpp = read("src/DIMM.Ui.cpp")
        dimm_ui = read("src/DIMM.ui")

        main_window_body = dimm_ui_cpp.split("void DIMM::setupMainWindowUi", 1)[1].split(
            "void DIMM::setupPreviewCanvases", 1
        )[0]
        self.assertNotIn("ui->thetaCard->hide()", main_window_body)
        self.assertNotIn("ui->tauCard->hide()", main_window_body)
        self.assertIn("ui->thetaCard->setVisible(true)", main_window_body)
        self.assertIn("ui->tauCard->setVisible(true)", main_window_body)

        for widget_name in [
            'name="r0Card"',
            'name="seeingCard"',
            'name="thetaCard"',
            'name="tauCard"',
        ]:
            self.assertIn(widget_name, dimm_ui)

    def test_result_file_header_records_optical_parameters(self):
        dimm_cpp = read("src/DIMM.Results.cpp")
        init_body = dimm_cpp.split("void DIMM::initResultFile()", 1)[1].split(
            "void DIMM::initDetailResultFile()", 1
        )[0]

        self.assertIn("aperture_mm=%3", init_body)
        self.assertIn("baseline_mm=%4", init_body)
        self.assertIn("baseline_angle_deg=%5", init_body)
        self.assertIn("focal_cm=%6", init_body)
        self.assertIn("zenith_deg=%7", init_body)
        self.assertIn("wavelength_nm=%8", init_body)
        self.assertIn("pixel_um=%9", init_body)


if __name__ == "__main__":
    unittest.main()

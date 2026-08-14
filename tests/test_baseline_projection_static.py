from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class BaselineProjectionStaticTest(unittest.TestCase):
    def test_optics_chain_carries_baseline_angle_to_worker(self):
        dimm_cpp = read("src/DIMM.cpp")
        settings_h = read("src/SettingsDialog.h")
        settings_cpp = read("src/SettingsDialog.cpp")
        config_application_cpp = read("src/ConfigApplicationController.cpp")
        processor_h = read("src/ImageProcessor.h")
        processor_cpp = read("src/ImageProcessor.cpp")

        self.assertIn("double baselineAngleDeg", settings_h)
        self.assertIn("QLineEdit* opticsBaselineAngle", settings_h)
        self.assertIn("double baselineAngleDeg", processor_h)
        self.assertIn("double baselineAngleDeg() const", processor_h)
        self.assertIn("double m_baselineAngleDeg = 0.0", processor_h)

        self.assertIn("基线方向角", settings_cpp)
        self.assertIn('opticsBaseline = new QLineEdit(QStringLiteral("250"))', settings_cpp)
        self.assertIn("opticsBaselineAngle = new QLineEdit(QStringLiteral(\"0\"))", settings_cpp)
        self.assertIn('opticsZenith = new QLineEdit(QStringLiteral("49.6"))', settings_cpp)
        self.assertIn("opticsBaselineAngle->text().toDouble(&ok)", settings_cpp)
        self.assertIn("appConfig.optical.baselineAngleDeg", settings_cpp)
        self.assertIn("callbacks.applyOptics(config.optical.apertureDiameterMm", config_application_cpp)
        self.assertIn("config.optical.baselineAngleDeg", config_application_cpp)
        self.assertIn("m_imageProcessor->setOpticalParams(apertureDiameterMm", dimm_cpp)
        self.assertIn("baselineAngleDeg", dimm_cpp)
        self.assertIn("m_settingsDialog->opticsBaselineAngle->setText", dimm_cpp)
        self.assertIn("m_imageProcessor->baselineAngleDeg()", dimm_cpp)

        self.assertIn("m_baselineAngleDeg = baselineAngleDeg", processor_cpp)
        self.assertIn("Q_ARG(double, baselineAngleDeg)", processor_cpp)
        self.assertIn("double m_baselineSeparation = 250e-3", processor_h)
        self.assertIn("double m_baselineSeparationMm = 250.0", processor_h)
        self.assertIn("double m_zenithAngleDeg = 49.6", processor_h)

    def test_differential_centroids_are_projected_onto_baseline_axes(self):
        processor_cpp = read("src/ImageProcessor.cpp")
        append_body = processor_cpp.split("bool ImageProcessorWorker::appendDifferentialSample()", 1)[1].split(
            "void ImageProcessorWorker::emitRoiPreviewIfDue", 1
        )[0]

        self.assertIn("baselineAngleDeg = m_baselineAngleDeg", append_body)
        self.assertIn("baselineAngleRad", append_body)
        self.assertIn("const double dx = cam1.centroid.x - cam0.centroid.x", append_body)
        self.assertIn("const double dy = cam1.centroid.y - cam0.centroid.y", append_body)
        self.assertIn("sample.longitudinal = dx * baselineCos + dy * baselineSin", append_body)
        self.assertIn("sample.transverse = -dx * baselineSin + dy * baselineCos", append_body)
        self.assertNotIn("sample.longitudinal = cam1.centroid.x - cam0.centroid.x", append_body)
        self.assertNotIn("sample.transverse = cam1.centroid.y - cam0.centroid.y", append_body)


if __name__ == "__main__":
    unittest.main()

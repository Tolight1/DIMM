from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisOptimizationStage1StaticTest(unittest.TestCase):
    def test_optics_default_uses_millimeters(self):
        cpp = read("src/DIMM.cpp")
        image_processor_h = read("src/ImageProcessor.h")

        self.assertIn('opticsLayout->addWidget(new QLabel(QStringLiteral("焦距 f (mm):")), 2, 0)', cpp)
        self.assertIn('opticsF = new QLineEdit(QStringLiteral("269"))', cpp)
        self.assertIn("double m_focalLengthMm = 269.0", image_processor_h)
        self.assertIn("double m_f = 0.269", image_processor_h)
        self.assertIn("m_f = std::max(0.01, focalLengthMm / 1000.0)", read("src/ImageProcessor.cpp"))
        self.assertIn('alignmentFocalLengthEdit = new QLineEdit(QStringLiteral("269"))', cpp)

    def test_solved_orbit_preference_changes_radius_branch(self):
        cpp = read("src/DIMM.Alignment.cpp")
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")
        radius_body = cpp.split("double DIMM::alignmentOrbitRadiusPx", 1)[1].split(
            "void DIMM::handleAlignmentFramePacket", 1
        )[0]
        overlay_body = cpp.split("void DIMM::updateAlignmentOverlay", 1)[1].split(
            "void DIMM::onStartCapture", 1
        )[0]

        self.assertNotIn("m_alignmentAutoRadius ? autoRadius : autoRadius", radius_body)
        self.assertIn("fallbackAlignmentOrbitRadiusPx()", cpp)
        self.assertIn("overlayInput.useSolvedOrbit = m_alignmentAutoRadius", overlay_body)
        self.assertIn("overlayInput.radiusAdjustPx = m_alignmentRadiusAdjustPx", overlay_body)
        self.assertIn("solved.polarisPolarRadiusPx + input.radiusAdjustPx", presenter_cpp)

    def test_show_matched_catalog_stars_controls_overlay_mapping(self):
        cpp = read("src/DIMM.Alignment.cpp")
        presenter_cpp = read("src/AlignmentUiPresenter.cpp")
        config_body = cpp.split("PolarisSolverConfig DIMM::buildPolarisSolverConfig", 1)[1].split(
            "void DIMM::onPolarisSolveFinished", 1
        )[0]

        self.assertIn("bool m_alignmentShowMatchedCatalogStars", read("src/DIMM.h"))
        self.assertIn("config.showMatchedCatalogStars = m_alignmentShowMatchedCatalogStars", config_body)
        self.assertIn("solved.showMatchedCatalogStars", presenter_cpp)
        self.assertIn("for (const CatalogImageMatch& match : solved.matches)", presenter_cpp)

    def test_solver_result_carries_overlay_and_confirmation_position_separately(self):
        header = read("src/PolarisSolver.h")
        cpp = read("src/PolarisSolver.cpp")
        dimm = read("src/DIMM.Alignment.cpp")
        result_body = dimm.split("void DIMM::onPolarisSolveFinished", 1)[1].split(
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates", 1
        )[0]

        self.assertIn("bool showMatchedCatalogStars", header)
        self.assertIn("bool hasPredictedPolarisPixel", header)
        self.assertIn("QPointF predictedPolarisPixel", header)
        self.assertIn("bool hasDetectedPolarisPixel", header)
        self.assertIn("QPointF detectedPolarisPixel", header)
        self.assertIn("result.predictedPolarisPixel = matchResult.polarisPixel", cpp)
        self.assertIn("result.detectedPolarisPixel = outputMatch.detectedPixel", cpp)
        self.assertIn("runtime.confirmedPolarisPosition[result.cameraIndex] = result.detectedPolarisPixel", result_body)
        self.assertNotIn("runtime.confirmedPolarisPosition[result.cameraIndex] = result.polarisPixel", result_body)

    def test_observation_epoch_is_derived_from_current_utc(self):
        cpp = read("src/DIMM.Alignment.cpp")
        config_body = cpp.split("PolarisSolverConfig DIMM::buildPolarisSolverConfig", 1)[1].split(
            "void DIMM::onPolarisSolveFinished", 1
        )[0]

        self.assertIn("decimalYearFromUtc", cpp)
        self.assertIn("QDateTime::currentDateTimeUtc()", config_body)
        self.assertNotIn("config.observationEpochYear = 2026.5", config_body)


if __name__ == "__main__":
    unittest.main()
